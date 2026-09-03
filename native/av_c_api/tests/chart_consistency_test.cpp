// SPDX-License-Identifier: GPL-3.0-or-later

#include "otgy/av_c_api.h"

#include "av_core/chart/chart_editor.hpp"
#include "av_core/chart/chart_projection.hpp"
#include "av_core/chart/chart_validator.hpp"
#include "av_core/coordination/operation_gate.hpp"
#include "av_core/playback/playback_position.hpp"
#include "av_core/project/project_serialization.hpp"
#include "av_core/slk/slk_chart_draft_projection.hpp"
#include "av_core/slk/slk_export.hpp"
#include "av_core/slk/slk_import.hpp"
#include "av_core/slk/slk_position.hpp"
#include "av_core/timing/timeline_navigation.hpp"
#include "av_core/timing/timeline_range.hpp"
#include "av_core/timing/timing_conversion.hpp"
#include "av_core/timing/timing_grid.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void expect(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

bool same_double(double left, double right) {
  return std::memcmp(&left, &right, sizeof(double)) == 0;
}

std::vector<std::uint8_t> read_file(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  expect(stream.good(), "unable to open SLK fixture");
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

otgy_byte_view view(const std::vector<std::uint8_t>& bytes) {
  return {bytes.empty() ? nullptr : bytes.data(),
          static_cast<std::uint64_t>(bytes.size())};
}

template <typename T, void (*Destroy)(T*)>
using Handle = std::unique_ptr<T, decltype(Destroy)>;

template <typename T, void (*Destroy)(T*)>
Handle<T, Destroy> handle(T* value) {
  return Handle<T, Destroy>(value, Destroy);
}

void expect_error(const otgy_error* error, otgy_error_domain domain,
                  std::uint32_t code, std::string_view message) {
  expect(error != nullptr, "missing C ABI error");
  expect(otgy_error_get_domain(error) == domain,
         "C ABI error domain differs");
  expect(otgy_error_get_code(error) == code, "C ABI error code differs");
  const auto actual = otgy_error_get_message(error);
  expect(std::string_view(actual.data, static_cast<std::size_t>(actual.length)) ==
             message,
         "C ABI error message differs");
}

otgy::core::ChartDraft empty_chart() {
  return {{}, {}, {}, {}, {}, {}};
}

void compare_edit(const otgy::core::ChartEditResult& direct,
                  const otgy_chart_edit_result& abi) {
  expect(abi.code == static_cast<std::uint32_t>(direct.code) &&
             abi.changed == static_cast<std::uint8_t>(direct.changed) &&
             abi.primary_event_id == direct.primary_event_id.value,
         "chart edit result differs");
}

void compare_markers(const std::vector<otgy::core::ChartMarker>& direct,
                     const otgy_chart_markers* abi) {
  expect(otgy_chart_markers_get_count(abi) == direct.size(),
         "chart marker count differs");
  const auto* values = otgy_chart_markers_get_data(abi);
  for (std::size_t index = 0U; index < direct.size(); ++index) {
    expect(values[index].id == direct[index].id.value &&
               values[index].row == direct[index].row &&
               values[index].column ==
                   static_cast<std::uint32_t>(direct[index].column) &&
               values[index].type ==
                   static_cast<std::uint32_t>(direct[index].type) &&
               values[index].show_n_count == direct[index].show_n_count,
           "chart marker differs");
  }
}

void compare_showtime_moves(
    const std::vector<otgy::core::ShowtimeMoveProjection>& direct,
    const otgy_showtime_moves* abi) {
  expect(otgy_showtime_moves_get_count(abi) == direct.size(),
         "Showtime move count differs");
  const auto* values = otgy_showtime_moves_get_data(abi);
  for (std::size_t index = 0U; index < direct.size(); ++index) {
    expect(values[index].event_id == direct[index].event_id.value &&
               values[index].row == direct[index].row &&
               values[index].show_n_count == direct[index].show_n_count &&
               values[index].range_start == direct[index].range_start &&
               values[index].range_end == direct[index].range_end &&
               values[index].is_first_in_sequence ==
                   static_cast<std::uint8_t>(
                       direct[index].is_first_in_sequence) &&
               values[index].is_last_in_sequence ==
                   static_cast<std::uint8_t>(direct[index].is_last_in_sequence),
           "Showtime move projection differs");
  }
}

void compare_semantics(const otgy::core::ChartDraft& direct,
                       const otgy_chart_semantics* abi) {
  expect(otgy_chart_semantics_get_role_events_count(abi) ==
             direct.role_events().size(),
         "role event count differs");
  const auto* role_events = otgy_chart_semantics_get_role_events(abi);
  for (std::size_t index = 0U; index < direct.role_events().size(); ++index) {
    const auto& value = direct.role_events()[index];
    expect(role_events[index].id == value.id.value &&
               role_events[index].row == value.row &&
               role_events[index].role ==
                   static_cast<std::uint32_t>(value.role) &&
               role_events[index].type ==
                   static_cast<std::uint32_t>(value.type) &&
               role_events[index].drill_group_id ==
                   value.drill_group_id.value &&
               role_events[index].dance_move_drill_id ==
                   value.dance_move_drill_id.value,
           "role event semantics differ");
  }

  expect(otgy_chart_semantics_get_showtime_start_events_count(abi) ==
             direct.showtime_start_events().size(),
         "Showtime start event count differs");
  const auto* starts =
      otgy_chart_semantics_get_showtime_start_events(abi);
  for (std::size_t index = 0U;
       index < direct.showtime_start_events().size(); ++index) {
    const auto& value = direct.showtime_start_events()[index];
    expect(starts[index].id == value.id.value &&
               starts[index].row == value.row &&
               starts[index].showtime_id == value.showtime_id.value,
           "Showtime start semantics differ");
  }

  expect(otgy_chart_semantics_get_showtime_move_events_count(abi) ==
             direct.showtime_move_events().size(),
         "Showtime move event count differs");
  const auto* moves = otgy_chart_semantics_get_showtime_move_events(abi);
  for (std::size_t index = 0U;
       index < direct.showtime_move_events().size(); ++index) {
    const auto& value = direct.showtime_move_events()[index];
    expect(moves[index].id == value.id.value &&
               moves[index].row == value.row &&
               moves[index].showtime_id == value.showtime_id.value &&
               moves[index].dance_move_drill_id ==
                   value.dance_move_drill_id.value &&
               moves[index].show_n_count == value.show_n_count,
           "Showtime move semantics differ");
  }

  expect(otgy_chart_semantics_get_drill_groups_count(abi) ==
             direct.drill_groups().size(),
         "drill group count differs");
  const auto* groups = otgy_chart_semantics_get_drill_groups(abi);
  const auto* group_event_ids =
      otgy_chart_semantics_get_drill_group_event_ids(abi);
  std::uint64_t expected_group_event_count = 0U;
  for (std::size_t index = 0U; index < direct.drill_groups().size(); ++index) {
    const auto& value = direct.drill_groups()[index];
    expect(groups[index].id == value.id.value &&
               groups[index].dance_move_drill_id ==
                   value.dance_move_drill_id.value,
           "drill group semantics differ");
    expect(groups[index].npc_event_id_offset == expected_group_event_count &&
               groups[index].npc_event_id_count ==
                   value.npc_event_ids.size(),
           "NPC drill group slice differs");
    for (std::size_t item = 0U; item < value.npc_event_ids.size(); ++item) {
      expect(group_event_ids[groups[index].npc_event_id_offset + item] ==
                 value.npc_event_ids[item].value,
             "NPC drill group event differs");
    }
    expected_group_event_count += value.npc_event_ids.size();
    expect(groups[index].player_event_id_offset ==
                   expected_group_event_count &&
               groups[index].player_event_id_count ==
                   value.player_event_ids.size(),
           "player drill group slice differs");
    for (std::size_t item = 0U; item < value.player_event_ids.size(); ++item) {
      expect(group_event_ids[groups[index].player_event_id_offset + item] ==
                 value.player_event_ids[item].value,
             "player drill group event differs");
    }
    expected_group_event_count += value.player_event_ids.size();
  }
  expect(otgy_chart_semantics_get_drill_group_event_ids_count(abi) ==
             expected_group_event_count,
         "drill group backing ID count differs");

  expect(otgy_chart_semantics_get_dance_move_drills_count(abi) ==
             direct.dance_move_drills().size(),
         "dance move drill count differs");
  const auto* drills = otgy_chart_semantics_get_dance_move_drills(abi);
  const auto* drill_group_ids =
      otgy_chart_semantics_get_dance_move_drill_group_ids(abi);
  std::uint64_t expected_drill_group_count = 0U;
  for (std::size_t index = 0U; index < direct.dance_move_drills().size();
       ++index) {
    const auto& value = direct.dance_move_drills()[index];
    expect(drills[index].id == value.id.value &&
               drills[index].move_n_count == value.move_n_count &&
               drills[index].drill_group_id_offset ==
                   expected_drill_group_count &&
               drills[index].drill_group_id_count ==
                   value.drill_group_ids.size(),
           "dance move drill semantics differ");
    for (std::size_t item = 0U; item < value.drill_group_ids.size(); ++item) {
      expect(drill_group_ids[drills[index].drill_group_id_offset + item] ==
                 value.drill_group_ids[item].value,
             "dance move drill group differs");
    }
    expected_drill_group_count += value.drill_group_ids.size();
  }
  expect(
      otgy_chart_semantics_get_dance_move_drill_group_ids_count(abi) ==
          expected_drill_group_count,
      "dance move drill backing ID count differs");

  expect(otgy_chart_semantics_get_showtimes_count(abi) ==
             direct.showtimes().size(),
         "Showtime relation count differs");
  const auto* showtimes = otgy_chart_semantics_get_showtimes(abi);
  const auto* showtime_drill_ids =
      otgy_chart_semantics_get_showtime_dance_move_drill_ids(abi);
  const auto* showtime_move_ids =
      otgy_chart_semantics_get_showtime_move_event_ids(abi);
  std::uint64_t expected_showtime_drill_count = 0U;
  std::uint64_t expected_showtime_move_count = 0U;
  for (std::size_t index = 0U; index < direct.showtimes().size(); ++index) {
    const auto& value = direct.showtimes()[index];
    expect(showtimes[index].id == value.id.value &&
               showtimes[index].start_event_id ==
                   value.start_event_id.value &&
               showtimes[index].dance_move_drill_id_offset ==
                   expected_showtime_drill_count &&
               showtimes[index].dance_move_drill_id_count ==
                   value.dance_move_drill_ids.size() &&
               showtimes[index].show_move_event_id_offset ==
                   expected_showtime_move_count &&
               showtimes[index].show_move_event_id_count ==
                   value.show_move_event_ids.size(),
           "Showtime relation semantics differ");
    for (std::size_t item = 0U; item < value.dance_move_drill_ids.size();
         ++item) {
      expect(showtime_drill_ids
                 [showtimes[index].dance_move_drill_id_offset + item] ==
                 value.dance_move_drill_ids[item].value,
             "Showtime dance move reference differs");
    }
    for (std::size_t item = 0U; item < value.show_move_event_ids.size();
         ++item) {
      expect(showtime_move_ids
                 [showtimes[index].show_move_event_id_offset + item] ==
                 value.show_move_event_ids[item].value,
             "Showtime move event reference differs");
    }
    expected_showtime_drill_count += value.dance_move_drill_ids.size();
    expected_showtime_move_count += value.show_move_event_ids.size();
  }
  expect(
      otgy_chart_semantics_get_showtime_dance_move_drill_ids_count(abi) ==
              expected_showtime_drill_count &&
          otgy_chart_semantics_get_showtime_move_event_ids_count(abi) ==
              expected_showtime_move_count,
      "Showtime backing ID counts differ");
}

void test_timing_and_coordination() {
  const otgy::core::TimingParameters direct_timing{129.24970691676435,
                                                    -0.090521541950113374};
  const otgy_timing_parameters abi_timing{direct_timing.bpm,
                                          direct_timing.offset_seconds};
  struct Conversion final {
    otgy::core::TimingConversionResult (*direct)(
        otgy::core::TimingParameters, double) noexcept;
    otgy_status (*abi)(otgy_timing_parameters, double, double*, otgy_error**);
    double input;
  };
  const std::array<Conversion, 4> conversions{{
      {otgy::core::seconds_to_beat, otgy_seconds_to_beat, 1.25},
      {otgy::core::beat_to_seconds, otgy_beat_to_seconds, 3.5},
      {otgy::core::seconds_to_sixteenth_row,
       otgy_seconds_to_sixteenth_row, 1.25},
      {otgy::core::sixteenth_row_to_seconds,
       otgy_sixteenth_row_to_seconds, 14.0},
  }};
  for (const auto& conversion : conversions) {
    const auto direct = conversion.direct(direct_timing, conversion.input);
    double abi = 0.0;
    otgy_error* raw_error = nullptr;
    expect(conversion.abi(abi_timing, conversion.input, &abi, &raw_error) ==
               OTGY_STATUS_OK,
           "C ABI timing conversion failed");
    auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
    expect(error == nullptr && same_double(abi, *direct.value()),
           "timing conversion differs");
  }
  const auto direct_beat_row = otgy::core::beat_to_sixteenth_row(-2.25);
  double abi_beat_row = 0.0;
  otgy_error* raw_error = nullptr;
  expect(otgy_beat_to_sixteenth_row(-2.25, &abi_beat_row, &raw_error) ==
             OTGY_STATUS_OK &&
             same_double(abi_beat_row, *direct_beat_row.value()),
         "beat-to-row differs");
  const auto direct_row_beat = otgy::core::sixteenth_row_to_beat(-9.0);
  expect(otgy_sixteenth_row_to_beat(-9.0, &abi_beat_row, &raw_error) ==
             OTGY_STATUS_OK &&
             same_double(abi_beat_row, *direct_row_beat.value()),
         "row-to-beat differs");

  const auto direct_offset =
      otgy::core::adjust_offset_by_beats(direct_timing, -2.0);
  double abi_offset = 0.0;
  expect(otgy_adjust_offset_by_beats(abi_timing, -2.0, &abi_offset,
                                     &raw_error) == OTGY_STATUS_OK &&
             same_double(abi_offset, *direct_offset.value()),
         "Offset beat adjustment differs");

  const auto direct_no_grid =
      otgy::core::seconds_to_beat({0.0, 0.0}, 1.0);
  expect(otgy_seconds_to_beat({0.0, 0.0}, 1.0, &abi_beat_row, &raw_error) ==
             OTGY_STATUS_CORE_ERROR,
         "NoGrid status differs");
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(error.get(), OTGY_ERROR_DOMAIN_TIMING_CONVERSION,
               static_cast<std::uint32_t>(direct_no_grid.error()),
               otgy::core::timing_conversion_error_message(
                   direct_no_grid.error()));

  const auto direct_grid =
      otgy::core::generate_timing_grid({100.0, 0.0}, 0.0, 9.6);
  otgy_timing_grid* raw_grid = nullptr;
  expect(otgy_generate_timing_grid({100.0, 0.0}, 0.0, 9.6, &raw_grid,
                                   &raw_error) == OTGY_STATUS_OK,
         "C ABI timing grid failed");
  auto grid = handle<otgy_timing_grid, otgy_timing_grid_destroy>(raw_grid);
  expect(otgy_timing_grid_get_line_count(grid.get()) ==
             direct_grid.grid()->lines().size(),
         "timing grid count differs");
  const auto* abi_lines = otgy_timing_grid_get_lines(grid.get());
  for (std::size_t index = 0U; index < direct_grid.grid()->lines().size();
       ++index) {
    const auto& direct = direct_grid.grid()->lines()[index];
    expect(abi_lines[index].sixteenth_row == direct.sixteenth_row &&
               same_double(abi_lines[index].seconds, direct.seconds) &&
               abi_lines[index].kind ==
                   static_cast<std::uint32_t>(direct.kind),
           "timing grid line differs");
  }

  const auto direct_range = otgy::core::calculate_chart_timeline_range(
      empty_chart(), direct_timing, 7.25);
  otgy_chart* raw_range_chart = nullptr;
  expect(otgy_chart_create_empty(&raw_range_chart, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI range chart create failed");
  auto range_chart =
      handle<otgy_chart, otgy_chart_destroy>(raw_range_chart);
  otgy_timeline_range abi_range{};
  expect(otgy_chart_calculate_timeline_range(
             range_chart.get(), abi_timing, 1U, 7.25, &abi_range,
             &raw_error) == OTGY_STATUS_OK,
         "C ABI timeline range failed");
  expect(same_double(abi_range.begin_seconds,
                     direct_range.range()->begin_seconds()) &&
             same_double(abi_range.end_seconds,
                         direct_range.range()->end_seconds()) &&
             same_double(abi_range.end_sixteenth_row,
                         direct_range.range()->end_sixteenth_row()) &&
             abi_range.uses_zero_bpm_projection ==
                 static_cast<std::uint8_t>(
                     direct_range.range()->uses_zero_bpm_projection()),
         "timeline range differs");

  const auto direct_navigation = otgy::core::calculate_timeline_navigation(
      *direct_range.range(), 15.25, 6.5);
  otgy_timeline_navigation abi_navigation{};
  expect(otgy_calculate_timeline_navigation(
             abi_range, 15.25, 6.5, &abi_navigation, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI navigation failed");
  expect(same_double(abi_navigation.to_begin_row,
                     direct_navigation.navigation()->to_begin_row()) &&
             same_double(abi_navigation.page_up_row,
                         direct_navigation.navigation()->page_up_row()) &&
             same_double(abi_navigation.page_down_row,
                         direct_navigation.navigation()->page_down_row()) &&
             same_double(abi_navigation.to_end_row,
                         direct_navigation.navigation()->to_end_row()) &&
             abi_navigation.can_page_down ==
                 static_cast<std::uint8_t>(
                     direct_navigation.navigation()->can_page_down()),
         "timeline navigation differs");

  const auto direct_start = otgy::core::start_playback(10.0, -0.25);
  otgy_playback_position abi_position{};
  expect(otgy_start_playback(1U, 10.0, -0.25, &abi_position, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI playback start failed");
  expect(abi_position.phase ==
                 static_cast<std::uint32_t>(direct_start.position()->phase()) &&
             same_double(abi_position.timeline_seconds,
                         direct_start.position()->timeline_seconds()),
         "playback start differs");
  const auto direct_advance =
      otgy::core::advance_playback(*direct_start.position(), 0.5);
  expect(otgy_advance_playback(abi_position, 0.5, &abi_position, &raw_error) ==
             OTGY_STATUS_OK &&
             same_double(abi_position.timeline_seconds,
                         direct_advance.position()->timeline_seconds()),
         "playback advance differs");
  const auto direct_pause =
      otgy::core::pause_playback(*direct_advance.position());
  expect(otgy_pause_playback(abi_position, &abi_position, &raw_error) ==
             OTGY_STATUS_OK &&
             abi_position.phase ==
                 static_cast<std::uint32_t>(direct_pause.position()->phase()),
         "playback pause differs");

  for (std::uint32_t mode = 0U; mode <= 3U; ++mode) {
    for (std::uint32_t operation = 0U; operation <= 4U; ++operation) {
      const auto direct = otgy::core::coordinate_application_operation(
          static_cast<otgy::core::ApplicationOperationMode>(mode),
          static_cast<otgy::core::ApplicationOperationClass>(operation));
      std::uint32_t abi = 99U;
      expect(otgy_coordinate_application_operation(mode, operation, &abi,
                                                   &raw_error) ==
                 OTGY_STATUS_OK &&
                 abi == static_cast<std::uint32_t>(direct),
             "operation gate differs");
    }
  }
  otgy_timing_grid_destroy(nullptr);
}

Handle<otgy_chart, otgy_chart_destroy> test_chart_edit_and_persistence() {
  auto direct_draft = empty_chart();
  otgy::core::ChartEditor direct_editor(direct_draft);
  otgy_chart* raw_chart = nullptr;
  otgy_error* raw_error = nullptr;
  expect(otgy_chart_create_empty(&raw_chart, &raw_error) == OTGY_STATUS_OK,
         "C ABI empty chart failed");
  auto chart = handle<otgy_chart, otgy_chart_destroy>(raw_chart);
  otgy_chart_editor* raw_editor = nullptr;
  expect(otgy_chart_editor_create(chart.get(), &raw_editor, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI editor create failed");
  auto editor =
      handle<otgy_chart_editor, otgy_chart_editor_destroy>(raw_editor);

  const auto direct_first =
      direct_editor.click(otgy::core::ChartColumn::Npc, 0U);
  otgy_chart_edit_result abi_first{};
  expect(otgy_chart_editor_click(editor.get(), OTGY_CHART_COLUMN_NPC, 0U,
                                 &abi_first, &raw_error) == OTGY_STATUS_OK,
         "C ABI first edit failed");
  compare_edit(direct_first, abi_first);
  const auto direct_second =
      direct_editor.click(otgy::core::ChartColumn::Npc, 4U);
  otgy_chart_edit_result abi_second{};
  expect(otgy_chart_editor_click(editor.get(), OTGY_CHART_COLUMN_NPC, 4U,
                                 &abi_second, &raw_error) == OTGY_STATUS_OK,
         "C ABI second edit failed");
  compare_edit(direct_second, abi_second);
  expect(otgy_chart_editor_can_undo(editor.get()) == direct_editor.can_undo() &&
             otgy_chart_editor_get_undo_count(editor.get()) ==
                 direct_editor.undo_count(),
         "editor history differs");

  auto direct_undo = direct_editor.undo();
  otgy_chart_edit_result abi_undo{};
  expect(otgy_chart_editor_undo(editor.get(), &abi_undo, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI undo failed");
  compare_edit(direct_undo, abi_undo);
  auto direct_redo = direct_editor.redo();
  otgy_chart_edit_result abi_redo{};
  expect(otgy_chart_editor_redo(editor.get(), &abi_redo, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI redo failed");
  compare_edit(direct_redo, abi_redo);
  expect(otgy_chart_editor_can_redo(editor.get()) == direct_editor.can_redo(),
         "redo state differs");
  expect(otgy_chart_editor_get_redo_count(editor.get()) ==
             direct_editor.redo_count(),
         "redo history count differs");

  otgy_chart* raw_edited = nullptr;
  expect(otgy_chart_editor_copy_chart(editor.get(), &raw_edited, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI editor chart copy failed");
  auto edited = handle<otgy_chart, otgy_chart_destroy>(raw_edited);
  const auto direct_markers =
      otgy::core::project_chart_markers(direct_editor.draft());
  otgy_chart_markers* raw_markers = nullptr;
  expect(otgy_chart_project_markers(edited.get(), &raw_markers, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI marker projection failed");
  auto markers =
      handle<otgy_chart_markers, otgy_chart_markers_destroy>(raw_markers);
  compare_markers(*direct_markers.markers(), markers.get());

  const auto direct_ordinal = otgy::core::chart_note_ordinal(
      direct_editor.draft(), direct_second.primary_event_id);
  std::uint64_t abi_ordinal = 0U;
  expect(otgy_chart_note_ordinal(edited.get(),
                                 direct_second.primary_event_id.value,
                                 &abi_ordinal, &raw_error) == OTGY_STATUS_OK &&
             abi_ordinal == direct_ordinal.ordinal,
         "note ordinal differs");

  const double bpm = 100.00000000000003;
  const double offset = -0.12345678901234566;
  const auto direct_formal =
      otgy::core::encode_formal_draft(direct_editor.draft(), bpm, offset);
  otgy_bytes* raw_formal = nullptr;
  expect(otgy_project_encode_formal_draft(edited.get(), bpm, offset,
                                          &raw_formal, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI formal encode failed");
  auto formal = handle<otgy_bytes, otgy_bytes_destroy>(raw_formal);
  const auto formal_view = otgy_bytes_get_view(formal.get());
  expect(formal_view.length == direct_formal.bytes()->size() &&
             std::memcmp(formal_view.data, direct_formal.bytes()->data(),
                         direct_formal.bytes()->size()) == 0,
         "formal project bytes differ");
  otgy_chart* raw_decoded = nullptr;
  double decoded_bpm = 0.0;
  double decoded_offset = 0.0;
  expect(otgy_project_decode_formal_draft(
             formal_view, &raw_decoded, &decoded_bpm, &decoded_offset,
             &raw_error) == OTGY_STATUS_OK,
         "C ABI formal decode failed");
  auto decoded = handle<otgy_chart, otgy_chart_destroy>(raw_decoded);
  expect(otgy_chart_equal(edited.get(), decoded.get()) == 1U &&
             same_double(decoded_bpm, bpm) &&
             same_double(decoded_offset, offset),
         "formal project round-trip differs");

  const auto direct_recovery =
      otgy::core::encode_recovery_snapshot(direct_editor.draft());
  otgy_bytes* raw_recovery = nullptr;
  expect(otgy_project_encode_recovery_snapshot(
             edited.get(), &raw_recovery, &raw_error) == OTGY_STATUS_OK,
         "C ABI recovery encode failed");
  auto recovery = handle<otgy_bytes, otgy_bytes_destroy>(raw_recovery);
  const auto recovery_view = otgy_bytes_get_view(recovery.get());
  expect(recovery_view.length == direct_recovery.bytes()->size() &&
             std::memcmp(recovery_view.data, direct_recovery.bytes()->data(),
                         direct_recovery.bytes()->size()) == 0,
         "recovery bytes differ");
  expect(otgy_project_decode_recovery_snapshot(
             recovery_view, &raw_decoded, &raw_error) == OTGY_STATUS_OK,
         "C ABI recovery decode failed");
  decoded.reset(raw_decoded);
  expect(otgy_chart_equal(edited.get(), decoded.get()) == 1U,
         "recovery chart differs");

  const auto direct_validation =
      otgy::core::validate_finished_chart(direct_editor.draft());
  otgy_chart_validation* raw_validation = nullptr;
  expect(otgy_chart_validate_finished(edited.get(), &raw_validation,
                                      &raw_error) == OTGY_STATUS_OK,
         "C ABI draft validation failed");
  auto validation =
      handle<otgy_chart_validation, otgy_chart_validation_destroy>(
          raw_validation);
  expect(otgy_chart_validation_is_finished(validation.get()) == 0U &&
             otgy_chart_validation_get_diagnostic_count(validation.get()) ==
                 direct_validation.diagnostics()->size(),
         "draft validation state differs");
  const auto* abi_diagnostics =
      otgy_chart_validation_get_diagnostics(validation.get());
  for (std::size_t index = 0U;
       index < direct_validation.diagnostics()->size(); ++index) {
    const auto& direct = direct_validation.diagnostics()->at(index);
    expect(abi_diagnostics[index].code ==
                   static_cast<std::uint32_t>(direct.code) &&
               abi_diagnostics[index].object_id == direct.object_id &&
               abi_diagnostics[index].related_id == direct.related_id &&
               abi_diagnostics[index].row == direct.row,
           "chart diagnostic differs");
  }
  otgy_finished_chart* unavailable = nullptr;
  expect(otgy_chart_validation_copy_finished(
             validation.get(), &unavailable, &raw_error) ==
             OTGY_STATUS_INVALID_ARGUMENT,
         "unfinished chart exposed a finished snapshot");
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(error.get(), OTGY_ERROR_DOMAIN_ABI,
               OTGY_ABI_ERROR_RESULT_NOT_AVAILABLE,
               "requested C ABI result is not available");

  otgy_chart* raw_clone = nullptr;
  expect(otgy_chart_clone(edited.get(), &raw_clone, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI chart clone failed");
  auto clone = handle<otgy_chart, otgy_chart_destroy>(raw_clone);
  expect(otgy_chart_equal(edited.get(), clone.get()) == 1U,
         "chart clone differs");

  otgy_chart_editor* raw_empty_editor = nullptr;
  expect(otgy_chart_editor_create(chart.get(), &raw_empty_editor, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI empty editor create failed");
  auto empty_editor =
      handle<otgy_chart_editor, otgy_chart_editor_destroy>(raw_empty_editor);
  otgy::core::ChartEditor direct_empty_editor(empty_chart());
  const auto direct_clear = direct_empty_editor.clear();
  otgy_chart_edit_result abi_clear{};
  expect(otgy_chart_editor_clear(empty_editor.get(), &abi_clear, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI clear failed");
  compare_edit(direct_clear, abi_clear);
  return edited;
}

void test_slk_finished_boundary(const std::vector<std::uint8_t>& bytes) {
  const auto direct_import =
      otgy::core::import_finished_slk(bytes.data(), bytes.size());
  expect(direct_import.has_value(), "direct SLK import failed");
  otgy_error* raw_error = nullptr;
  otgy_slk_document* raw_document = nullptr;
  expect(otgy_slk_import(view(bytes), &raw_document, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI SLK import failed");
  auto document =
      handle<otgy_slk_document, otgy_slk_document_destroy>(raw_document);
  const auto direct_projection =
      otgy::core::project_finished_slk_to_chart_draft(
          *direct_import.document());
  expect(direct_projection.has_value(), "direct SLK projection failed");
  otgy_chart* raw_chart = nullptr;
  expect(otgy_slk_document_project_chart(document.get(), &raw_chart,
                                         &raw_error) == OTGY_STATUS_OK,
         "C ABI SLK projection failed");
  auto chart = handle<otgy_chart, otgy_chart_destroy>(raw_chart);
  document.reset();

  otgy_chart_semantics* raw_semantics = nullptr;
  expect(otgy_chart_copy_semantics(chart.get(), &raw_semantics, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI chart semantic snapshot failed");
  auto semantics =
      handle<otgy_chart_semantics, otgy_chart_semantics_destroy>(
          raw_semantics);
  compare_semantics(*direct_projection.draft(), semantics.get());

  const otgy::core::TimingParameters range_timing{100.0, 0.0};
  const auto direct_range = otgy::core::calculate_chart_timeline_range(
      *direct_projection.draft(), range_timing, std::nullopt);
  otgy_timeline_range abi_range{};
  expect(otgy_chart_calculate_timeline_range(
             chart.get(), {100.0, 0.0}, 0U, 0.0, &abi_range,
             &raw_error) == OTGY_STATUS_OK,
         "C ABI imported chart range failed");
  expect(same_double(abi_range.end_seconds,
                     direct_range.range()->end_seconds()) &&
             same_double(abi_range.end_sixteenth_row,
                         direct_range.range()->end_sixteenth_row()),
         "imported chart timeline range differs");

  const auto direct_markers =
      otgy::core::project_chart_markers(*direct_projection.draft());
  otgy_chart_markers* raw_markers = nullptr;
  expect(otgy_chart_project_markers(chart.get(), &raw_markers, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI imported marker projection failed");
  auto markers =
      handle<otgy_chart_markers, otgy_chart_markers_destroy>(raw_markers);
  compare_markers(*direct_markers.markers(), markers.get());

  const auto direct_moves =
      otgy::core::project_showtime_moves(*direct_projection.draft());
  otgy_showtime_moves* raw_moves = nullptr;
  expect(otgy_chart_project_showtime_moves(chart.get(), &raw_moves,
                                           &raw_error) == OTGY_STATUS_OK,
         "C ABI Showtime projection failed");
  auto moves =
      handle<otgy_showtime_moves, otgy_showtime_moves_destroy>(raw_moves);
  compare_showtime_moves(*direct_moves.moves(), moves.get());

  for (const auto& marker : *direct_markers.markers()) {
    if (marker.type == otgy::core::ChartMarkerType::NpcNote) {
      const auto direct = otgy::core::chart_note_ordinal(
          *direct_projection.draft(), marker.id);
      std::uint64_t abi = 0U;
      expect(otgy_chart_note_ordinal(chart.get(), marker.id.value, &abi,
                                     &raw_error) == OTGY_STATUS_OK &&
                 abi == direct.ordinal,
             "imported note ordinal differs");
      break;
    }
  }

  auto direct_validation =
      otgy::core::validate_finished_chart(*direct_projection.draft());
  expect(direct_validation.is_finished(), "direct chart validation failed");
  otgy_chart_validation* raw_validation = nullptr;
  expect(otgy_chart_validate_finished(chart.get(), &raw_validation,
                                      &raw_error) == OTGY_STATUS_OK,
         "C ABI finished validation failed");
  auto validation =
      handle<otgy_chart_validation, otgy_chart_validation_destroy>(
          raw_validation);
  expect(otgy_chart_validation_is_finished(validation.get()) == 1U &&
             otgy_chart_validation_get_diagnostic_count(validation.get()) ==
                 0U,
         "finished validation state differs");
  otgy_finished_chart* raw_finished = nullptr;
  expect(otgy_chart_validation_copy_finished(
             validation.get(), &raw_finished, &raw_error) == OTGY_STATUS_OK,
         "C ABI finished chart copy failed");
  auto finished =
      handle<otgy_finished_chart, otgy_finished_chart_destroy>(raw_finished);
  validation.reset();
  const auto direct_export = otgy::core::export_finished_chart_to_slk(
      *direct_validation.finished_chart());
  otgy_bytes* raw_export = nullptr;
  expect(otgy_finished_chart_export_slk(finished.get(), &raw_export,
                                        &raw_error) == OTGY_STATUS_OK,
         "C ABI SLK export failed");
  auto export_bytes = handle<otgy_bytes, otgy_bytes_destroy>(raw_export);
  const auto abi_export = otgy_bytes_get_view(export_bytes.get());
  expect(abi_export.length == direct_export.bytes()->size() &&
             std::memcmp(abi_export.data, direct_export.bytes()->data(),
                         direct_export.bytes()->size()) == 0 &&
             abi_export.length == bytes.size() &&
             std::memcmp(abi_export.data, bytes.data(), bytes.size()) == 0,
         "canonical SLK bytes differ");

  auto direct_empty = otgy::core::import_finished_slk(nullptr, 0U);
  otgy_slk_document* failed_document = reinterpret_cast<otgy_slk_document*>(1);
  expect(otgy_slk_import({nullptr, 0U}, &failed_document, &raw_error) ==
             OTGY_STATUS_CORE_ERROR,
         "empty SLK error status differs");
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect(failed_document == nullptr, "failed SLK exposed a document");
  expect_error(error.get(), OTGY_ERROR_DOMAIN_SLK_IMPORT,
               static_cast<std::uint32_t>(direct_empty.failure()->error),
               otgy::core::slk_import_error_message(
                   direct_empty.failure()->error));

  otgy_slk_document_destroy(nullptr);
  otgy_chart_validation_destroy(nullptr);
  otgy_finished_chart_destroy(nullptr);
}

void test_slk_position() {
  otgy_error* raw_error = nullptr;
  const auto direct = otgy::core::compose_slk_position(12U, 3U, 2U);
  otgy_slk_position abi{};
  expect(otgy_compose_slk_position(12U, 3U, 2U, &abi, &raw_error) ==
             OTGY_STATUS_OK,
         "C ABI SLK position compose failed");
  expect(abi.measure == direct.position()->measure &&
             abi.beat == direct.position()->beat &&
             abi.sixteenth == direct.position()->sixteenth &&
             abi.total_sixteenth == direct.position()->total_sixteenth,
         "SLK position compose differs");
  const auto decomposed =
      otgy::core::decompose_slk_position(direct.position()->total_sixteenth);
  expect(otgy_decompose_slk_position(abi.total_sixteenth, &abi, &raw_error) ==
             OTGY_STATUS_OK &&
             abi.measure == decomposed.position()->measure,
         "SLK position decompose differs");
  const auto validated = otgy::core::validate_slk_position(
      abi.measure, abi.beat, abi.sixteenth, abi.total_sixteenth);
  expect(otgy_validate_slk_position(abi.measure, abi.beat, abi.sixteenth,
                                    abi.total_sixteenth, &abi,
                                    &raw_error) == OTGY_STATUS_OK &&
             abi.total_sixteenth == validated.position()->total_sixteenth,
         "SLK position validate differs");

  const auto direct_error = otgy::core::compose_slk_position(0U, 4U, 0U);
  expect(otgy_compose_slk_position(0U, 4U, 0U, &abi, &raw_error) ==
             OTGY_STATUS_CORE_ERROR,
         "SLK position error status differs");
  auto error = handle<otgy_error, otgy_error_destroy>(raw_error);
  expect_error(error.get(), OTGY_ERROR_DOMAIN_SLK_POSITION,
               static_cast<std::uint32_t>(direct_error.error()),
               otgy::core::slk_position_error_message(direct_error.error()));
}

}  // namespace

int main() {
  try {
    test_timing_and_coordination();
    auto edited = test_chart_edit_and_persistence();
    expect(edited != nullptr, "edited chart ownership was lost");
    test_slk_finished_boundary(read_file(OTGY_C_API_SLK_FIXTURE));
    test_slk_position();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
