// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_editor.hpp"
#include "av_core/chart/chart_validator.hpp"
#include "av_core/project/project_serialization.hpp"
#include "av_core/slk/slk_chart_draft_projection.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using otgy::core::ChartColumn;
using otgy::core::ChartDraft;
using otgy::core::ChartEditor;
using otgy::core::ChartRole;
using otgy::core::ChartRoleEvent;
using otgy::core::ChartRoleEventType;
using otgy::core::ProjectSerializationError;

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input.is_open());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] ChartDraft canonical_draft() {
  const auto bytes =
      read_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  const auto import =
      otgy::core::import_finished_slk(bytes.data(), bytes.size());
  assert(import);
  const auto projection =
      otgy::core::project_finished_slk_to_chart_draft(*import.document());
  assert(projection);
  return *projection.draft();
}

[[nodiscard]] std::uint64_t double_bits(double value) {
  std::uint64_t bits = 0U;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

[[nodiscard]] std::uint32_t crc32(const std::uint8_t* bytes,
                                  std::size_t count) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (std::size_t index = 0U; index < count; ++index) {
    crc ^= bytes[index];
    for (unsigned int bit = 0U; bit < 8U; ++bit) {
      const auto mask = static_cast<std::uint32_t>(
          -static_cast<std::int32_t>(crc & 1U));
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

void write_u32(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint32_t value) {
  for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
    bytes[offset++] = static_cast<std::uint8_t>(value >> shift);
  }
}

void write_u64(std::vector<std::uint8_t>& bytes, std::size_t offset,
               std::uint64_t value) {
  for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
    bytes[offset++] = static_cast<std::uint8_t>(value >> shift);
  }
}

void refresh_integrity(std::vector<std::uint8_t>& bytes) {
  assert(bytes.size() >= 24U);
  write_u64(bytes, 12U, bytes.size() - 24U);
  write_u32(bytes, 20U, crc32(bytes.data() + 24U, bytes.size() - 24U));
}

void append_double_bits(std::vector<std::uint8_t>& bytes, double value) {
  const auto bits = double_bits(value);
  for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
    bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
  }
}

[[nodiscard]] std::vector<std::uint8_t> legacy_formal_payload(
    const ChartDraft& chart, double bpm, double offset) {
  const auto current = otgy::core::encode_formal_draft(chart);
  assert(current);
  const auto& encoded = *current.bytes();
  std::vector<std::uint8_t> payload;
  payload.reserve(16U + encoded.size() - 24U);
  append_double_bits(payload, bpm);
  append_double_bits(payload, offset);
  payload.insert(payload.end(), encoded.begin() + 24, encoded.end());
  std::vector<std::uint8_t> result(encoded.begin(), encoded.begin() + 24);
  result[8U] = 1U;
  result[9U] = 0U;
  write_u64(result, 12U, payload.size());
  write_u32(result, 20U, crc32(payload.data(), payload.size()));
  result.insert(result.end(), payload.begin(), payload.end());
  return result;
}

void test_formal_and_recovery_round_trip() {
  const auto chart = canonical_draft();
  const auto formal = otgy::core::encode_formal_draft(chart);
  const auto repeated = otgy::core::encode_formal_draft(chart);
  const auto recovery = otgy::core::encode_recovery_snapshot(chart);
  assert(formal && repeated && recovery);
  assert(*formal.bytes() == *repeated.bytes());
  assert(formal.bytes()->size() == recovery.bytes()->size());
  assert(formal.bytes()->at(8U) == 2U);
  assert(formal.bytes()->at(10U) == 1U);
  assert(recovery.bytes()->at(10U) == 2U);

  auto formal_bytes = *formal.bytes();
  auto decoded = otgy::core::decode_formal_draft(formal_bytes.data(),
                                                  formal_bytes.size());
  assert(decoded);
  formal_bytes.clear();
  assert(otgy::core::chart_drafts_equal(decoded.payload()->chart, chart));

  auto recovery_bytes = *recovery.bytes();
  auto recovered = otgy::core::decode_recovery_snapshot(
      recovery_bytes.data(), recovery_bytes.size());
  assert(recovered);
  recovery_bytes.clear();
  assert(otgy::core::chart_drafts_equal(*recovered.chart(), chart));

  const auto decoded_copy = *decoded.payload();
  const auto decoded_move = std::move(decoded).take_payload();
  assert(otgy::core::chart_drafts_equal(decoded_copy.chart, chart));
  assert(otgy::core::chart_drafts_equal(decoded_move.chart, chart));
  assert(decoded_copy.chart.role_events().data() !=
         decoded_move.chart.role_events().data());
}

