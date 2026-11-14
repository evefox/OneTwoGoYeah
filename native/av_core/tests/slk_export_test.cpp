// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_editor.hpp"
#include "av_core/chart/chart_validator.hpp"
#include "av_core/slk/slk_chart_draft_projection.hpp"
#include "av_core/slk/slk_export.hpp"
#include "av_core/slk/slk_import.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
  assert(input.is_open());
  return {std::istreambuf_iterator<char>{input},
          std::istreambuf_iterator<char>{}};
}

[[nodiscard]] otgy::core::FinishedChart finished_from_bytes(
    const std::vector<std::uint8_t>& bytes) {
  auto import = otgy::core::import_finished_slk(bytes.data(), bytes.size());
  assert(import);
  auto projection =
      otgy::core::project_finished_slk_to_chart_draft(*import.document());
  assert(projection);
  auto validation =
      otgy::core::validate_finished_chart(*projection.draft());
  assert(validation.is_finished());
  return std::move(validation).take_finished_chart();
}

[[nodiscard]] otgy::core::FinishedChart prefix_finished_from_file(
    const std::string& path) {
  const auto bytes = read_file(path);
  auto import = otgy::core::import_finished_slk(bytes.data(), bytes.size());
  assert(import);
  auto showtimes = import.document()->semantics().showtimes();
  assert(showtimes.front().show_d_rows.size() > 1U);
  showtimes.front().show_d_rows.resize(1U);
  otgy::core::FinishedSlkDocument prefix_document{
      otgy::core::SlkShowtimeAnalysis{
          std::move(showtimes), {},
          import.document()->semantics().boundary_row(),
          import.document()->semantics().boundary_source_row_index()}};
  auto projection =
      otgy::core::project_finished_slk_to_chart_draft(prefix_document);
  assert(projection);
  auto validation =
      otgy::core::validate_finished_chart(*projection.draft());
  assert(validation.is_finished());
  return std::move(validation).take_finished_chart();
}

