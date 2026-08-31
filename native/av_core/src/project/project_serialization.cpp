// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/project/project_serialization.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace otgy::core {
namespace {

constexpr std::uint8_t kMagic[8] = {'O', 'T', 'G', 'Y', 'P', 'R', 'J', 0U};
constexpr std::size_t kHeaderSize = 24U;

class Writer final {
 public:
  void u8(std::uint8_t value) { bytes_.push_back(value); }

  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value));
    u8(static_cast<std::uint8_t>(value >> 8U));
  }

  void u32(std::uint32_t value) {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
      u8(static_cast<std::uint8_t>(value >> shift));
    }
  }

  void u64(std::uint64_t value) {
    for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
      u8(static_cast<std::uint8_t>(value >> shift));
    }
  }

  void raw(const std::uint8_t* bytes, std::size_t count) {
    bytes_.insert(bytes_.end(), bytes, bytes + count);
  }

  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
    return bytes_;
  }

  [[nodiscard]] std::vector<std::uint8_t> take() && {
    return std::move(bytes_);
  }

 private:
  std::vector<std::uint8_t> bytes_;
};

class Reader final {
 public:
  Reader(const std::uint8_t* bytes, std::size_t size) noexcept
      : bytes_(bytes), size_(size), position_(0U) {}

  [[nodiscard]] bool u8(std::uint8_t& value) noexcept {
    if (remaining() < 1U) {
      return false;
    }
    value = bytes_[position_++];
    return true;
  }

  [[nodiscard]] bool u16(std::uint16_t& value) noexcept {
    std::uint8_t low = 0U;
    std::uint8_t high = 0U;
    if (!u8(low) || !u8(high)) {
      return false;
    }
    value = static_cast<std::uint16_t>(low) |
            static_cast<std::uint16_t>(high) << 8U;
    return true;
  }

  [[nodiscard]] bool u32(std::uint32_t& value) noexcept {
    value = 0U;
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
      std::uint8_t byte = 0U;
      if (!u8(byte)) {
        return false;
      }
      value |= static_cast<std::uint32_t>(byte) << shift;
    }
    return true;
  }

  [[nodiscard]] bool u64(std::uint64_t& value) noexcept {
    value = 0U;
    for (unsigned int shift = 0U; shift < 64U; shift += 8U) {
      std::uint8_t byte = 0U;
      if (!u8(byte)) {
        return false;
      }
      value |= static_cast<std::uint64_t>(byte) << shift;
    }
    return true;
  }

  [[nodiscard]] bool raw(std::uint8_t* target, std::size_t count) noexcept {
    if (remaining() < count) {
      return false;
    }
    std::memcpy(target, bytes_ + position_, count);
    position_ += count;
    return true;
  }

  [[nodiscard]] std::size_t remaining() const noexcept {
    return size_ - position_;
  }

  [[nodiscard]] const std::uint8_t* current() const noexcept {
    return bytes_ + position_;
  }

 private:
  const std::uint8_t* bytes_;
  std::size_t size_;
  std::size_t position_;
};