void test_empty_payload_canonical_bytes() {
  const ChartDraft empty{{}, {}, {}, {}, {}, {}};
  const auto formal = otgy::core::encode_formal_draft(empty);
  const auto recovery = otgy::core::encode_recovery_snapshot(empty);
  assert(formal && recovery);
  assert(formal.bytes()->size() == 72U);
  assert(recovery.bytes()->size() == 72U);
  const std::vector<std::uint8_t> prefix{'O', 'T', 'G', 'Y', 'P', 'R', 'J',
                                         0U, 2U, 0U};
  for (std::size_t index = 0U; index < prefix.size(); ++index) {
    assert(formal.bytes()->at(index) == prefix[index]);
    assert(recovery.bytes()->at(index) == prefix[index]);
  }
  assert(formal.bytes()->at(10U) == 1U);
  assert(recovery.bytes()->at(10U) == 2U);
  assert(formal.bytes()->at(12U) == 48U);
  assert(recovery.bytes()->at(12U) == 48U);
  assert(formal.bytes()->at(20U) == 0x95U);
  assert(formal.bytes()->at(21U) == 0xB3U);
  assert(formal.bytes()->at(22U) == 0x88U);
  assert(formal.bytes()->at(23U) == 0xF2U);
  assert(recovery.bytes()->at(20U) == 0x95U);
  assert(recovery.bytes()->at(21U) == 0xB3U);
  assert(recovery.bytes()->at(22U) == 0x88U);
  assert(recovery.bytes()->at(23U) == 0xF2U);
  for (std::size_t index = 0U; index < formal.bytes()->size(); ++index) {
    if (index == 10U) {
      assert(formal.bytes()->at(index) != recovery.bytes()->at(index));
    } else {
      assert(formal.bytes()->at(index) == recovery.bytes()->at(index));
    }
  }
}

void test_incomplete_draft_and_history_exclusion() {
  ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
  assert(editor.click(ChartColumn::Npc, 0U));
  assert(editor.click(ChartColumn::Npc, 7U));
  assert(editor.click(ChartColumn::Player, 10U));
  assert(editor.can_undo());
  const auto validation = otgy::core::validate_finished_chart(editor.draft());
  assert(!validation.is_finished());

  const auto formal = otgy::core::encode_formal_draft(editor.draft());
  const auto recovery =
      otgy::core::encode_recovery_snapshot(editor.draft());
  assert(formal && recovery);
  auto decoded = otgy::core::decode_formal_draft(formal.bytes()->data(),
                                                  formal.bytes()->size());
  auto recovered = otgy::core::decode_recovery_snapshot(
      recovery.bytes()->data(), recovery.bytes()->size());
  assert(decoded && recovered);
  assert(otgy::core::chart_drafts_equal(decoded.payload()->chart,
                                        editor.draft()));
  assert(otgy::core::chart_drafts_equal(*recovered.chart(), editor.draft()));

  ChartEditor restored{std::move(recovered).take_chart()};
  assert(!restored.can_undo());
  assert(!restored.can_redo());
}

void test_legacy_formal_timing_is_discarded() {
  const ChartDraft chart = canonical_draft();
  const double bpm = 129.24970691676435;
  const double offset = -0.090521541950113374;
  auto legacy = legacy_formal_payload(chart, bpm, offset);
  assert(legacy.at(8U) == 1U);
  auto decoded =
      otgy::core::decode_formal_draft(legacy.data(), legacy.size());
  assert(decoded);
  assert(otgy::core::chart_drafts_equal(decoded.payload()->chart, chart));

  auto other =
      legacy_formal_payload(chart, std::nextafter(bpm, 130.0),
                            std::nextafter(offset, 0.0));
  assert(legacy != other);
  auto other_decoded =
      otgy::core::decode_formal_draft(other.data(), other.size());
  assert(other_decoded);
  assert(otgy::core::chart_drafts_equal(other_decoded.payload()->chart,
                                        decoded.payload()->chart));
  const auto current = otgy::core::encode_formal_draft(
      decoded.payload()->chart);
  assert(current);
  assert(otgy::core::chart_drafts_equal(
      otgy::core::decode_formal_draft(current.bytes()->data(),
                                      current.bytes()->size())
          .payload()
          ->chart,
      chart));
}

