// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/slk/slk_chart_draft_projection.hpp"

#include <new>
#include <utility>

namespace otgy::core {
namespace {

[[nodiscard]] ChartRole chart_role(SlkDrillBlockRole role) noexcept {
  return role == SlkDrillBlockRole::Npc ? ChartRole::Npc : ChartRole::Player;
}

[[nodiscard]] bool chart_event_type(SlkRowType source,
                                    ChartRoleEventType& target) noexcept {
  switch (source) {
    case SlkRowType::C:
      target = ChartRoleEventType::BlockStart;
      return true;
    case SlkRowType::N:
      target = ChartRoleEventType::Note;
      return true;
    case SlkRowType::S:
      target = ChartRoleEventType::MoveEnd;
      return true;
    case SlkRowType::D:
    case SlkRowType::R:
      return false;
  }
  return false;
}

[[nodiscard]] bool append_role_event(const SlkDrillGroupRow& source,
                                     ChartDanceMoveDrillId dance_move_drill_id,
                                     std::vector<ChartRoleEvent>& events,
                                     std::vector<ChartEventId>& relation_ids) {
  ChartRoleEventType type = ChartRoleEventType::Note;
  if (!chart_event_type(source.row.type, type)) {
    return false;
  }
  const ChartEventId id{source.row.sheet_row};
  events.push_back({id,
                    source.row.position.total_sixteenth,
                    chart_role(source.role),
                    type,
                    {source.drill_group_id.value},
                    dance_move_drill_id});
  relation_ids.push_back(id);
  return true;
}

[[nodiscard]] bool append_group(const SlkDanceMoveDrill& source,
                                const SlkDanceMoveDrillGroupMapping& mapping,
                                ChartDanceMoveDrillId dance_move_drill_id,
                                std::vector<ChartRoleEvent>& events,
                                std::vector<ChartDrillGroup>& groups) {
  ChartDrillGroup group{
      {mapping.drill_group_id.value}, dance_move_drill_id, {}, {}};
  if (!append_role_event(mapping.npc_c_row, dance_move_drill_id, events,
                         group.npc_event_ids)) {
    return false;
  }

  for (const auto& row : source.npc.rows) {
    if (row.row.drill_group_id.value == mapping.drill_group_id.value &&
        !append_role_event(row.row, dance_move_drill_id, events,
                           group.npc_event_ids)) {
      return false;
    }
  }
  if (!append_role_event(mapping.player_c_row, dance_move_drill_id, events,
                         group.player_event_ids)) {
    return false;
  }
  for (const auto& row : source.player.rows) {
    if (row.row.drill_group_id.value == mapping.drill_group_id.value &&
        !append_role_event(row.row, dance_move_drill_id, events,
                           group.player_event_ids)) {
      return false;
    }
  }
  groups.push_back(std::move(group));
  return true;
}

}  // namespace

SlkChartDraftProjectionResult::SlkChartDraftProjectionResult(
    ChartDraft draft) noexcept
    : storage_(std::move(draft)) {}

SlkChartDraftProjectionResult::SlkChartDraftProjectionResult(
    SlkChartDraftProjectionFailure failure) noexcept
    : storage_(failure) {}

bool SlkChartDraftProjectionResult::has_value() const noexcept {
  return std::holds_alternative<ChartDraft>(storage_);
}

const ChartDraft* SlkChartDraftProjectionResult::draft() const noexcept {
  return std::get_if<ChartDraft>(&storage_);
}

const SlkChartDraftProjectionFailure* SlkChartDraftProjectionResult::failure()
    const noexcept {
  return std::get_if<SlkChartDraftProjectionFailure>(&storage_);
}

ChartDraft SlkChartDraftProjectionResult::take_draft() && {
  return std::move(std::get<ChartDraft>(storage_));
}

SlkChartDraftProjectionResult project_finished_slk_to_chart_draft(
    const FinishedSlkDocument& document) noexcept {
  const auto& semantics = document.semantics();
  if (semantics.showtimes().empty()) {
    return SlkChartDraftProjectionResult{
        {SlkChartDraftProjectionError::InvalidFinishedDocument,
         semantics.boundary_row().has_value()
             ? semantics.boundary_row()->sheet_row
             : 0U}};
  }

  try {
    std::vector<ChartRoleEvent> role_events;
    std::vector<ChartShowtimeStartEvent> showtime_start_events;
    std::vector<ChartShowtimeMoveEvent> showtime_move_events;
    std::vector<ChartDrillGroup> drill_groups;
    std::vector<ChartDanceMoveDrill> dance_move_drills;
    std::vector<ChartShowtime> showtimes;

    for (const auto& source_showtime : semantics.showtimes()) {
      if (source_showtime.c_row.type != SlkRowType::C ||
          source_showtime.c_row.value != 0U ||
          source_showtime.stage.drills().empty() ||
          source_showtime.show_d_rows.empty() ||
          source_showtime.show_d_rows.size() >
              source_showtime.stage.drills().size()) {
        return SlkChartDraftProjectionResult{
            {SlkChartDraftProjectionError::InvalidFinishedDocument,
             source_showtime.c_row.sheet_row}};
      }

      ChartShowtime showtime{{source_showtime.id.value},
                             {source_showtime.c_row.sheet_row},
                             {},
                             {}};
      for (const auto& source_drill : source_showtime.stage.drills()) {
        const ChartDanceMoveDrillId drill_id{source_drill.id.value};
        ChartDanceMoveDrill drill{drill_id, source_drill.move_n_count, {}};
        for (const auto& mapping : source_drill.groups) {
          if (!append_group(source_drill, mapping, drill_id, role_events,
                            drill_groups)) {
            return SlkChartDraftProjectionResult{
                {SlkChartDraftProjectionError::InvalidFinishedDocument,
                 mapping.npc_c_row.row.sheet_row}};
          }
          drill.drill_group_ids.push_back({mapping.drill_group_id.value});
        }
        showtime.dance_move_drill_ids.push_back(drill_id);
        dance_move_drills.push_back(std::move(drill));
      }

      showtime_start_events.push_back(
          {{source_showtime.c_row.sheet_row},
           source_showtime.c_row.position.total_sixteenth,
           {source_showtime.id.value}});
      for (std::size_t index = 0U;
           index < source_showtime.show_d_rows.size(); ++index) {
        const auto& source_show_d = source_showtime.show_d_rows[index];
        if (source_show_d.row.type != SlkRowType::D ||
            source_show_d.show_n_count != source_show_d.row.value ||
            source_show_d.show_d_index != index ||
            source_show_d.dance_move_drill_id.value !=
                source_showtime.stage.drills()[index].id.value) {
          return SlkChartDraftProjectionResult{
              {SlkChartDraftProjectionError::InvalidFinishedDocument,
               source_show_d.row.sheet_row}};
        }
        const ChartEventId event_id{source_show_d.row.sheet_row};
        showtime_move_events.push_back(
            {event_id,
             source_show_d.row.position.total_sixteenth,
             {source_showtime.id.value},
             {source_show_d.dance_move_drill_id.value},
             source_show_d.show_n_count});
        showtime.show_move_event_ids.push_back(event_id);
      }
      showtimes.push_back(std::move(showtime));
    }

    return SlkChartDraftProjectionResult{
        ChartDraft{std::move(role_events), std::move(showtime_start_events),
                   std::move(showtime_move_events), std::move(drill_groups),
                   std::move(dance_move_drills), std::move(showtimes)}};
  } catch (const std::bad_alloc&) {
    return SlkChartDraftProjectionResult{
        {SlkChartDraftProjectionError::AllocationFailure, 0U}};
  }
}

}  // namespace otgy::core