[[nodiscard]] otgy::core::FinishedChart make_finished_chart(
    const std::vector<std::pair<std::uint64_t, std::uint64_t>>& drill_shapes,
    std::uint64_t first_row = 1U) {
  using namespace otgy::core;

  std::vector<ChartRoleEvent> role_events;
  std::vector<ChartShowtimeStartEvent> start_events;
  std::vector<ChartShowtimeMoveEvent> move_events;
  std::vector<ChartDrillGroup> groups;
  std::vector<ChartDanceMoveDrill> drills;
  std::vector<ChartShowtime> showtimes;

  std::uint64_t event_id = 1U;
  std::uint64_t group_id = 10'000U;
  std::uint64_t drill_id = 20'000U;
  std::uint64_t row = first_row;
  ChartShowtime showtime{{30'000U}, {0U}, {}, {}};

  const auto add_event = [&](ChartRole role, ChartRoleEventType type,
                             ChartDrillGroupId current_group,
                             ChartDanceMoveDrillId current_drill,
                             std::vector<ChartEventId>& relation) {
    const ChartEventId id{event_id++};
    role_events.push_back(
        {id, row++, role, type, current_group, current_drill});
    relation.push_back(id);
  };

  for (const auto [move_n_count, group_count] : drill_shapes) {
    assert(move_n_count >= 1U && move_n_count <= 11U);
    assert(group_count >= 1U && group_count <= move_n_count);
    const ChartDanceMoveDrillId current_drill{drill_id++};
    ChartDanceMoveDrill drill{current_drill, move_n_count, {}};
    std::uint64_t remaining_notes = move_n_count;
    for (std::uint64_t group_index = 0U; group_index < group_count;
         ++group_index) {
      const bool final_group = group_index + 1U == group_count;
      const std::uint64_t segment_notes =
          final_group ? remaining_notes : 1U;
      remaining_notes -= segment_notes;
      const ChartDrillGroupId current_group{group_id++};
      ChartDrillGroup group{current_group, current_drill, {}, {}};
      add_event(ChartRole::Npc, ChartRoleEventType::BlockStart, current_group,
                current_drill, group.npc_event_ids);
      for (std::uint64_t index = 0U; index < segment_notes; ++index) {
        add_event(ChartRole::Npc, ChartRoleEventType::Note, current_group,
                  current_drill, group.npc_event_ids);
      }
      if (final_group) {
        add_event(ChartRole::Npc, ChartRoleEventType::MoveEnd, current_group,
                  current_drill, group.npc_event_ids);
      }
      add_event(ChartRole::Player, ChartRoleEventType::BlockStart,
                current_group, current_drill, group.player_event_ids);
      for (std::uint64_t index = 0U; index < segment_notes; ++index) {
        add_event(ChartRole::Player, ChartRoleEventType::Note, current_group,
                  current_drill, group.player_event_ids);
      }
      if (final_group) {
        add_event(ChartRole::Player, ChartRoleEventType::MoveEnd, current_group,
                  current_drill, group.player_event_ids);
      }
      drill.drill_group_ids.push_back(current_group);
      groups.push_back(std::move(group));
    }
    assert(remaining_notes == 0U);
    showtime.dance_move_drill_ids.push_back(current_drill);
    drills.push_back(std::move(drill));
  }

  const ChartEventId start_id{event_id++};
  start_events.push_back({start_id, row++, showtime.id});
  showtime.start_event_id = start_id;
  for (const auto& drill : drills) {
    const ChartEventId move_id{event_id++};
    move_events.push_back(
        {move_id, row++, showtime.id, drill.id, drill.move_n_count});
    showtime.show_move_event_ids.push_back(move_id);
  }
  showtimes.push_back(std::move(showtime));

  const ChartDraft draft{std::move(role_events), std::move(start_events),
                         std::move(move_events), std::move(groups),
                         std::move(drills), std::move(showtimes)};
  auto validation = validate_finished_chart(draft);
  assert(validation.is_finished());
  return std::move(validation).take_finished_chart();
}

[[nodiscard]] std::size_t count_occurrences(std::string_view text,
                                            std::string_view needle) {
  std::size_t count = 0U;
  std::size_t position = 0U;
  while ((position = text.find(needle, position)) != std::string_view::npos) {
    ++count;
    position += needle.size();
  }
  return count;
}

void assert_anchor_follows_sheet_row(std::string_view text,
                                     std::uint64_t sheet_row) {
  constexpr std::string_view anchor = "F;P0;FG0C;SM24;X7\r\n";
  const auto row_begin =
      text.find("C;Y" + std::to_string(sheet_row) + ";X1;K");
  assert(row_begin != std::string_view::npos);
  const auto value_cell = text.find("\r\nC;X6;K", row_begin);
  assert(value_cell != std::string_view::npos);
  const auto row_end = text.find("\r\n", value_cell + 2U);
  assert(row_end != std::string_view::npos);
  assert(text.substr(row_end + 2U, anchor.size()) == anchor);
}

[[nodiscard]] std::vector<std::uint8_t> normalized_fixed_header(
    const std::vector<std::uint8_t>& bytes) {
  const std::string_view text{reinterpret_cast<const char*>(bytes.data()),
                              bytes.size()};
  const auto data_begin = text.find("C;Y3;X1;K");
  assert(data_begin != std::string_view::npos);
  std::vector<std::uint8_t> normalized;
  std::size_t line_begin = 0U;
  while (line_begin < data_begin) {
    const auto line_end = text.find("\r\n", line_begin);
    assert(line_end != std::string_view::npos);
    const auto line = text.substr(line_begin, line_end - line_begin);
    if (line.rfind("B;", 0U) != 0U && line.rfind("NN;", 0U) != 0U) {
      normalized.insert(normalized.end(), bytes.begin() + line_begin,
                        bytes.begin() + line_end + 2U);
    }
    line_begin = line_end + 2U;
  }
  return normalized;
}

void assert_same_draft_semantics(const otgy::core::ChartDraft& left,
                                 const otgy::core::ChartDraft& right) {
  assert(left.role_events().size() == right.role_events().size());
  for (std::size_t index = 0U; index < left.role_events().size(); ++index) {
    const auto& a = left.role_events()[index];
    const auto& b = right.role_events()[index];
    assert(a.id.value == b.id.value && a.row == b.row && a.role == b.role &&
           a.type == b.type &&
           a.drill_group_id.value == b.drill_group_id.value &&
           a.dance_move_drill_id.value == b.dance_move_drill_id.value);
  }
  assert(left.showtime_start_events().size() ==
         right.showtime_start_events().size());
  for (std::size_t index = 0U; index < left.showtime_start_events().size();
       ++index) {
    const auto& a = left.showtime_start_events()[index];
    const auto& b = right.showtime_start_events()[index];
    assert(a.id.value == b.id.value && a.row == b.row &&
           a.showtime_id.value == b.showtime_id.value);
  }
  assert(left.showtime_move_events().size() ==
         right.showtime_move_events().size());
  for (std::size_t index = 0U; index < left.showtime_move_events().size();
       ++index) {
    const auto& a = left.showtime_move_events()[index];
    const auto& b = right.showtime_move_events()[index];
    assert(a.id.value == b.id.value && a.row == b.row &&
           a.showtime_id.value == b.showtime_id.value &&
           a.dance_move_drill_id.value == b.dance_move_drill_id.value &&
           a.show_n_count == b.show_n_count);
  }
  assert(left.drill_groups().size() == right.drill_groups().size());
  for (std::size_t index = 0U; index < left.drill_groups().size(); ++index) {
    const auto& a = left.drill_groups()[index];
    const auto& b = right.drill_groups()[index];
    assert(a.id.value == b.id.value &&
           a.dance_move_drill_id.value == b.dance_move_drill_id.value);
    assert(a.npc_event_ids.size() == b.npc_event_ids.size());
    assert(a.player_event_ids.size() == b.player_event_ids.size());
    for (std::size_t item = 0U; item < a.npc_event_ids.size(); ++item) {
      assert(a.npc_event_ids[item].value == b.npc_event_ids[item].value);
    }
    for (std::size_t item = 0U; item < a.player_event_ids.size(); ++item) {
      assert(a.player_event_ids[item].value == b.player_event_ids[item].value);
    }
  }
  assert(left.dance_move_drills().size() ==
         right.dance_move_drills().size());
  for (std::size_t index = 0U; index < left.dance_move_drills().size();
       ++index) {
    const auto& a = left.dance_move_drills()[index];
    const auto& b = right.dance_move_drills()[index];
    assert(a.id.value == b.id.value && a.move_n_count == b.move_n_count &&
           a.drill_group_ids.size() == b.drill_group_ids.size());
    for (std::size_t item = 0U; item < a.drill_group_ids.size(); ++item) {
      assert(a.drill_group_ids[item].value == b.drill_group_ids[item].value);
    }
  }
  assert(left.showtimes().size() == right.showtimes().size());
  for (std::size_t index = 0U; index < left.showtimes().size(); ++index) {
    const auto& a = left.showtimes()[index];
    const auto& b = right.showtimes()[index];
    assert(a.id.value == b.id.value &&
           a.start_event_id.value == b.start_event_id.value &&
           a.dance_move_drill_ids.size() ==
               b.dance_move_drill_ids.size() &&
           a.show_move_event_ids.size() == b.show_move_event_ids.size());
    for (std::size_t item = 0U; item < a.dance_move_drill_ids.size(); ++item) {
      assert(a.dance_move_drill_ids[item].value ==
             b.dance_move_drill_ids[item].value);
      assert(a.show_move_event_ids[item].value ==
             b.show_move_event_ids[item].value);
    }
  }
}

void assert_crlf_container(const std::vector<std::uint8_t>& bytes) {
  assert(bytes.size() >= 3U);
  assert(bytes[0U] != 0xEFU || bytes[1U] != 0xBBU || bytes[2U] != 0xBFU);
  for (std::size_t index = 0U; index < bytes.size(); ++index) {
    if (bytes[index] == static_cast<std::uint8_t>('\n')) {
      assert(index > 0U);
      assert(bytes[index - 1U] == static_cast<std::uint8_t>('\r'));
    }
    if (bytes[index] == static_cast<std::uint8_t>('\r')) {
      assert(index + 1U < bytes.size());
      assert(bytes[index + 1U] == static_cast<std::uint8_t>('\n'));
    }
  }
  const std::string_view text{reinterpret_cast<const char*>(bytes.data()),
                              bytes.size()};
  assert(text.substr(text.size() - 3U) == "E\r\n");
}

void test_canonical_finished_chart_round_trips_byte_for_byte() {
  const auto canonical =
      read_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  const auto chart = finished_from_bytes(canonical);
  auto exported = otgy::core::export_finished_chart_to_slk(chart);
  assert(exported);
  assert(exported.error() == otgy::core::SlkExportError::None);
  assert(exported.bytes() != nullptr);
  assert(*exported.bytes() == canonical);

  const auto copy = *exported.bytes();
  const auto moved = std::move(exported).take_bytes();
  assert(copy == moved);
  assert(copy.data() != moved.data());
}

void test_compatibility_chart_uses_canonical_shell_and_reimports() {
  const auto compatibility =
      read_file(std::string{OTGY_EXAMPLE_DIR} + "/谎话成品.slk");
  const auto canonical =
      read_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  const auto chart = finished_from_bytes(compatibility);
  const auto exported = otgy::core::export_finished_chart_to_slk(chart);
  assert(exported);
  assert(*exported.bytes() != compatibility);
  assert(exported.bytes()->size() > 13U);
  assert(std::equal(canonical.begin(), canonical.begin() + 13,
                    exported.bytes()->begin()));
  assert(normalized_fixed_header(*exported.bytes()) ==
         normalized_fixed_header(canonical));
  assert(normalized_fixed_header(*exported.bytes()) !=
         normalized_fixed_header(compatibility));

  const auto reimport = otgy::core::import_finished_slk(
      exported.bytes()->data(), exported.bytes()->size());
  assert(reimport);
  assert(reimport.document()->semantics().showtimes().size() == 2U);
  assert(reimport.document()->semantics().showtimes()[0U].show_d_rows.size() ==
         8U);
  assert(reimport.document()->semantics().showtimes()[1U].show_d_rows.size() ==
         9U);
  assert(reimport.document()->semantics().boundary_row()->position
             .total_sixteenth == 1'920U);
  const auto projection =
      otgy::core::project_finished_slk_to_chart_draft(*reimport.document());
  assert(projection);
  assert_same_draft_semantics(chart.draft(), *projection.draft());
}

void test_prefix_showtime_exports_and_reimports() {
  const auto chart = prefix_finished_from_file(
      std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  const auto exported = otgy::core::export_finished_chart_to_slk(chart);
  assert(exported);
  const auto& draft = chart.draft();
  assert(draft.showtimes().front().dance_move_drill_ids.size() == 6U);
  assert(draft.showtimes().front().show_move_event_ids.size() == 1U);

  const auto reimport = otgy::core::import_finished_slk(
      exported.bytes()->data(), exported.bytes()->size());
  assert(reimport);
  assert(reimport.document()->semantics().showtimes().front().stage.drills()
             .size() == 6U);
  assert(reimport.document()->semantics().showtimes().front().show_d_rows
             .size() == 1U);
}

void test_direct_split_values_and_boundary_extension_reimport() {
  const auto chart = make_finished_chart({{3U, 1U}, {3U, 2U}});
  const auto exported = otgy::core::export_finished_chart_to_slk(chart);
  assert(exported);
  const auto parsed = otgy::core::parse_slk_records(
      exported.bytes()->data(), exported.bytes()->size());
  assert(parsed);

  std::vector<std::uint64_t> c_values;
  for (const auto& row : parsed.records()->rows()) {
    if (row.type == otgy::core::SlkRowType::C && row.value != 0U) {
      c_values.push_back(row.value);
    }
  }
  assert((c_values ==
          std::vector<std::uint64_t>{3U, 3U, 103U, 103U, 203U, 203U}));
  const auto reimport = otgy::core::import_finished_slk(
      exported.bytes()->data(), exported.bytes()->size());
  assert(reimport);
  assert(reimport.document()->semantics().showtimes().size() == 1U);
  assert(reimport.document()->semantics().showtimes()[0U].stage.drills().size() ==
         2U);

  const auto long_chart = make_finished_chart({{1U, 1U}}, 2'000U);
  const auto long_export =
      otgy::core::export_finished_chart_to_slk(long_chart);
  assert(long_export);
  const auto long_parse = otgy::core::parse_slk_records(
      long_export.bytes()->data(), long_export.bytes()->size());
  assert(long_parse);
  assert(long_parse.records()->rows().back().position.total_sixteenth ==
         2'016U);
}

void assert_template_range_case(
    const std::vector<std::pair<std::uint64_t, std::uint64_t>>& shapes,
    std::uint64_t expected_last_sheet_row,
    std::size_t expected_anchor_count) {
  const auto chart = make_finished_chart(shapes);
  const auto exported = otgy::core::export_finished_chart_to_slk(chart);
  assert(exported);
  assert_crlf_container(*exported.bytes());
  const std::string_view text{
      reinterpret_cast<const char*>(exported.bytes()->data()),
      exported.bytes()->size()};
  const auto last = std::to_string(expected_last_sheet_row);
  assert(text.find("B;Y" + last + ";X7;D0 0 " +
                   std::to_string(expected_last_sheet_row - 1U) +
                   " 6\r\n") != std::string_view::npos);
  assert(text.find("NN;N_FilterDatabase;ER1C1:R" + last + "C6\r\n") !=
         std::string_view::npos);
  assert(count_occurrences(text, "F;P0;FG0C;SM24;X7\r\n") ==
         expected_anchor_count);
  if (expected_last_sheet_row >= 162U) {
    assert_anchor_follows_sheet_row(text, 162U);
  }
  if (expected_last_sheet_row >= 314U) {
    assert_anchor_follows_sheet_row(text, 314U);
  }

  const auto parsed = otgy::core::parse_slk_records(
      exported.bytes()->data(), exported.bytes()->size());
  assert(parsed);
  assert(parsed.records()->rows().size() == expected_last_sheet_row - 2U);
  assert(parsed.records()->rows().back().sheet_row == expected_last_sheet_row);
  assert(parsed.records()->rows().back().type == otgy::core::SlkRowType::R);
}

void test_range_fields_and_format_anchor_boundaries() {
  assert_template_range_case(
      {{7U, 7U}, {7U, 7U}, {7U, 7U}, {7U, 7U}, {8U, 7U}}, 161U,
      0U);
  assert_template_range_case(
      {{9U, 9U}, {9U, 9U}, {9U, 9U}, {10U, 9U}}, 162U, 1U);
  assert_template_range_case(
      {{10U, 10U}, {10U, 10U}, {10U, 10U}, {11U, 10U},
       {11U, 10U}, {11U, 10U}, {11U, 10U}},
      313U, 1U);
  assert_template_range_case(
      {{9U, 9U}, {9U, 9U}, {9U, 9U}, {9U, 9U},
       {9U, 9U}, {9U, 9U}, {9U, 9U}, {9U, 8U}},
      314U, 2U);
}

void add_export_dance_move(otgy::core::ChartEditor& editor,
                           std::uint64_t first_row) {
  assert(editor.click(otgy::core::ChartColumn::Npc, first_row));
  assert(editor.click(otgy::core::ChartColumn::Npc, first_row + 2U));
  assert(editor.click(otgy::core::ChartColumn::Npc, first_row + 4U));
  assert(editor.click(otgy::core::ChartColumn::Npc, first_row + 6U));
  assert(editor.click(otgy::core::ChartColumn::Npc, first_row + 6U));
  assert(editor.click(otgy::core::ChartColumn::Player, first_row + 8U));
  assert(editor.click(otgy::core::ChartColumn::Player, first_row + 10U));
}

void test_showtime_less_drills_export_and_round_trip() {
  otgy::core::ChartEditor mixed{
      otgy::core::ChartDraft{{}, {}, {}, {}, {}, {}}};
  add_export_dance_move(mixed, 0U);
  assert(mixed.click(otgy::core::ChartColumn::Showtime, 16U));
  add_export_dance_move(mixed, 40U);
  assert(mixed.draft().showtimes().size() == 1U);
  assert(mixed.draft().dance_move_drills().size() == 2U);

  auto mixed_validation = otgy::core::validate_finished_chart(mixed.draft());
  assert(mixed_validation.is_finished());
  const auto mixed_export = otgy::core::export_finished_chart_to_slk(
      *mixed_validation.finished_chart());
  assert(mixed_export);
  const auto mixed_parse = otgy::core::parse_slk_records(
      mixed_export.bytes()->data(), mixed_export.bytes()->size());
  assert(mixed_parse);
  // The trailing Showtime-less stage suppresses the compatibility boundary.
  for (const auto& row : mixed_parse.records()->rows()) {
    assert(row.type != otgy::core::SlkRowType::R);
  }
  const auto mixed_reimport = otgy::core::import_finished_slk(
      mixed_export.bytes()->data(), mixed_export.bytes()->size());
  assert(mixed_reimport);
  assert(mixed_reimport.document()->semantics().showtimes().size() == 1U);
  assert(mixed_reimport.document()->semantics().trailing_drills().size() == 1U);
  const auto mixed_projection = otgy::core::project_finished_slk_to_chart_draft(
      *mixed_reimport.document());
  assert(mixed_projection);
  auto mixed_round_validation =
      otgy::core::validate_finished_chart(*mixed_projection.draft());
  assert(mixed_round_validation.is_finished());
  const auto mixed_round_export = otgy::core::export_finished_chart_to_slk(
      *mixed_round_validation.finished_chart());
  assert(mixed_round_export);
  assert(*mixed_round_export.bytes() == *mixed_export.bytes());

  otgy::core::ChartEditor plain{
      otgy::core::ChartDraft{{}, {}, {}, {}, {}, {}}};
  add_export_dance_move(plain, 0U);
  add_export_dance_move(plain, 40U);
  assert(plain.draft().showtimes().empty());
  auto plain_validation = otgy::core::validate_finished_chart(plain.draft());
  assert(plain_validation.is_finished());
  const auto plain_export = otgy::core::export_finished_chart_to_slk(
      *plain_validation.finished_chart());
  assert(plain_export);
  const auto plain_parse = otgy::core::parse_slk_records(
      plain_export.bytes()->data(), plain_export.bytes()->size());
  assert(plain_parse);
  for (const auto& row : plain_parse.records()->rows()) {
    assert(row.type != otgy::core::SlkRowType::R);
  }
  const auto plain_reimport = otgy::core::import_finished_slk(
      plain_export.bytes()->data(), plain_export.bytes()->size());
  assert(plain_reimport);
  assert(plain_reimport.document()->semantics().showtimes().empty());
  assert(plain_reimport.document()->semantics().trailing_drills().size() == 2U);
  const auto plain_projection = otgy::core::project_finished_slk_to_chart_draft(
      *plain_reimport.document());
  assert(plain_projection);
  auto plain_round_validation =
      otgy::core::validate_finished_chart(*plain_projection.draft());
  assert(plain_round_validation.is_finished());
  const auto plain_round_export = otgy::core::export_finished_chart_to_slk(
      *plain_round_validation.finished_chart());
  assert(plain_round_export);
  assert(*plain_round_export.bytes() == *plain_export.bytes());

  otgy::core::ChartEditor long_plain{
      otgy::core::ChartDraft{{}, {}, {}, {}, {}, {}}};
  add_export_dance_move(long_plain, 2'000U);
  auto long_validation = otgy::core::validate_finished_chart(long_plain.draft());
  assert(long_validation.is_finished());
  const auto long_export = otgy::core::export_finished_chart_to_slk(
      *long_validation.finished_chart());
  assert(long_export);
  const auto long_parse = otgy::core::parse_slk_records(
      long_export.bytes()->data(), long_export.bytes()->size());
  assert(long_parse);
  // The last stage has no Showtime, so no compatibility boundary is written.
  for (const auto& row : long_parse.records()->rows()) {
    assert(row.type != otgy::core::SlkRowType::R);
  }
  assert(long_parse.records()->rows().back().type ==
         otgy::core::SlkRowType::S);
  assert(long_parse.records()->rows().back().position.total_sixteenth ==
         2'014U);
}

/// The user can delete trailing SHOW-D entries. The dropped moves stay in the
/// same ordinary stage and are written before that stage's SHOW-C, so a strict
/// import recovers the same shown count and the same move order.
void test_reduced_showtime_keeps_stage_moves_and_round_trips() {
  const auto canonical = read_file(std::string{OTGY_EXAMPLE_DIR} + "/z3116_lv5.slk");
  const auto finished = finished_from_bytes(canonical);
  const auto& draft = finished.draft();
  assert(draft.showtimes().front().show_move_event_ids.size() > 1U);

  auto showtimes = draft.showtimes();
  auto move_events = draft.showtime_move_events();
  const auto original_shown = showtimes.front().show_move_event_ids.size();
  const auto original_recorded = showtimes.front().dance_move_drill_ids.size();
  const auto dropped = showtimes.front().show_move_event_ids.back();
  showtimes.front().show_move_event_ids.pop_back();
  showtimes.front().dance_move_drill_ids.pop_back();
  move_events.erase(
      std::remove_if(move_events.begin(), move_events.end(),
                     [&dropped](const auto& event) {
                       return event.id.value == dropped.value;
                     }),
      move_events.end());
  const otgy::core::ChartDraft reduced{
      draft.role_events(), draft.showtime_start_events(), std::move(move_events),
      draft.drill_groups(), draft.dance_move_drills(), std::move(showtimes)};
  const auto validation = otgy::core::validate_finished_chart(reduced);
  assert(validation.is_finished());

  const auto exported = otgy::core::export_finished_chart_to_slk(
      *validation.finished_chart());
  assert(exported);
  const auto reimport = otgy::core::import_finished_slk(
      exported.bytes()->data(), exported.bytes()->size());
  assert(reimport);
  const auto& semantics = reimport.document()->semantics();
  assert(semantics.showtimes().front().stage.drills().size() ==
         original_recorded);
  assert(semantics.showtimes().front().show_d_rows.size() ==
         original_shown - 1U);

  const auto reexport = otgy::core::export_finished_chart_to_slk(
      finished_from_bytes(*exported.bytes()));
  assert(reexport);
  assert(*reexport.bytes() == *exported.bytes());
}

/// A move starting inside the previous Showtime's display range but after its
/// last SHOW-D event row belongs to the next stage. The canonical file keeps
/// that row order, and a strict import maps the d entries to the next stage's
/// leading moves.
void test_showtime_stage_after_last_show_d_row_round_trips() {
  using namespace otgy::core;
  std::vector<ChartRoleEvent> events;
  std::vector<ChartShowtimeStartEvent> start_events;
  std::vector<ChartShowtimeMoveEvent> move_events;
  std::vector<ChartDrillGroup> groups;
  std::vector<ChartDanceMoveDrill> drills;
  std::vector<ChartShowtime> showtimes;
  std::uint64_t event_id = 1U;
  std::uint64_t group_id = 1U;
  std::uint64_t drill_id = 1U;
  const auto add_move = [&](std::uint64_t npc_c, std::uint64_t npc_n,
                            std::uint64_t npc_s, std::uint64_t player_c,
                            std::uint64_t player_n, std::uint64_t player_s) {
    const ChartDrillGroupId gid{group_id++};
    const ChartDanceMoveDrillId did{drill_id++};
    ChartDrillGroup group{gid, did, {}, {}};
    const auto add = [&](ChartRole role, ChartRoleEventType type,
                         std::uint64_t row, std::vector<ChartEventId>& rel) {
      const ChartEventId id{event_id++};
      events.push_back({id, row, role, type, gid, did});
      rel.push_back(id);
    };
    add(ChartRole::Npc, ChartRoleEventType::BlockStart, npc_c,
        group.npc_event_ids);
    add(ChartRole::Npc, ChartRoleEventType::Note, npc_n, group.npc_event_ids);
    add(ChartRole::Npc, ChartRoleEventType::MoveEnd, npc_s,
        group.npc_event_ids);
    add(ChartRole::Player, ChartRoleEventType::BlockStart, player_c,
        group.player_event_ids);
    add(ChartRole::Player, ChartRoleEventType::Note, player_n,
        group.player_event_ids);
    add(ChartRole::Player, ChartRoleEventType::MoveEnd, player_s,
        group.player_event_ids);
    groups.push_back(std::move(group));
    drills.push_back({did, 1U, {gid}});
    return did;
  };

  const auto move_a = add_move(0U, 2U, 4U, 6U, 8U, 10U);
  // The first N is inside the previous SHOW-D's display range [32, 48), but
  // after its event row.
  const auto move_b = add_move(40U, 42U, 44U, 46U, 48U, 50U);
  const auto move_c = add_move(64U, 66U, 68U, 70U, 72U, 74U);

  const ChartEventId start_1{100U};
  const ChartEventId d_1{101U};
  start_events.push_back({start_1, 16U, {110U}});
  move_events.push_back({d_1, 32U, {110U}, move_a, 1U});
  showtimes.push_back({{110U}, start_1, {move_a}, {d_1}});

  const ChartEventId start_2{200U};
  const ChartEventId d_2{201U};
  const ChartEventId d_3{202U};
  start_events.push_back({start_2, 96U, {210U}});
  move_events.push_back({d_2, 97U, {210U}, move_b, 1U});
  move_events.push_back({d_3, 112U, {210U}, move_c, 1U});
  showtimes.push_back({{210U}, start_2, {move_b, move_c}, {d_2, d_3}});

  const ChartDraft draft{std::move(events), std::move(start_events),
                         std::move(move_events), std::move(groups),
                         std::move(drills), std::move(showtimes)};
  const auto validation = validate_finished_chart(draft);
  assert(validation.is_finished());
  const auto exported = export_finished_chart_to_slk(
      *validation.finished_chart());
  assert(exported);
  const auto reimport = import_finished_slk(
      exported.bytes()->data(), exported.bytes()->size());
  assert(reimport);
  const auto& semantics = reimport.document()->semantics();
  assert(semantics.showtimes().size() == 2U);
  assert(semantics.showtimes()[0U].stage.drills().size() == 1U);
  assert(semantics.showtimes()[0U].show_d_rows.size() == 1U);
  assert(semantics.showtimes()[1U].stage.drills().size() == 2U);
  assert(semantics.showtimes()[1U].show_d_rows.size() == 2U);
  const auto reexport = export_finished_chart_to_slk(
      finished_from_bytes(*exported.bytes()));
  assert(reexport);
  assert(*reexport.bytes() == *exported.bytes());
}

void test_export_error_messages() {
  for (unsigned int raw = 0U;
       raw <= static_cast<unsigned int>(
                  otgy::core::SlkExportError::AllocationFailure);
       ++raw) {
    const auto message = otgy::core::slk_export_error_message(
        static_cast<otgy::core::SlkExportError>(raw));
    if (raw == 0U) {
      assert(message.empty());
    } else {
      assert(!message.empty());
    }
  }
  assert(!otgy::core::slk_export_error_message(
              static_cast<otgy::core::SlkExportError>(255))
              .empty());
}

}  // namespace

int main() {
  test_canonical_finished_chart_round_trips_byte_for_byte();
  test_compatibility_chart_uses_canonical_shell_and_reimports();
  test_prefix_showtime_exports_and_reimports();
  test_direct_split_values_and_boundary_extension_reimport();
  test_range_fields_and_format_anchor_boundaries();
  test_showtime_less_drills_export_and_round_trip();
  test_reduced_showtime_keeps_stage_moves_and_round_trips();
  test_showtime_stage_after_last_show_d_row_round_trips();
  test_export_error_messages();
  return 0;
}