[[nodiscard]] std::uint32_t crc32(const std::uint8_t* bytes,
                                  std::size_t count) noexcept {
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

[[nodiscard]] bool valid_role(ChartRole role) noexcept {
  return role == ChartRole::Npc || role == ChartRole::Player;
}

[[nodiscard]] bool valid_role_event_type(ChartRoleEventType type) noexcept {
  return type == ChartRoleEventType::BlockStart ||
         type == ChartRoleEventType::Note ||
         type == ChartRoleEventType::MoveEnd;
}

void write_ids(Writer& writer, const std::vector<ChartEventId>& ids) {
  writer.u64(static_cast<std::uint64_t>(ids.size()));
  for (const auto id : ids) {
    writer.u64(id.value);
  }
}

void write_group_ids(Writer& writer,
                     const std::vector<ChartDrillGroupId>& ids) {
  writer.u64(static_cast<std::uint64_t>(ids.size()));
  for (const auto id : ids) {
    writer.u64(id.value);
  }
}

void write_drill_ids(Writer& writer,
                     const std::vector<ChartDanceMoveDrillId>& ids) {
  writer.u64(static_cast<std::uint64_t>(ids.size()));
  for (const auto id : ids) {
    writer.u64(id.value);
  }
}

[[nodiscard]] bool write_chart(Writer& writer, const ChartDraft& chart) {
  writer.u64(static_cast<std::uint64_t>(chart.role_events().size()));
  for (const auto& event : chart.role_events()) {
    if (!valid_role(event.role) || !valid_role_event_type(event.type)) {
      return false;
    }
    writer.u64(event.id.value);
    writer.u64(event.row);
    writer.u8(static_cast<std::uint8_t>(event.role));
    writer.u8(static_cast<std::uint8_t>(event.type));
    writer.u64(event.drill_group_id.value);
    writer.u64(event.dance_move_drill_id.value);
  }

  writer.u64(
      static_cast<std::uint64_t>(chart.showtime_start_events().size()));
  for (const auto& event : chart.showtime_start_events()) {
    writer.u64(event.id.value);
    writer.u64(event.row);
    writer.u64(event.showtime_id.value);
  }

  writer.u64(
      static_cast<std::uint64_t>(chart.showtime_move_events().size()));
  for (const auto& event : chart.showtime_move_events()) {
    writer.u64(event.id.value);
    writer.u64(event.row);
    writer.u64(event.showtime_id.value);
    writer.u64(event.dance_move_drill_id.value);
    writer.u64(event.show_n_count);
  }

  writer.u64(static_cast<std::uint64_t>(chart.drill_groups().size()));
  for (const auto& group : chart.drill_groups()) {
    writer.u64(group.id.value);
    writer.u64(group.dance_move_drill_id.value);
    write_ids(writer, group.npc_event_ids);
    write_ids(writer, group.player_event_ids);
  }

  writer.u64(static_cast<std::uint64_t>(chart.dance_move_drills().size()));
  for (const auto& drill : chart.dance_move_drills()) {
    writer.u64(drill.id.value);
    writer.u64(drill.move_n_count);
    write_group_ids(writer, drill.drill_group_ids);
  }

  writer.u64(static_cast<std::uint64_t>(chart.showtimes().size()));
  for (const auto& showtime : chart.showtimes()) {
    writer.u64(showtime.id.value);
    writer.u64(showtime.start_event_id.value);
    write_drill_ids(writer, showtime.dance_move_drill_ids);
    write_ids(writer, showtime.show_move_event_ids);
  }
  return true;
}

template <typename Value, typename ReadValue>
[[nodiscard]] bool read_vector(Reader& reader, std::size_t minimum_size,
                               std::vector<Value>& values,
                               ReadValue read_value) {
  std::uint64_t count = 0U;
  if (!reader.u64(count) ||
      count > static_cast<std::uint64_t>(values.max_size()) ||
      (minimum_size != 0U &&
       count > static_cast<std::uint64_t>(reader.remaining() / minimum_size))) {
    return false;
  }
  values.reserve(static_cast<std::size_t>(count));
  for (std::uint64_t index = 0U; index < count; ++index) {
    Value value{};
    if (!read_value(reader, value)) {
      return false;
    }
    values.push_back(std::move(value));
  }
  return true;
}

[[nodiscard]] bool read_event_ids(Reader& reader,
                                  std::vector<ChartEventId>& ids) {
  return read_vector(reader, 8U, ids,
                     [](Reader& source, ChartEventId& id) {
                       return source.u64(id.value);
                     });
}

[[nodiscard]] bool read_group_ids(Reader& reader,
                                  std::vector<ChartDrillGroupId>& ids) {
  return read_vector(reader, 8U, ids,
                     [](Reader& source, ChartDrillGroupId& id) {
                       return source.u64(id.value);
                     });
}

[[nodiscard]] bool read_drill_ids(
    Reader& reader, std::vector<ChartDanceMoveDrillId>& ids) {
  return read_vector(reader, 8U, ids,
                     [](Reader& source, ChartDanceMoveDrillId& id) {
                       return source.u64(id.value);
                     });
}

[[nodiscard]] bool read_chart(Reader& reader, ChartDraft& chart) {
  std::vector<ChartRoleEvent> role_events;
  if (!read_vector(reader, 34U, role_events,
                   [](Reader& source, ChartRoleEvent& event) {
                     std::uint8_t role = 0U;
                     std::uint8_t type = 0U;
                     if (!source.u64(event.id.value) ||
                         !source.u64(event.row) || !source.u8(role) ||
                         !source.u8(type) ||
                         !source.u64(event.drill_group_id.value) ||
                         !source.u64(event.dance_move_drill_id.value)) {
                       return false;
                     }
                     event.role = static_cast<ChartRole>(role);
                     event.type = static_cast<ChartRoleEventType>(type);
                     return valid_role(event.role) &&
                            valid_role_event_type(event.type);
                   })) {
    return false;
  }

  std::vector<ChartShowtimeStartEvent> start_events;
  if (!read_vector(reader, 24U, start_events,
                   [](Reader& source, ChartShowtimeStartEvent& event) {
                     return source.u64(event.id.value) &&
                            source.u64(event.row) &&
                            source.u64(event.showtime_id.value);
                   })) {
    return false;
  }

  std::vector<ChartShowtimeMoveEvent> move_events;
  if (!read_vector(reader, 40U, move_events,
                   [](Reader& source, ChartShowtimeMoveEvent& event) {
                     return source.u64(event.id.value) &&
                            source.u64(event.row) &&
                            source.u64(event.showtime_id.value) &&
                            source.u64(event.dance_move_drill_id.value) &&
                            source.u64(event.show_n_count);
                   })) {
    return false;
  }

  std::vector<ChartDrillGroup> groups;
  if (!read_vector(reader, 32U, groups,
                   [](Reader& source, ChartDrillGroup& group) {
                     return source.u64(group.id.value) &&
                            source.u64(group.dance_move_drill_id.value) &&
                            read_event_ids(source, group.npc_event_ids) &&
                            read_event_ids(source, group.player_event_ids);
                   })) {
    return false;
  }

  std::vector<ChartDanceMoveDrill> drills;
  if (!read_vector(reader, 24U, drills,
                   [](Reader& source, ChartDanceMoveDrill& drill) {
                     return source.u64(drill.id.value) &&
                            source.u64(drill.move_n_count) &&
                            read_group_ids(source, drill.drill_group_ids);
                   })) {
    return false;
  }

  std::vector<ChartShowtime> showtimes;
  if (!read_vector(reader, 32U, showtimes,
                   [](Reader& source, ChartShowtime& showtime) {
                     return source.u64(showtime.id.value) &&
                            source.u64(showtime.start_event_id.value) &&
                            read_drill_ids(source,
                                           showtime.dance_move_drill_ids) &&
                            read_event_ids(source,
                                           showtime.show_move_event_ids);
                   })) {
    return false;
  }

  chart = ChartDraft{std::move(role_events), std::move(start_events),
                     std::move(move_events), std::move(groups),
                     std::move(drills), std::move(showtimes)};
  return true;
}

[[nodiscard]] bool valid_timing(double bpm, double offset) noexcept {
  return std::isfinite(bpm) && bpm >= 0.0 && bpm <= 100000.0 &&
         std::isfinite(offset) && offset >= -30.0 && offset <= 30.0;
}

void write_double_bits(Writer& writer, double value) {
  std::uint64_t bits = 0U;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  writer.u64(bits);
}

[[nodiscard]] bool read_double_bits(Reader& reader, double& value) noexcept {
  std::uint64_t bits = 0U;
  if (!reader.u64(bits)) {
    return false;
  }
  std::memcpy(&value, &bits, sizeof(value));
  return true;
}

[[nodiscard]] ProjectBytesResult encode_payload(ProjectPayloadKind kind,
                                                const ChartDraft& chart,
                                                double bpm,
                                                double offset) noexcept {
  try {
    Writer payload;
    if (kind == ProjectPayloadKind::FormalDraft) {
      if (!valid_timing(bpm, offset)) {
        return ProjectBytesResult{ProjectSerializationError::InvalidTiming};
      }
      write_double_bits(payload, bpm);
      write_double_bits(payload, offset);
    }
    if (!write_chart(payload, chart)) {
      return ProjectBytesResult{ProjectSerializationError::InvalidDraft};
    }

    const auto& payload_bytes = payload.bytes();
    Writer result;
    result.raw(kMagic, sizeof(kMagic));
    result.u16(kProjectPayloadVersion);
    result.u8(static_cast<std::uint8_t>(kind));
    result.u8(0U);
    result.u64(static_cast<std::uint64_t>(payload_bytes.size()));
    result.u32(crc32(payload_bytes.data(), payload_bytes.size()));
    result.raw(payload_bytes.data(), payload_bytes.size());
    return ProjectBytesResult{std::move(result).take()};
  } catch (const std::bad_alloc&) {
    return ProjectBytesResult{ProjectSerializationError::AllocationFailure};
  }
}

struct DecodedEnvelope final {
  const std::uint8_t* payload;
  std::size_t payload_size;
};

[[nodiscard]] ProjectSerializationError decode_envelope(
    const std::uint8_t* bytes, std::size_t byte_count,
    ProjectPayloadKind expected_kind, DecodedEnvelope& envelope) noexcept {
  if (bytes == nullptr) {
    return byte_count == 0U ? ProjectSerializationError::EmptyInput
                            : ProjectSerializationError::NullInput;
  }
  if (byte_count == 0U) {
    return ProjectSerializationError::EmptyInput;
  }
  if (byte_count < kHeaderSize) {
    return ProjectSerializationError::LengthMismatch;
  }
  Reader reader{bytes, byte_count};
  std::uint8_t magic[sizeof(kMagic)]{};
  std::uint16_t version = 0U;
  std::uint8_t kind = 0U;
  std::uint8_t flags = 0U;
  std::uint64_t payload_size = 0U;
  std::uint32_t expected_crc = 0U;
  if (!reader.raw(magic, sizeof(magic)) ||
      std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
    return ProjectSerializationError::InvalidMagic;
  }
  if (!reader.u16(version) || !reader.u8(kind) || !reader.u8(flags) ||
      !reader.u64(payload_size) || !reader.u32(expected_crc)) {
    return ProjectSerializationError::LengthMismatch;
  }
  if (version != kProjectPayloadVersion) {
    return ProjectSerializationError::UnsupportedVersion;
  }
  if (kind != static_cast<std::uint8_t>(expected_kind)) {
    return ProjectSerializationError::UnexpectedPayloadKind;
  }
  if (flags != 0U) {
    return ProjectSerializationError::InvalidHeader;
  }
  if (payload_size != static_cast<std::uint64_t>(reader.remaining())) {
    return ProjectSerializationError::LengthMismatch;
  }
  if (crc32(reader.current(), reader.remaining()) != expected_crc) {
    return ProjectSerializationError::IntegrityMismatch;
  }
  envelope = {reader.current(), reader.remaining()};
  return ProjectSerializationError::None;
}

}  // namespace