void test_input_and_header_errors() {
  const ChartDraft empty{{}, {}, {}, {}, {}, {}};
  const auto formal = otgy::core::encode_formal_draft(empty);
  const auto recovery = otgy::core::encode_recovery_snapshot(empty);
  assert(formal && recovery);

  assert(otgy::core::decode_formal_draft(nullptr, 0U).error() ==
         ProjectSerializationError::EmptyInput);
  assert(otgy::core::decode_formal_draft(nullptr, 1U).error() ==
         ProjectSerializationError::NullInput);
  assert(otgy::core::decode_formal_draft(formal.bytes()->data(), 1U).error() ==
         ProjectSerializationError::LengthMismatch);
  assert(otgy::core::decode_formal_draft(recovery.bytes()->data(),
                                         recovery.bytes()->size())
             .error() == ProjectSerializationError::UnexpectedPayloadKind);
  assert(otgy::core::decode_recovery_snapshot(formal.bytes()->data(),
                                              formal.bytes()->size())
             .error() == ProjectSerializationError::UnexpectedPayloadKind);

  auto bytes = *formal.bytes();
  bytes[0U] ^= 1U;
  assert(otgy::core::decode_formal_draft(bytes.data(), bytes.size()).error() ==
         ProjectSerializationError::InvalidMagic);
  bytes = *formal.bytes();
  bytes[8U] = 3U;
  assert(otgy::core::decode_formal_draft(bytes.data(), bytes.size()).error() ==
         ProjectSerializationError::UnsupportedVersion);
  bytes = *formal.bytes();
  bytes[11U] = 1U;
  assert(otgy::core::decode_formal_draft(bytes.data(), bytes.size()).error() ==
         ProjectSerializationError::InvalidHeader);
  bytes = *formal.bytes();
  bytes[12U] += 1U;
  assert(otgy::core::decode_formal_draft(bytes.data(), bytes.size()).error() ==
         ProjectSerializationError::LengthMismatch);
  bytes = *formal.bytes();
  bytes.back() ^= 1U;
  assert(otgy::core::decode_formal_draft(bytes.data(), bytes.size()).error() ==
         ProjectSerializationError::IntegrityMismatch);
  bytes = *formal.bytes();
  bytes.pop_back();
  assert(otgy::core::decode_formal_draft(bytes.data(), bytes.size()).error() ==
         ProjectSerializationError::LengthMismatch);
  bytes = *formal.bytes();
  bytes.push_back(0U);
  assert(otgy::core::decode_formal_draft(bytes.data(), bytes.size()).error() ==
         ProjectSerializationError::LengthMismatch);
}

void test_malformed_payload_and_invalid_values() {
  const ChartRoleEvent event{{1U}, 2U, ChartRole::Npc,
                             ChartRoleEventType::Note, {3U}, {4U}};
  const ChartDraft chart{{event}, {}, {}, {}, {}, {}};
  const auto recovery = otgy::core::encode_recovery_snapshot(chart);
  assert(recovery);

  auto bytes = *recovery.bytes();
  bytes[48U] = 255U;
  refresh_integrity(bytes);
  assert(otgy::core::decode_recovery_snapshot(bytes.data(), bytes.size())
             .error() == ProjectSerializationError::MalformedPayload);

  bytes = *recovery.bytes();
  write_u64(bytes, 24U, std::numeric_limits<std::uint64_t>::max());
  refresh_integrity(bytes);
  assert(otgy::core::decode_recovery_snapshot(bytes.data(), bytes.size())
             .error() == ProjectSerializationError::MalformedPayload);

  bytes = *recovery.bytes();
  bytes.push_back(0U);
  refresh_integrity(bytes);
  assert(otgy::core::decode_recovery_snapshot(bytes.data(), bytes.size())
             .error() == ProjectSerializationError::MalformedPayload);

  auto invalid_events = chart.role_events();
  invalid_events.front().role = static_cast<ChartRole>(255);
  const ChartDraft invalid{std::move(invalid_events), {}, {}, {}, {}, {}};
  assert(otgy::core::encode_recovery_snapshot(invalid).error() ==
         ProjectSerializationError::InvalidDraft);

  assert(otgy::core::encode_formal_draft(
             invalid)
             .error() == ProjectSerializationError::InvalidDraft);

  auto legacy = legacy_formal_payload(
      chart, std::numeric_limits<double>::infinity(), 0.0);
  assert(otgy::core::decode_formal_draft(legacy.data(), legacy.size())
             .error() == ProjectSerializationError::InvalidTiming);
  legacy = legacy_formal_payload(chart, 100.0, 30.001);
  assert(otgy::core::decode_formal_draft(legacy.data(), legacy.size())
             .error() == ProjectSerializationError::InvalidTiming);
}

void test_error_messages() {
  for (unsigned int raw = 0U;
       raw <= static_cast<unsigned int>(
                  ProjectSerializationError::AllocationFailure);
       ++raw) {
    const auto message = otgy::core::project_serialization_error_message(
        static_cast<ProjectSerializationError>(raw));
    if (raw == 0U) {
      assert(message.empty());
    } else {
      assert(!message.empty());
    }
  }
  assert(!otgy::core::project_serialization_error_message(
              static_cast<ProjectSerializationError>(255))
              .empty());
}

}  // namespace

int main() {
  test_formal_and_recovery_round_trip();
  test_empty_payload_canonical_bytes();
  test_incomplete_draft_and_history_exclusion();
  test_legacy_formal_timing_is_discarded();
  test_input_and_header_errors();
  test_malformed_payload_and_invalid_values();
  test_error_messages();
  return 0;
}
