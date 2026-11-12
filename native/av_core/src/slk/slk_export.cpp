// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_export.hpp"

#include "av_core/slk/slk_position.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace otgy::core {
namespace {

constexpr char kCanonicalHeaderHex[] =
    "49443b5057584c3b4e3b450d0a503b5047656e6572616c0d0a503b50300d0a503b50302e30300d0a503b50232c2323300d0a503b50232c2323302e30300d0a50"
    "3b50232c2323303b3b5c2d232c2323300d0a503b50232c2323303b3b5b5265645d5c2d232c2323300d0a503b50232c2323302e30303b3b5c2d232c2323302e30"
    "300d0a503b50232c2323302e30303b3b5b5265645d5c2d232c2323302e30300d0a503b50222422232c2323303b3b2224225c2d232c2323300d0a503b50222422"
    "232c2323303b3b5b5265645d2224225c2d232c2323300d0a503b50222422232c2323302e30303b3b2224225c2d232c2323302e30300d0a503b50222422232c23"
    "23302e30303b3b5b5265645d2224225c2d232c2323302e30300d0a503b5030250d0a503b50302e3030250d0a503b50302e3030452b30300d0a503b502323302e"
    "30452b300d0a503b50235c203f2f3f0d0a503b50235c203f3f2f3f3f0d0a503b50797979792f6d2f640d0a503b50645c2d6d6d6d5c2d79790d0a503b50645c2d"
    "6d6d6d0d0a503b506d6d6d5c2d79790d0a503b50683a6d6d5c20414d2f504d0d0a503b50683a6d6d3a73735c20414d2f504d0d0a503b50683a6d6d0d0a503b50"
    "683a6d6d3a73730d0a503b50797979792f6d2f645c20683a6d6d0d0a503b506d6d3a73730d0a503b506d6d3a73732e300d0a503b50400d0a503b505b685d3a6d"
    "6d3a73730d0a503b505f202224222a20232c2323305f203b3b5f202224222a205c2d232c2323305f203b3b5f202224222a20222d225f203b3b5f20405f200d0a"
    "503b505f202a20232c2323305f203b3b5f202a205c2d232c2323305f203b3b5f202a20222d225f203b3b5f20405f200d0a503b505f202224222a20232c232330"
    "2e30305f203b3b5f202224222a205c2d232c2323302e30305f203b3b5f202224222a20222d223f3f5f203b3b5f20405f200d0a503b505f202a20232c2323302e"
    "30305f203b3b5f202a205c2d232c2323302e30305f203b3b5f202a20222d223f3f5f203b3b5f20405f200d0a503b505c24232c2323305f293b3b5c285c24232c"
    "2323305c290d0a503b505c24232c2323305f293b3b5b5265645d5c285c24232c2323305c290d0a503b505c24232c2323302e30305f293b3b5c285c24232c2323"
    "302e30305c290d0a503b505c24232c2323302e30305f293b3b5b5265645d5c285c24232c2323302e30305c290d0a503b506d2f642f79790d0a503b5079797979"
    "22c4ea226d22d4c2226422c8d5220d0a503b506822cab1226d6d22b7d6220d0a503b506822cab1226d6d22b7d622737322c3eb220d0a503b507979797922c4ea"
    "226d22d4c2220d0a503b506d22d4c2226422c8d5220d0a503b50c9cfcee72fcfc2cee76822cab1226d6d22b7d6220d0a503b50c9cfcee72fcfc2cee76822cab1"
    "226d6d22b7d622737322c3eb220d0a503b50225c22232c2323303b3b5c2d225c22232c2323300d0a503b50225c22232c2323303b3b5b5265645d5c2d225c2223"
    "2c2323300d0a503b50225c22232c2323302e30303b3b5c2d225c22232c2323302e30300d0a503b50225c22232c2323302e30303b3b5b5265645d5c2d225c2223"
    "2c2323302e30300d0a503b50797979792f6d6d2f64640d0a503b5064642f6d6d6d2f79790d0a503b5064642f6d6d6d0d0a503b506d6d6d2f79790d0a503b5079"
    "7979792f6d6d2f64645c20683a6d6d0d0a503b505f2d225c222a20232c2323305f2d3b3b5c2d225c222a20232c2323305f2d3b3b5f2d225c222a20222d225f2d"
    "3b3b5f2d405f2d0d0a503b505f2d2a20232c2323305f2d3b3b5c2d2a20232c2323305f2d3b3b5f2d2a20222d225f2d3b3b5f2d405f2d0d0a503b505f2d225c22"
    "2a20232c2323302e30305f2d3b3b5c2d225c222a20232c2323302e30305f2d3b3b5f2d225c222a20222d223f3f5f2d3b3b5f2d405f2d0d0a503b505f2d2a2023"
    "2c2323302e30305f2d3b3b5c2d2a20232c2323302e30305f2d3b3b5f2d2a20222d223f3f5f2d3b3b5f2d405f2d0d0a503b506d6d2f64642f79790d0a503b5079"
    "79797922b3e2225c206d6d22bff9225c20646422c0cf220d0a503b506822bdc3225c206d6d22bad0220d0a503b506822bdc3225c206d6d22bad0225c20737322"
    "c3ca220d0a503b507979797922d2b4225c206d6d22eac5225c20646422eced220d0a503b506d6d2f64640d0a503b50797979795c2f6d6d5c2f64640d0a503b50"
    "6d6d22bff9225c20646422c0cf220d0a503b505b5265645d5b3d305d47656e6572616c0d0a503b505b5265645d5b3c3e305d47656e6572616c0d0a503b502259"
    "6573223b3b22596573223b3b224e6f220d0a503b502254727565223b3b2254727565223b3b2246616c7365220d0a503b50224f6e223b3b224f6e223b3b224f66"
    "66220d0a503b505b24a2e62d325d5c20232c2323302e30305f293b3b5b5265645d5c285b24a2e62d325d5c20232c2323302e30305c290d0a503b46b5b8bff23b"
    "4d3232300d0a503b46b5b8bff23b4d3232300d0a503b46b5b8bff23b4d3232300d0a503b46b5b8bff23b4d3232300d0a503b45b5c8cfdf3b4d3232303b4c390d"
    "0a503b45b5c8cfdf204c696768743b4d3336303b4c35350d0a503b45b5c8cfdf3b4d3330303b53423b4c35350d0a503b45b5c8cfdf3b4d3236303b53423b4c35"
    "350d0a503b45b5c8cfdf3b4d3232303b53423b4c35350d0a503b45b5c8cfdf3b4d3232303b4c31380d0a503b45b5c8cfdf3b4d3232303b4c32310d0a503b45b5"
    "c8cfdf3b4d3232303b4c36310d0a503b45b5c8cfdf3b4d3232303b4c36330d0a503b45b5c8cfdf3b4d3232303b53423b4c36340d0a503b45b5c8cfdf3b4d3232"
    "303b53423b4c35330d0a503b45b5c8cfdf3b4d3232303b4c35330d0a503b45b5c8cfdf3b4d3232303b53423b4c31300d0a503b45b5c8cfdf3b4d3232303b4c31"
    "310d0a503b45b5c8cfdf3b4d3232303b53493b4c32340d0a503b45b5c8cfdf3b4d3232303b53423b4c390d0a503b45b5c8cfdf3b4d3232303b4c31300d0a503b"
    "45b5b8bff23b4d3232300d0a503b45b5b8bff23b4d3136300d0a503b45b5b8bff23b4d3230300d0a503b45b5b8bff23b4d3232303b4c390d0a503b45b5b8bff2"
    "3b4d3138300d0a463b50303b44473047383b4d3237300d0a423b593433303b58373b443020302034323920360d0a4f3b4c3b463b443b56303b4b34373b54303b"
    "4731303020302e3030310d0a463b5731203420360d0a463b573520352031310d0a463b50303b464730433b534d32343b43310d0a463b50303b464730433b534d"
    "32343b43320d0a463b50303b464730433b534d32343b43330d0a463b50303b464730433b534d32343b43340d0a463b50303b464730433b534d32343b43350d0a"
    "463b50303b464730433b534d32343b43360d0a4e4e3b4e5f46696c74657244617461626173653b45523143313a5234333043360d0a433b59313b58313b4b22b8"
    "b6b5f0220d0a433b58323b4b22b9da28312f3429220d0a463b5036383b464730433b58330d0a433b4b22312f3136220d0a433b58343b4b22c0a7c4a1220d0a43"
    "3b58353b4b22b5bfc0db220d0a433b58363b4b22b7b9baa728b1e6c0cc29220d0a433b59323b58313b4b22696e74220d0a433b58323b4b22696e74220d0a433b"
    "58333b4b22696e74220d0a433b58343b4b22696e74220d0a433b58353b4b22656e756d286e2c732c632c642c7229220d0a433b58363b4b22696e74220d0a";

constexpr std::uint8_t hex_nibble(char value) noexcept {
  return value >= '0' && value <= '9'
             ? static_cast<std::uint8_t>(value - '0')
             : static_cast<std::uint8_t>(value - 'a' + 10);
}

template <std::size_t Size>
constexpr auto decode_hex(const char (&hex)[Size]) noexcept {
  static_assert((Size - 1U) % 2U == 0U, "hex input must contain byte pairs");
  std::array<std::uint8_t, (Size - 1U) / 2U> bytes{};
  for (std::size_t index = 0U; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::uint8_t>(
        (hex_nibble(hex[index * 2U]) << 4U) |
        hex_nibble(hex[index * 2U + 1U]));
  }
  return bytes;
}

constexpr auto kCanonicalHeader = decode_hex(kCanonicalHeaderHex);
constexpr std::string_view kCanonicalB = "B;Y430;X7;D0 0 429 6\r\n";
constexpr std::string_view kCanonicalNn =
    "NN;N_FilterDatabase;ER1C1:R430C6\r\n";
constexpr std::string_view kFormatAnchor = "F;P0;FG0C;SM24;X7\r\n";
constexpr std::uint64_t kCanonicalBoundaryRow = 1'920U;

struct EncodedRow final {
  std::uint64_t row;
  char type;
  std::uint64_t value;
};

[[nodiscard]] const ChartRoleEvent* find_role_event(
    const ChartDraft& draft, std::uint64_t id) noexcept {
  for (const auto& event : draft.role_events()) {
    if (event.id.value == id) {
      return &event;
    }
  }
  return nullptr;
}

[[nodiscard]] const ChartShowtimeStartEvent* find_start_event(
    const ChartDraft& draft, std::uint64_t id) noexcept {
  for (const auto& event : draft.showtime_start_events()) {
    if (event.id.value == id) {
      return &event;
    }
  }
  return nullptr;
}

[[nodiscard]] const ChartShowtimeMoveEvent* find_move_event(
    const ChartDraft& draft, std::uint64_t id) noexcept {
  for (const auto& event : draft.showtime_move_events()) {
    if (event.id.value == id) {
      return &event;
    }
  }
  return nullptr;
}

[[nodiscard]] const ChartDrillGroup* find_group(
    const ChartDraft& draft, std::uint64_t id) noexcept {
  for (const auto& group : draft.drill_groups()) {
    if (group.id.value == id) {
      return &group;
    }
  }
  return nullptr;
}

[[nodiscard]] const ChartDanceMoveDrill* find_drill(
    const ChartDraft& draft, std::uint64_t id) noexcept {
  for (const auto& drill : draft.dance_move_drills()) {
    if (drill.id.value == id) {
      return &drill;
    }
  }
  return nullptr;
}

[[nodiscard]] bool append_row(std::vector<EncodedRow>& rows,
                              EncodedRow row) {
  if (!rows.empty() && rows.back().row >= row.row) {
    return false;
  }
  rows.push_back(row);
  return true;
}

[[nodiscard]] bool append_block(const ChartDraft& draft,
                                const std::vector<ChartEventId>& event_ids,
                                std::uint64_t c_value,
                                std::vector<EncodedRow>& rows) {
  for (const auto id : event_ids) {
    const auto* event = find_role_event(draft, id.value);
    if (event == nullptr) {
      return false;
    }
    switch (event->type) {
      case ChartRoleEventType::BlockStart:
        if (!append_row(rows, {event->row, 'c', c_value})) {
          return false;
        }
        break;
      case ChartRoleEventType::Note:
        if (!append_row(rows, {event->row, 'n', 0U})) {
          return false;
        }
        break;
      case ChartRoleEventType::MoveEnd:
        if (!append_row(rows, {event->row, 's', 0U})) {
          return false;
        }
        break;
    }
  }
  return true;
}

[[nodiscard]] bool segment_n_count(
    const ChartDraft& draft, const ChartDrillGroup& group,
    std::uint64_t& count) noexcept {
  count = 0U;
  for (const auto id : group.npc_event_ids) {
    const auto* event = find_role_event(draft, id.value);
    if (event == nullptr) {
      return false;
    }
    if (event->type == ChartRoleEventType::Note) {
      ++count;
    }
  }
  return count != 0U;
}

[[nodiscard]] bool append_drill_blocks(const ChartDraft& draft,
                                       const ChartDanceMoveDrill& drill,
                                       std::vector<EncodedRow>& rows) {
  if (drill.drill_group_ids.empty()) {
    return false;
  }
  const bool split = drill.drill_group_ids.size() > 1U;
  for (const auto group_id : drill.drill_group_ids) {
    const auto* group = find_group(draft, group_id.value);
    std::uint64_t segment_count = 0U;
    if (group == nullptr || !segment_n_count(draft, *group, segment_count)) {
      return false;
    }
    const std::uint64_t c_value =
        split ? segment_count * 100U + drill.move_n_count : drill.move_n_count;
    if (!append_block(draft, group->npc_event_ids, c_value, rows) ||
        !append_block(draft, group->player_event_ids, c_value, rows)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] SlkExportError encode_rows(
    const ChartDraft& draft, std::vector<EncodedRow>& rows) {
  std::size_t expected_count = draft.role_events().size();
  if (expected_count > std::numeric_limits<std::size_t>::max() -
                           draft.showtime_start_events().size()) {
    return SlkExportError::SizeOverflow;
  }
  expected_count += draft.showtime_start_events().size();
  if (expected_count > std::numeric_limits<std::size_t>::max() -
                           draft.showtime_move_events().size()) {
    return SlkExportError::SizeOverflow;
  }
  expected_count += draft.showtime_move_events().size();
  rows.reserve(expected_count);

  std::vector<const ChartDanceMoveDrill*> drills;
  drills.reserve(draft.dance_move_drills().size());
  for (const auto& drill : draft.dance_move_drills()) {
    drills.push_back(&drill);
  }
  std::sort(drills.begin(), drills.end(),
            [&draft](const ChartDanceMoveDrill* left,
                     const ChartDanceMoveDrill* right) {
              const auto left_row = dance_move_drill_first_row(draft, left->id);
              const auto right_row =
                  dance_move_drill_first_row(draft, right->id);
              const auto left_value =
                  left_row.has_value()
                      ? *left_row
                      : std::numeric_limits<std::uint64_t>::max();
              const auto right_value =
                  right_row.has_value()
                      ? *right_row
                      : std::numeric_limits<std::uint64_t>::max();
              return left_value != right_value
                         ? left_value < right_value
                         : left->id.value < right->id.value;
            });
  std::vector<bool> emitted(drills.size(), false);
  const auto is_recorded = [](const ChartShowtime& showtime,
                              ChartDanceMoveDrillId id) {
    return std::any_of(showtime.dance_move_drill_ids.begin(),
                       showtime.dance_move_drill_ids.end(),
                       [id](ChartDanceMoveDrillId recorded) {
                         return recorded.value == id.value;
                       });
  };

  const ChartShowtimeMoveEvent* last_show_move = nullptr;
  bool last_is_show_move = false;
  bool has_stage_begin = false;
  std::uint64_t stage_begin = 0U;
  for (const auto& showtime : draft.showtimes()) {
    const auto* start = find_start_event(draft, showtime.start_event_id.value);
    if (start == nullptr) {
      return SlkExportError::InvalidFinishedChart;
    }
    for (const auto drill_id : showtime.dance_move_drill_ids) {
      const auto* drill = find_drill(draft, drill_id.value);
      if (drill == nullptr || !append_drill_blocks(draft, *drill, rows)) {
        return SlkExportError::InvalidFinishedChart;
      }
      for (std::size_t index = 0U; index < drills.size(); ++index) {
        if (!emitted[index] && drills[index]->id.value == drill_id.value) {
          emitted[index] = true;
          break;
        }
      }
      last_is_show_move = false;
    }
    // A Showtime may reference fewer moves than its stage holds. The unshown
    // moves stay in the same ordinary stage, so they are written between the
    // recorded moves and this stage's SHOW-C.
    for (std::size_t index = 0U; index < drills.size(); ++index) {
      if (emitted[index]) {
        continue;
      }
      const auto first_row = dance_move_drill_first_row(draft, drills[index]->id);
      if (!first_row.has_value() || *first_row >= start->row ||
          (has_stage_begin && *first_row <= stage_begin) ||
          is_recorded(showtime, drills[index]->id)) {
        continue;
      }
      if (!append_drill_blocks(draft, *drills[index], rows)) {
        return SlkExportError::InvalidFinishedChart;
      }
      emitted[index] = true;
      last_is_show_move = false;
    }

    if (!append_row(rows, {start->row, 'c', 0U})) {
      return SlkExportError::InvalidFinishedChart;
    }
    last_is_show_move = false;
    std::uint64_t last_row = start->row;
    for (const auto event_id : showtime.show_move_event_ids) {
      const auto* event = find_move_event(draft, event_id.value);
      if (event == nullptr ||
          !append_row(rows, {event->row, 'd', event->show_n_count})) {
        return SlkExportError::InvalidFinishedChart;
      }
      if (event->row > last_row) {
        last_row = event->row;
      }
      last_show_move = event;
      last_is_show_move = true;
    }
    has_stage_begin = true;
    stage_begin = last_row;
  }

  for (std::size_t index = 0U; index < drills.size(); ++index) {
    if (emitted[index]) {
      continue;
    }
    if (!append_drill_blocks(draft, *drills[index], rows)) {
      return SlkExportError::InvalidFinishedChart;
    }
    emitted[index] = true;
    last_is_show_move = false;
  }

  if (rows.size() != expected_count ||
      (!draft.showtimes().empty() && last_show_move == nullptr)) {
    return SlkExportError::InvalidFinishedChart;
  }

  // The compatibility boundary only closes a final Showtime stage. When the
  // last stage is a Showtime-less ordinary stage, the file ends at its last
  // event and no boundary row is written.
  if (!last_is_show_move) {
    return SlkExportError::None;
  }
  if (last_show_move == nullptr) {
    return SlkExportError::InvalidFinishedChart;
  }
  const std::uint64_t range_start =
      last_show_move->row - last_show_move->row % 16U;
  const std::uint64_t duration =
      last_show_move->show_n_count <= 6U ? 16U : 32U;
  if (range_start > std::numeric_limits<std::uint64_t>::max() - duration) {
    return SlkExportError::InvalidFinishedChart;
  }
  const std::uint64_t boundary =
      std::max(kCanonicalBoundaryRow, range_start + duration);
  if (!append_row(rows, {boundary, 'r', 0U})) {
    return SlkExportError::InvalidFinishedChart;
  }
  return SlkExportError::None;
}

void append_text(std::vector<std::uint8_t>& output,
                 std::string_view text) {
  output.insert(output.end(), text.begin(), text.end());
}

void append_bytes(std::vector<std::uint8_t>& output,
                  const std::uint8_t* begin, const std::uint8_t* end) {
  output.insert(output.end(), begin, end);
}

void append_decimal(std::vector<std::uint8_t>& output,
                    std::uint64_t value) {
  std::array<char, 20U> buffer{};
  const auto conversion =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  append_text(output, {buffer.data(),
                       static_cast<std::size_t>(conversion.ptr - buffer.data())});
}

[[nodiscard]] bool append_header(std::uint64_t last_sheet_row,
                                 std::vector<std::uint8_t>& output) {
  const std::string_view header{
      reinterpret_cast<const char*>(kCanonicalHeader.data()),
      kCanonicalHeader.size()};
  const auto b_position = header.find(kCanonicalB);
  const auto nn_position = header.find(kCanonicalNn);
  if (b_position == std::string_view::npos ||
      nn_position == std::string_view::npos || nn_position <= b_position) {
    return false;
  }

  append_bytes(output, kCanonicalHeader.data(),
               kCanonicalHeader.data() + b_position);
  append_text(output, "B;Y");
  append_decimal(output, last_sheet_row);
  append_text(output, ";X7;D0 0 ");
  append_decimal(output, last_sheet_row - 1U);
  append_text(output, " 6\r\n");

  const auto between_begin = b_position + kCanonicalB.size();
  append_bytes(output, kCanonicalHeader.data() + between_begin,
               kCanonicalHeader.data() + nn_position);
  append_text(output, "NN;N_FilterDatabase;ER1C1:R");
  append_decimal(output, last_sheet_row);
  append_text(output, "C6\r\n");

  const auto after_nn = nn_position + kCanonicalNn.size();
  append_bytes(output, kCanonicalHeader.data() + after_nn,
               kCanonicalHeader.data() + kCanonicalHeader.size());
  return true;
}

void append_cell_prefix(std::vector<std::uint8_t>& output,
                        std::uint64_t sheet_row) {
  append_text(output, "C;Y");
  append_decimal(output, sheet_row);
  append_text(output, ";X1;K");
}

void append_encoded_row(const EncodedRow& row, std::uint64_t sheet_row,
                        std::vector<std::uint8_t>& output) {
  const auto position = decompose_slk_position(row.row);
  append_cell_prefix(output, sheet_row);
  append_decimal(output, position.position()->measure);
  append_text(output, "\r\nC;X2;K");
  append_decimal(output, position.position()->beat);
  append_text(output, "\r\nC;X3;K");
  append_decimal(output, position.position()->sixteenth);
  append_text(output, "\r\nC;X4;K");
  append_decimal(output, position.position()->total_sixteenth);
  append_text(output, "\r\nC;X5;K\"");
  output.push_back(static_cast<std::uint8_t>(row.type));
  append_text(output, "\"\r\nC;X6;K");
  append_decimal(output, row.value);
  append_text(output, "\r\n");
}

}  // namespace

SlkExportResult::SlkExportResult(std::vector<std::uint8_t> bytes) noexcept
    : storage_(std::move(bytes)) {}

SlkExportResult::SlkExportResult(SlkExportError error) noexcept
    : storage_(error) {}

bool SlkExportResult::has_value() const noexcept {
  return std::holds_alternative<std::vector<std::uint8_t>>(storage_);
}

const std::vector<std::uint8_t>* SlkExportResult::bytes() const noexcept {
  return std::get_if<std::vector<std::uint8_t>>(&storage_);
}

SlkExportError SlkExportResult::error() const noexcept {
  const auto* error = std::get_if<SlkExportError>(&storage_);
  return error == nullptr ? SlkExportError::None : *error;
}

std::vector<std::uint8_t> SlkExportResult::take_bytes() && {
  return std::move(std::get<std::vector<std::uint8_t>>(storage_));
}

SlkExportResult export_finished_chart_to_slk(
    const FinishedChart& chart) noexcept {
  try {
    std::vector<EncodedRow> rows;
    const auto encoding_error = encode_rows(chart.draft(), rows);
    if (encoding_error != SlkExportError::None) {
      return SlkExportResult{encoding_error};
    }
    if (rows.size() >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint64_t>::max() - 2U)) {
      return SlkExportResult{SlkExportError::SizeOverflow};
    }
    const auto last_sheet_row =
        static_cast<std::uint64_t>(rows.size()) + 2U;

    std::vector<std::uint8_t> output;
    if (!append_header(last_sheet_row, output)) {
      return SlkExportResult{SlkExportError::InvalidFinishedChart};
    }
    std::uint64_t sheet_row = 3U;
    for (const auto& row : rows) {
      append_encoded_row(row, sheet_row, output);
      if (sheet_row == 162U || sheet_row == 314U) {
        append_text(output, kFormatAnchor);
      }
      ++sheet_row;
    }
    append_text(output, "E\r\n");
    return SlkExportResult{std::move(output)};
  } catch (const std::length_error&) {
    return SlkExportResult{SlkExportError::SizeOverflow};
  } catch (const std::bad_alloc&) {
    return SlkExportResult{SlkExportError::AllocationFailure};
  }
}

}  // namespace otgy::core