ProjectBytesResult::ProjectBytesResult(
    std::vector<std::uint8_t> bytes) noexcept
    : storage_(std::move(bytes)) {}

ProjectBytesResult::ProjectBytesResult(ProjectSerializationError error) noexcept
    : storage_(error) {}

ProjectBytesResult::operator bool() const noexcept {
  return std::holds_alternative<std::vector<std::uint8_t>>(storage_);
}

const std::vector<std::uint8_t>* ProjectBytesResult::bytes() const noexcept {
  return std::get_if<std::vector<std::uint8_t>>(&storage_);
}

ProjectSerializationError ProjectBytesResult::error() const noexcept {
  const auto* error = std::get_if<ProjectSerializationError>(&storage_);
  return error == nullptr ? ProjectSerializationError::None : *error;
}

std::vector<std::uint8_t> ProjectBytesResult::take_bytes() && {
  return std::move(std::get<std::vector<std::uint8_t>>(storage_));
}

FormalDraftDecodeResult::FormalDraftDecodeResult(
    FormalDraftPayload payload) noexcept
    : storage_(std::move(payload)) {}

FormalDraftDecodeResult::FormalDraftDecodeResult(
    ProjectSerializationError error) noexcept
    : storage_(error) {}

FormalDraftDecodeResult::operator bool() const noexcept {
  return std::holds_alternative<FormalDraftPayload>(storage_);
}

