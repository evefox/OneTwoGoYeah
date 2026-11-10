// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_draft.hpp"

#include <utility>

namespace otgy::core {

ChartDraft::ChartDraft(
    std::vector<ChartRoleEvent> role_events,
    std::vector<ChartShowtimeStartEvent> showtime_start_events,
    std::vector<ChartShowtimeMoveEvent> showtime_move_events,
    std::vector<ChartDrillGroup> drill_groups,
    std::vector<ChartDanceMoveDrill> dance_move_drills,
    std::vector<ChartShowtime> showtimes) noexcept
    : role_events_(std::move(role_events)),
      showtime_start_events_(std::move(showtime_start_events)),
      showtime_move_events_(std::move(showtime_move_events)),
      drill_groups_(std::move(drill_groups)),
      dance_move_drills_(std::move(dance_move_drills)),
      showtimes_(std::move(showtimes)) {}

bool chart_drafts_equal(const ChartDraft& left,
                        const ChartDraft& right) noexcept {
  if (left.role_events().size() != right.role_events().size() ||
      left.showtime_start_events().size() !=
          right.showtime_start_events().size() ||
      left.showtime_move_events().size() !=
          right.showtime_move_events().size() ||
      left.drill_groups().size() != right.drill_groups().size() ||
      left.dance_move_drills().size() != right.dance_move_drills().size() ||
      left.showtimes().size() != right.showtimes().size()) {
    return false;
  }
  for (std::size_t index = 0U; index < left.role_events().size(); ++index) {
    const auto& a = left.role_events()[index];
    const auto& b = right.role_events()[index];
    if (a.id.value != b.id.value || a.row != b.row || a.role != b.role ||
        a.type != b.type ||
        a.drill_group_id.value != b.drill_group_id.value ||
        a.dance_move_drill_id.value != b.dance_move_drill_id.value) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < left.showtime_start_events().size();
       ++index) {
    const auto& a = left.showtime_start_events()[index];
    const auto& b = right.showtime_start_events()[index];
    if (a.id.value != b.id.value || a.row != b.row ||
        a.showtime_id.value != b.showtime_id.value) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < left.showtime_move_events().size();
       ++index) {
    const auto& a = left.showtime_move_events()[index];
    const auto& b = right.showtime_move_events()[index];
    if (a.id.value != b.id.value || a.row != b.row ||
        a.showtime_id.value != b.showtime_id.value ||
        a.dance_move_drill_id.value != b.dance_move_drill_id.value ||
        a.show_n_count != b.show_n_count) {
      return false;
    }
  }
  for (std::size_t index = 0U; index < left.drill_groups().size(); ++index) {
    const auto& a = left.drill_groups()[index];
    const auto& b = right.drill_groups()[index];
    if (a.id.value != b.id.value ||
        a.dance_move_drill_id.value != b.dance_move_drill_id.value ||
        a.npc_event_ids.size() != b.npc_event_ids.size() ||
        a.player_event_ids.size() != b.player_event_ids.size()) {
      return false;
    }
    for (std::size_t item = 0U; item < a.npc_event_ids.size(); ++item) {
      if (a.npc_event_ids[item].value != b.npc_event_ids[item].value) {
        return false;
      }
    }
    for (std::size_t item = 0U; item < a.player_event_ids.size(); ++item) {
      if (a.player_event_ids[item].value != b.player_event_ids[item].value) {
        return false;
      }
    }
  }
  for (std::size_t index = 0U; index < left.dance_move_drills().size();
       ++index) {
    const auto& a = left.dance_move_drills()[index];
    const auto& b = right.dance_move_drills()[index];
    if (a.id.value != b.id.value || a.move_n_count != b.move_n_count ||
        a.drill_group_ids.size() != b.drill_group_ids.size()) {
      return false;
    }
    for (std::size_t item = 0U; item < a.drill_group_ids.size(); ++item) {
      if (a.drill_group_ids[item].value != b.drill_group_ids[item].value) {
        return false;
      }
    }
  }
  for (std::size_t index = 0U; index < left.showtimes().size(); ++index) {
    const auto& a = left.showtimes()[index];
    const auto& b = right.showtimes()[index];
    if (a.id.value != b.id.value ||
        a.start_event_id.value != b.start_event_id.value ||
        a.dance_move_drill_ids.size() != b.dance_move_drill_ids.size() ||
        a.show_move_event_ids.size() != b.show_move_event_ids.size()) {
      return false;
    }
    for (std::size_t item = 0U; item < a.dance_move_drill_ids.size(); ++item) {
      if (a.dance_move_drill_ids[item].value !=
          b.dance_move_drill_ids[item].value) {
        return false;
      }
    }
    for (std::size_t item = 0U; item < a.show_move_event_ids.size(); ++item) {
      if (a.show_move_event_ids[item].value !=
          b.show_move_event_ids[item].value) {
        return false;
      }
    }
  }
  return true;
}

std::optional<std::uint64_t> dance_move_drill_first_row(
    const ChartDraft& draft, ChartDanceMoveDrillId id) noexcept {
  const ChartDanceMoveDrill* target = nullptr;
  for (const auto& drill : draft.dance_move_drills()) {
    if (drill.id.value == id.value) {
      target = &drill;
      break;
    }
  }
  if (target == nullptr) {
    return std::nullopt;
  }
  std::optional<std::uint64_t> first_row;
  for (const auto group_id : target->drill_group_ids) {
    const ChartDrillGroup* group = nullptr;
    for (const auto& candidate : draft.drill_groups()) {
      if (candidate.id.value == group_id.value) {
        group = &candidate;
        break;
      }
    }
    if (group == nullptr) {
      continue;
    }
    const auto include = [&draft, &first_row](const ChartEventId event_id) {
      for (const auto& event : draft.role_events()) {
        if (event.id.value == event_id.value) {
          if (!first_row.has_value() || event.row < *first_row) {
            first_row = event.row;
          }
          return;
        }
      }
    };
    for (const auto event_id : group->npc_event_ids) {
      include(event_id);
    }
    for (const auto event_id : group->player_event_ids) {
      include(event_id);
    }
  }
  return first_row;
}

}  // namespace otgy::core