const FormalDraftPayload* FormalDraftDecodeResult::payload() const noexcept {
  return std::get_if<FormalDraftPayload>(&storage_);
}

ProjectSerializationError FormalDraftDecodeResult::error() const noexcept {
  const auto* error = std::get_if<ProjectSerializationError>(&storage_);
  return error == nullptr ? ProjectSerializationError::None : *error;
}

FormalDraftPayload FormalDraftDecodeResult::take_payload() && {
  return std::move(std::get<FormalDraftPayload>(storage_));
}

RecoverySnapshotDecodeResult::RecoverySnapshotDecodeResult(
    ChartDraft chart) noexcept
    : storage_(std::move(chart)) {}

RecoverySnapshotDecodeResult::RecoverySnapshotDecodeResult(
    ProjectSerializationError error) noexcept
    : storage_(error) {}

RecoverySnapshotDecodeResult::operator bool() const noexcept {
  return std::holds_alternative<ChartDraft>(storage_);
}

const ChartDraft* RecoverySnapshotDecodeResult::chart() const noexcept {
  return std::get_if<ChartDraft>(&storage_);
}

ProjectSerializationError RecoverySnapshotDecodeResult::error() const noexcept {
  const auto* error = std::get_if<ProjectSerializationError>(&storage_);
  return error == nullptr ? ProjectSerializationError::None : *error;
}

ChartDraft RecoverySnapshotDecodeResult::take_chart() && {
  return std::move(std::get<ChartDraft>(storage_));
}

ProjectBytesResult encode_formal_draft(const ChartDraft& chart, double bpm,
                                       double offset_seconds) noexcept {
  return encode_payload(ProjectPayloadKind::FormalDraft, chart, bpm,
                        offset_seconds);
}

FormalDraftDecodeResult decode_formal_draft(const std::uint8_t* bytes,
                                             std::size_t byte_count) noexcept {
  DecodedEnvelope envelope{};
  const auto error = decode_envelope(bytes, byte_count,
                                     ProjectPayloadKind::FormalDraft, envelope);
  if (error != ProjectSerializationError::None) {
    return FormalDraftDecodeResult{error};
  }
  try {
    Reader reader{envelope.payload, envelope.payload_size};
    double bpm = 0.0;
    double offset = 0.0;
    ChartDraft chart{{}, {}, {}, {}, {}, {}};
    if (!read_double_bits(reader, bpm) || !read_double_bits(reader, offset)) {
      return FormalDraftDecodeResult{
          ProjectSerializationError::MalformedPayload};
    }
    if (!valid_timing(bpm, offset)) {
      return FormalDraftDecodeResult{ProjectSerializationError::InvalidTiming};
    }
    if (!read_chart(reader, chart) || reader.remaining() != 0U) {
      return FormalDraftDecodeResult{
          ProjectSerializationError::MalformedPayload};
    }
    return FormalDraftDecodeResult{
        FormalDraftPayload{std::move(chart), bpm, offset}};
  } catch (const std::bad_alloc&) {
    return FormalDraftDecodeResult{
        ProjectSerializationError::AllocationFailure};
  }
}

ProjectBytesResult encode_recovery_snapshot(const ChartDraft& chart) noexcept {
  return encode_payload(ProjectPayloadKind::RecoverySnapshot, chart, 0.0, 0.0);
}

RecoverySnapshotDecodeResult decode_recovery_snapshot(
    const std::uint8_t* bytes, std::size_t byte_count) noexcept {
  DecodedEnvelope envelope{};
  const auto error =
      decode_envelope(bytes, byte_count, ProjectPayloadKind::RecoverySnapshot,
                      envelope);
  if (error != ProjectSerializationError::None) {
    return RecoverySnapshotDecodeResult{error};
  }
  try {
    Reader reader{envelope.payload, envelope.payload_size};
    ChartDraft chart{{}, {}, {}, {}, {}, {}};
    if (!read_chart(reader, chart) || reader.remaining() != 0U) {
      return RecoverySnapshotDecodeResult{
          ProjectSerializationError::MalformedPayload};
    }
    return RecoverySnapshotDecodeResult{std::move(chart)};
  } catch (const std::bad_alloc&) {
    return RecoverySnapshotDecodeResult{
        ProjectSerializationError::AllocationFailure};
  }
}

}  // namespace otgy::core
