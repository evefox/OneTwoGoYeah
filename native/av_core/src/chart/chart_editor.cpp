// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_editor.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace otgy::core {
namespace {

struct MutableDraft final {
  std::vector<ChartRoleEvent> role_events;
  std::vector<ChartShowtimeStartEvent> showtime_start_events;
  std::vector<ChartShowtimeMoveEvent> showtime_move_events;
  std::vector<ChartDrillGroup> drill_groups;
  std::vector<ChartDanceMoveDrill> dance_move_drills;
  std::vector<ChartShowtime> showtimes;

  explicit MutableDraft(const ChartDraft& draft)
      : role_events(draft.role_events()),
        showtime_start_events(draft.showtime_start_events()),
        showtime_move_events(draft.showtime_move_events()),
        drill_groups(draft.drill_groups()),
        dance_move_drills(draft.dance_move_drills()),
        showtimes(draft.showtimes()) {}

  [[nodiscard]] ChartDraft finish() && {
    return {std::move(role_events), std::move(showtime_start_events),
            std::move(showtime_move_events), std::move(drill_groups),
            std::move(dance_move_drills), std::move(showtimes)};
  }
};

struct IdState final {
  std::uint64_t next;
  bool available;
};

[[nodiscard]] IdState initial_id_state(const ChartDraft& draft) noexcept {
  std::uint64_t maximum = 0U;
  const auto include = [&maximum](std::uint64_t value) {
    maximum = std::max(maximum, value);
  };
  for (const auto& event : draft.role_events()) {
    include(event.id.value);
  }
  for (const auto& event : draft.showtime_start_events()) {
    include(event.id.value);
    include(event.showtime_id.value);
  }
  for (const auto& event : draft.showtime_move_events()) {
    include(event.id.value);
    include(event.showtime_id.value);
    include(event.dance_move_drill_id.value);
  }
  for (const auto& group : draft.drill_groups()) {
    include(group.id.value);
    include(group.dance_move_drill_id.value);
  }
  for (const auto& drill : draft.dance_move_drills()) {
    include(drill.id.value);
  }
  for (const auto& showtime : draft.showtimes()) {
    include(showtime.id.value);
  }
  if (maximum == std::numeric_limits<std::uint64_t>::max()) {
    return {0U, false};
  }
  return {maximum + 1U, true};
}

[[nodiscard]] bool allocate_id(IdState& state, std::uint64_t& value) noexcept {
  if (!state.available) {
    return false;
  }
  value = state.next;
  if (state.next == std::numeric_limits<std::uint64_t>::max()) {
    state.available = false;
  } else {
    ++state.next;
  }
  return true;
}

[[nodiscard]] ChartRoleEvent* find_role_event(MutableDraft& draft,
                                              std::uint64_t id) noexcept {
  for (auto& event : draft.role_events) {
    if (event.id.value == id) {
      return &event;
    }
  }
  return nullptr;
}

[[nodiscard]] ChartDrillGroup* find_group(MutableDraft& draft,
                                          std::uint64_t id) noexcept {
  for (auto& group : draft.drill_groups) {
    if (group.id.value == id) {
      return &group;
    }
  }
  return nullptr;
}

[[nodiscard]] ChartDanceMoveDrill* find_drill(MutableDraft& draft,
                                              std::uint64_t id) noexcept {
  for (auto& drill : draft.dance_move_drills) {
    if (drill.id.value == id) {
      return &drill;
    }
  }
  return nullptr;
}

[[nodiscard]] ChartShowtime* find_showtime(MutableDraft& draft,
                                           std::uint64_t id) noexcept {
  for (auto& showtime : draft.showtimes) {
    if (showtime.id.value == id) {
      return &showtime;
    }
  }
  return nullptr;
}

[[nodiscard]] const ChartDrillGroup* find_group_const(
    const MutableDraft& draft, std::uint64_t id) noexcept {
  for (const auto& group : draft.drill_groups) {
    if (group.id.value == id) {
      return &group;
    }
  }
  return nullptr;
}

[[nodiscard]] const ChartRoleEvent* find_role_event_const(
    const MutableDraft& draft, std::uint64_t id) noexcept {
  for (const auto& event : draft.role_events) {
    if (event.id.value == id) {
      return &event;
    }
  }
  return nullptr;
}

[[nodiscard]] bool role_event_less(const ChartRoleEvent& left,
                                   const ChartRoleEvent& right) noexcept {
  return left.row != right.row ? left.row < right.row
                               : left.id.value < right.id.value;
}

void sort_role_events(MutableDraft& draft) {
  std::sort(draft.role_events.begin(), draft.role_events.end(),
            role_event_less);
}

void sort_relation_ids(MutableDraft& draft, std::vector<ChartEventId>& ids) {
  std::sort(ids.begin(), ids.end(), [&draft](ChartEventId left,
                                             ChartEventId right) {
    const auto* left_event = find_role_event(draft, left.value);
    const auto* right_event = find_role_event(draft, right.value);
    if (left_event == nullptr || right_event == nullptr) {
      return left.value < right.value;
    }
    return role_event_less(*left_event, *right_event);
  });
}

[[nodiscard]] std::uint64_t group_first_row(
    const MutableDraft& draft, ChartDrillGroupId group_id) noexcept {
  const auto* group = find_group_const(draft, group_id.value);
  if (group == nullptr) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  std::uint64_t first = std::numeric_limits<std::uint64_t>::max();
  for (const auto id : group->npc_event_ids) {
    const auto* event = find_role_event_const(draft, id.value);
    if (event != nullptr) {
      first = std::min(first, event->row);
    }
  }
  for (const auto id : group->player_event_ids) {
    const auto* event = find_role_event_const(draft, id.value);
    if (event != nullptr) {
      first = std::min(first, event->row);
    }
  }
  return first;
}

void sort_drill_group_ids(const MutableDraft& draft,
                          ChartDanceMoveDrill& drill) {
  std::sort(drill.drill_group_ids.begin(), drill.drill_group_ids.end(),
            [&draft](ChartDrillGroupId left, ChartDrillGroupId right) {
              const auto left_row = group_first_row(draft, left);
              const auto right_row = group_first_row(draft, right);
              return left_row != right_row ? left_row < right_row
                                           : left.value < right.value;
            });
}

void recompute_move_n_count(MutableDraft& draft,
                            ChartDanceMoveDrillId drill_id) noexcept {
  auto* drill = find_drill(draft, drill_id.value);
  if (drill == nullptr) {
    return;
  }
  std::uint64_t count = 0U;
  for (const auto& event : draft.role_events) {
    if (event.role == ChartRole::Npc &&
        event.type == ChartRoleEventType::Note &&
        event.dance_move_drill_id.value == drill_id.value) {
      ++count;
    }
  }
  drill->move_n_count = count;
}

void erase_event_references(MutableDraft& draft,
                            const std::vector<std::uint64_t>& ids) {
  const auto erased = [&ids](ChartEventId id) {
    return std::find(ids.begin(), ids.end(), id.value) != ids.end();
  };
  for (auto& group : draft.drill_groups) {
    group.npc_event_ids.erase(
        std::remove_if(group.npc_event_ids.begin(), group.npc_event_ids.end(),
                       erased),
        group.npc_event_ids.end());
    group.player_event_ids.erase(
        std::remove_if(group.player_event_ids.begin(),
                       group.player_event_ids.end(), erased),
        group.player_event_ids.end());
  }
  for (auto& showtime : draft.showtimes) {
    showtime.show_move_event_ids.erase(
        std::remove_if(showtime.show_move_event_ids.begin(),
                       showtime.show_move_event_ids.end(), erased),
        showtime.show_move_event_ids.end());
    if (erased(showtime.start_event_id)) {
      showtime.start_event_id = {0U};
    }
  }
}

void erase_role_events(MutableDraft& draft,
                       const std::vector<std::uint64_t>& ids) {
  std::vector<ChartDanceMoveDrillId> affected;
  for (const auto& event : draft.role_events) {
    if (std::find(ids.begin(), ids.end(), event.id.value) != ids.end() &&
        event.role == ChartRole::Npc) {
      affected.push_back(event.dance_move_drill_id);
    }
  }
  draft.role_events.erase(
      std::remove_if(draft.role_events.begin(), draft.role_events.end(),
                     [&ids](const ChartRoleEvent& event) {
                       return std::find(ids.begin(), ids.end(), event.id.value) !=
                              ids.end();
                     }),
      draft.role_events.end());
  erase_event_references(draft, ids);
  for (const auto id : affected) {
    recompute_move_n_count(draft, id);
  }
}

[[nodiscard]] bool editable_projection(
    const ChartDraft& draft, std::vector<ChartMarker>& markers,
    ChartEditCode& error) {
  auto projection = project_chart_markers(draft);
  if (!projection) {
    error = projection.error() == ChartProjectionError::AllocationFailure
                ? ChartEditCode::AllocationFailure
                : ChartEditCode::InvalidDraft;
    return false;
  }
  markers = std::move(projection).take_markers();
  for (std::size_t index = 0U; index < markers.size(); ++index) {
    for (std::size_t other = index + 1U; other < markers.size(); ++other) {
      if (markers[index].id.value == markers[other].id.value ||
          markers[index].row == markers[other].row) {
        error = ChartEditCode::InvalidDraft;
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] const ChartMarker* marker_at(
    const std::vector<ChartMarker>& markers, std::uint64_t row) noexcept {
  for (const auto& marker : markers) {
    if (marker.row == row) {
      return &marker;
    }
    if (marker.row > row) {
      break;
    }
  }
  return nullptr;
}

[[nodiscard]] bool npc_start_allowed(const ChartDraft& draft,
                                     std::uint64_t row,
                                     ChartEditCode& error) noexcept {
  const auto projection = project_showtime_moves(draft);
  if (!projection) {
    error = projection.error() == ChartProjectionError::RangeOverflow
                ? ChartEditCode::RangeOverflow
                : projection.error() == ChartProjectionError::AllocationFailure
                      ? ChartEditCode::AllocationFailure
                      : ChartEditCode::InvalidDraft;
    return false;
  }
  for (const auto& move : *projection.moves()) {
    if (row < move.range_start || row >= move.range_end) {
      continue;
    }
    if (!move.is_last_in_sequence || row < move.range_end - 8U) {
      error = ChartEditCode::ShowtimeRangeOccupied;
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool drill_has_npc_end(const MutableDraft& draft,
                                     ChartDanceMoveDrillId drill_id) noexcept {
  for (const auto& event : draft.role_events) {
    if (event.role == ChartRole::Npc &&
        event.dance_move_drill_id.value == drill_id.value &&
        event.type == ChartRoleEventType::MoveEnd) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool drill_is_owned_by_showtime(
    const MutableDraft& draft, ChartDanceMoveDrillId drill_id) noexcept {
  for (const auto& showtime : draft.showtimes) {
    for (const auto id : showtime.dance_move_drill_ids) {
      if (id.value == drill_id.value) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] ChartEditResult add_npc_start(
    MutableDraft& draft, const std::vector<ChartMarker>& markers,
    std::uint64_t row, IdState& ids) {
  std::uint64_t event_id = 0U;
  std::uint64_t group_id = 0U;
  if (!allocate_id(ids, event_id) || !allocate_id(ids, group_id)) {
    return {ChartEditCode::IdExhausted, false, {0U}};
  }

  ChartDanceMoveDrillId drill_id{};
  const ChartRoleEvent* previous_npc_start = nullptr;
  for (auto marker = markers.rbegin(); marker != markers.rend(); ++marker) {
    if (marker->row >= row || marker->type != ChartMarkerType::NpcBlockStart) {
      continue;
    }
    previous_npc_start = find_role_event(draft, marker->id.value);
    break;
  }
  if (previous_npc_start != nullptr &&
      find_drill(draft, previous_npc_start->dance_move_drill_id.value) !=
          nullptr &&
      !drill_is_owned_by_showtime(
          draft, previous_npc_start->dance_move_drill_id) &&
      !drill_has_npc_end(draft,
                         previous_npc_start->dance_move_drill_id)) {
    drill_id = previous_npc_start->dance_move_drill_id;
  } else {
    std::uint64_t value = 0U;
    if (!allocate_id(ids, value)) {
      return {ChartEditCode::IdExhausted, false, {0U}};
    }
    drill_id = {value};
    draft.dance_move_drills.push_back({drill_id, 0U, {}});
  }

  draft.drill_groups.push_back(
      {{group_id}, drill_id, {{event_id}}, {}});
  auto* drill = find_drill(draft, drill_id.value);
  if (drill == nullptr) {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }
  drill->drill_group_ids.push_back({group_id});
  draft.role_events.push_back({{event_id}, row, ChartRole::Npc,
                               ChartRoleEventType::BlockStart, {group_id},
                               drill_id});
  sort_role_events(draft);
  sort_drill_group_ids(draft, *drill);
  return {ChartEditCode::Success, true, {event_id}};
}

[[nodiscard]] ChartEditResult add_npc_action(
    MutableDraft& draft, const ChartMarker& previous, std::uint64_t row,
    ChartRoleEventType type, IdState& ids) {
  const auto* source = find_role_event(draft, previous.id.value);
  if (source == nullptr || source->role != ChartRole::Npc ||
      find_group(draft, source->drill_group_id.value) == nullptr ||
      find_drill(draft, source->dance_move_drill_id.value) == nullptr) {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }
  const auto group_id = source->drill_group_id;
  const auto drill_id = source->dance_move_drill_id;
  std::uint64_t event_id = 0U;
  if (!allocate_id(ids, event_id)) {
    return {ChartEditCode::IdExhausted, false, {0U}};
  }
  draft.role_events.push_back({{event_id}, row, ChartRole::Npc, type,
                               group_id, drill_id});
  auto* group = find_group(draft, group_id.value);
  group->npc_event_ids.push_back({event_id});
  sort_role_events(draft);
  sort_relation_ids(draft, group->npc_event_ids);
  recompute_move_n_count(draft, drill_id);
  return {ChartEditCode::Success, true, {event_id}};
}

[[nodiscard]] ChartEditResult click_empty_npc(
    const ChartDraft& original, MutableDraft& draft,
    const std::vector<ChartMarker>& markers, std::uint64_t row,
    IdState& ids) {
  const auto* previous = previous_chart_marker(markers, row);
  ChartRoleEventType type = ChartRoleEventType::BlockStart;
  if (previous == nullptr || previous->column != ChartColumn::Npc) {
    ChartEditCode error = ChartEditCode::Success;
    if (!npc_start_allowed(original, row, error)) {
      return {error, false, {0U}};
    }
    return add_npc_start(draft, markers, row, ids);
  }
  if (previous->type == ChartMarkerType::NpcBlockStart) {
    type = ChartRoleEventType::Note;
  } else if (previous->type == ChartMarkerType::NpcMoveEnd) {
    return {ChartEditCode::CannotAppendAfterEnd, false, {0U}};
  } else if (previous->type == ChartMarkerType::NpcNote) {
    const auto* next = next_chart_marker(markers, row);
    if (next != nullptr &&
        (next->type == ChartMarkerType::NpcNote ||
         next->type == ChartMarkerType::NpcMoveEnd)) {
      type = ChartRoleEventType::Note;
    } else {
      std::uint64_t count = 0U;
      for (auto marker = markers.rbegin(); marker != markers.rend(); ++marker) {
        if (marker->row >= row || marker->column != ChartColumn::Npc) {
          continue;
        }
        if (marker->type == ChartMarkerType::NpcMoveEnd) {
          break;
        }
        if (marker->type == ChartMarkerType::NpcNote) {
          ++count;
        }
      }
      type = count < 11U ? ChartRoleEventType::Note
                         : ChartRoleEventType::MoveEnd;
    }
  } else {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }
  return add_npc_action(draft, *previous, row, type, ids);
}

[[nodiscard]] ChartEditResult click_existing_npc(
    MutableDraft& draft, const std::vector<ChartMarker>& markers,
    const ChartMarker& current) {
  if (current.type == ChartMarkerType::NpcBlockStart) {
    std::vector<std::uint64_t> deleted{current.id.value};
    bool after_current = false;
    for (const auto& marker : markers) {
      if (!after_current) {
        after_current = marker.id.value == current.id.value;
        continue;
      }
      if (marker.type != ChartMarkerType::NpcNote &&
          marker.type != ChartMarkerType::NpcMoveEnd) {
        break;
      }
      deleted.push_back(marker.id.value);
    }
    erase_role_events(draft, deleted);
  } else if (current.type == ChartMarkerType::NpcNote) {
    auto* event = find_role_event(draft, current.id.value);
    if (event == nullptr) {
      return {ChartEditCode::InvalidDraft, false, {0U}};
    }
    event->type = ChartRoleEventType::MoveEnd;
    recompute_move_n_count(draft, event->dance_move_drill_id);
  } else if (current.type == ChartMarkerType::NpcMoveEnd) {
    erase_role_events(draft, {current.id.value});
  } else {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }
  return {ChartEditCode::Success, true, current.id};
}

[[nodiscard]] ChartEditResult add_player_start(MutableDraft& draft,
                                               const ChartMarker& previous,
                                               std::uint64_t row,
                                               IdState& ids) {
  const auto* source = find_role_event(draft, previous.id.value);
  if (source == nullptr || source->role != ChartRole::Npc) {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }
  auto* group = find_group(draft, source->drill_group_id.value);
  if (group == nullptr ||
      find_drill(draft, source->dance_move_drill_id.value) == nullptr) {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }
  std::uint64_t event_id = 0U;
  if (!allocate_id(ids, event_id)) {
    return {ChartEditCode::IdExhausted, false, {0U}};
  }
  draft.role_events.push_back({{event_id}, row, ChartRole::Player,
                               ChartRoleEventType::BlockStart,
                               source->drill_group_id,
                               source->dance_move_drill_id});
  group->player_event_ids.push_back({event_id});
  sort_role_events(draft);
  sort_relation_ids(draft, group->player_event_ids);
  return {ChartEditCode::Success, true, {event_id}};
}

[[nodiscard]] ChartEditResult generate_player_actions(
    MutableDraft& draft, const std::vector<ChartMarker>& markers,
    const ChartMarker& player_start, std::uint64_t clicked_row, IdState& ids) {
  const auto* player_c = find_role_event(draft, player_start.id.value);
  if (player_c == nullptr || player_c->role != ChartRole::Player ||
      player_c->type != ChartRoleEventType::BlockStart) {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }
  std::size_t npc_c_index = markers.size();
  for (std::size_t index = 0U; index < markers.size(); ++index) {
    if (markers[index].row >= player_start.row) {
      break;
    }
    if (markers[index].type == ChartMarkerType::NpcBlockStart) {
      npc_c_index = index;
    }
  }
  if (npc_c_index == markers.size()) {
    return {ChartEditCode::NoNpcSource, false, {0U}};
  }
  std::size_t source_index = markers.size();
  for (std::size_t index = npc_c_index + 1U; index < markers.size(); ++index) {
    if (markers[index].row >= player_start.row) {
      break;
    }
    if (markers[index].type == ChartMarkerType::NpcNote) {
      source_index = index;
      break;
    }
  }
  if (source_index == markers.size()) {
    return {ChartEditCode::NoNpcSource, false, {0U}};
  }

  std::vector<const ChartMarker*> source;
  for (std::size_t index = source_index; index < markers.size(); ++index) {
    if (markers[index].type != ChartMarkerType::NpcNote &&
        markers[index].type != ChartMarkerType::NpcMoveEnd) {
      break;
    }
    source.push_back(&markers[index]);
  }
  if (source.empty()) {
    return {ChartEditCode::NoNpcSource, false, {0U}};
  }
  const auto source_row = source.front()->row;
  std::vector<std::uint64_t> target_rows;
  target_rows.reserve(source.size());
  for (const auto* marker : source) {
    const auto delta = marker->row - source_row;
    if (clicked_row > std::numeric_limits<std::uint64_t>::max() - delta) {
      return {ChartEditCode::RangeOverflow, false, {0U}};
    }
    target_rows.push_back(clicked_row + delta);
  }
  const auto target_end = target_rows.back();
  for (const auto& marker : markers) {
    if (marker.row >= clicked_row && marker.row <= target_end) {
      return {ChartEditCode::GenerationRangeOccupied, false, {0U}};
    }
  }

  auto* group = find_group(draft, player_c->drill_group_id.value);
  if (group == nullptr) {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }
  const auto group_id = player_c->drill_group_id;
  const auto drill_id = player_c->dance_move_drill_id;
  ChartEventId first_id{0U};
  for (std::size_t index = 0U; index < source.size(); ++index) {
    std::uint64_t event_id = 0U;
    if (!allocate_id(ids, event_id)) {
      return {ChartEditCode::IdExhausted, false, {0U}};
    }
    if (index == 0U) {
      first_id = {event_id};
    }
    const auto type = source[index]->type == ChartMarkerType::NpcNote
                          ? ChartRoleEventType::Note
                          : ChartRoleEventType::MoveEnd;
    draft.role_events.push_back({{event_id}, target_rows[index],
                                 ChartRole::Player, type, group_id, drill_id});
    group->player_event_ids.push_back({event_id});
  }
  sort_role_events(draft);
  sort_relation_ids(draft, group->player_event_ids);
  return {ChartEditCode::Success, true, first_id};
}

[[nodiscard]] ChartEditResult click_empty_player(
    MutableDraft& draft, const std::vector<ChartMarker>& markers,
    std::uint64_t row, IdState& ids) {
  const auto* previous = previous_chart_marker(markers, row);
  if (previous == nullptr) {
    return {ChartEditCode::CannotPlace, false, {0U}};
  }
  if (previous->column == ChartColumn::Npc) {
    return add_player_start(draft, *previous, row, ids);
  }
  if (previous->type == ChartMarkerType::PlayerBlockStart) {
    return generate_player_actions(draft, markers, *previous, row, ids);
  }
  return {ChartEditCode::CannotPlace, false, {0U}};
}

[[nodiscard]] ChartEditResult click_existing_player(
    MutableDraft& draft, const std::vector<ChartMarker>& markers,
    const ChartMarker& current) {
  if (current.type == ChartMarkerType::PlayerMoveEnd) {
    return {ChartEditCode::AutoGeneratedMarkerLocked, false, {0U}};
  }
  std::vector<std::uint64_t> deleted;
  if (current.type == ChartMarkerType::PlayerBlockStart) {
    deleted.push_back(current.id.value);
  } else if (current.type == ChartMarkerType::PlayerNote) {
    const auto* previous = previous_chart_marker(markers, current.row);
    if (previous == nullptr ||
        previous->type != ChartMarkerType::PlayerBlockStart) {
      return {ChartEditCode::AutoGeneratedMarkerLocked, false, {0U}};
    }
  } else {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }

  bool at_current = false;
  for (const auto& marker : markers) {
    if (!at_current) {
      at_current = marker.id.value == current.id.value;
      if (!at_current) {
        continue;
      }
    }
    if (marker.id.value != current.id.value &&
        marker.type != ChartMarkerType::PlayerNote &&
        marker.type != ChartMarkerType::PlayerMoveEnd) {
      break;
    }
    if (marker.id.value == current.id.value ||
        marker.type == ChartMarkerType::PlayerNote ||
        marker.type == ChartMarkerType::PlayerMoveEnd) {
      deleted.push_back(marker.id.value);
    }
  }
  std::sort(deleted.begin(), deleted.end());
  deleted.erase(std::unique(deleted.begin(), deleted.end()), deleted.end());
  erase_role_events(draft, deleted);
  return {ChartEditCode::Success, true, current.id};
}

struct BlockCheck final {
  bool valid;
  std::vector<const ChartRoleEvent*> events;
  std::uint64_t note_count;
  bool has_end;
};

[[nodiscard]] BlockCheck check_block(const MutableDraft& draft,
                                     const ChartDrillGroup& group,
                                     const std::vector<ChartEventId>& ids,
                                     ChartRole role) {
  BlockCheck result{true, {}, 0U, false};
  result.events.reserve(ids.size());
  for (const auto id : ids) {
    const auto* event = find_role_event_const(draft, id.value);
    if (event == nullptr || event->role != role ||
        event->drill_group_id.value != group.id.value ||
        event->dance_move_drill_id.value !=
            group.dance_move_drill_id.value) {
      result.valid = false;
      continue;
    }
    result.events.push_back(event);
  }
  if (result.events.size() < 2U ||
      result.events.front()->type != ChartRoleEventType::BlockStart) {
    result.valid = false;
    return result;
  }
  for (std::size_t index = 1U; index < result.events.size(); ++index) {
    const auto* event = result.events[index];
    if (result.events[index - 1U]->row >= event->row) {
      result.valid = false;
    }
    if (event->type == ChartRoleEventType::Note && !result.has_end) {
      ++result.note_count;
    } else if (event->type == ChartRoleEventType::MoveEnd &&
               index + 1U == result.events.size() && !result.has_end) {
      result.has_end = true;
    } else {
      result.valid = false;
    }
  }
  if (result.note_count == 0U) {
    result.valid = false;
  }
  return result;
}

[[nodiscard]] bool blocks_match(const BlockCheck& npc,
                                const BlockCheck& player) noexcept {
  if (!npc.valid || !player.valid ||
      npc.events.size() != player.events.size() ||
      npc.note_count != player.note_count || npc.has_end != player.has_end) {
    return false;
  }
  const auto npc_anchor = npc.events[1U]->row;
  const auto player_anchor = player.events[1U]->row;
  for (std::size_t index = 0U; index < npc.events.size(); ++index) {
    if (npc.events[index]->type != player.events[index]->type) {
      return false;
    }
    if (index > 0U &&
        npc.events[index]->row - npc_anchor !=
            player.events[index]->row - player_anchor) {
      return false;
    }
  }
  return true;
}

struct CompleteDrill final {
  ChartDanceMoveDrillId id;
  std::uint64_t first_row;
  std::uint64_t last_row;
  std::uint64_t move_n_count;
};

[[nodiscard]] bool complete_drill(const MutableDraft& draft,
                                  const ChartDanceMoveDrill& drill,
                                  CompleteDrill& result) {
  if (drill.drill_group_ids.empty() || drill.move_n_count == 0U ||
      drill.move_n_count > 11U) {
    return false;
  }
  std::uint64_t note_count = 0U;
  bool has_rows = false;
  bool has_relation_row = false;
  std::uint64_t relation_row = 0U;
  std::uint64_t first_row = 0U;
  std::uint64_t last_row = 0U;
  for (std::size_t index = 0U; index < drill.drill_group_ids.size(); ++index) {
    const auto* group =
        find_group_const(draft, drill.drill_group_ids[index].value);
    if (group == nullptr ||
        group->dance_move_drill_id.value != drill.id.value) {
      return false;
    }
    const auto npc =
        check_block(draft, *group, group->npc_event_ids, ChartRole::Npc);
    const auto player = check_block(draft, *group, group->player_event_ids,
                                    ChartRole::Player);
    const bool should_end = index + 1U == drill.drill_group_ids.size();
    if (!blocks_match(npc, player) || npc.has_end != should_end) {
      return false;
    }
    for (const auto* event : npc.events) {
      if (has_relation_row && relation_row >= event->row) {
        return false;
      }
      relation_row = event->row;
      has_relation_row = true;
    }
    for (const auto* event : player.events) {
      if (has_relation_row && relation_row >= event->row) {
        return false;
      }
      relation_row = event->row;
    }
    note_count += npc.note_count;
    for (const auto* event : npc.events) {
      first_row = !has_rows ? event->row : std::min(first_row, event->row);
      last_row = !has_rows ? event->row : std::max(last_row, event->row);
      has_rows = true;
    }
    for (const auto* event : player.events) {
      first_row = std::min(first_row, event->row);
      last_row = std::max(last_row, event->row);
    }
  }
  if (!has_rows || note_count != drill.move_n_count) {
    return false;
  }
  result = {drill.id, first_row, last_row, drill.move_n_count};
  return true;
}

[[nodiscard]] std::vector<CompleteDrill> collect_complete_drills(
    const MutableDraft& draft, const std::vector<ChartMarker>& markers,
    std::uint64_t showtime_start_row) {
  bool has_previous_showtime_move = false;
  std::uint64_t previous_showtime_move_row = 0U;
  for (const auto& marker : markers) {
    if (marker.row >= showtime_start_row) {
      break;
    }
    if (marker.type == ChartMarkerType::ShowtimeMove) {
      previous_showtime_move_row = marker.row;
      has_previous_showtime_move = true;
    }
  }

  std::vector<CompleteDrill> result;
  for (const auto& drill : draft.dance_move_drills) {
    CompleteDrill candidate{};
    if (!complete_drill(draft, drill, candidate) ||
        candidate.last_row >= showtime_start_row ||
        (has_previous_showtime_move &&
         candidate.first_row <= previous_showtime_move_row)) {
      continue;
    }
    result.push_back(candidate);
  }
  std::sort(result.begin(), result.end(),
            [](const CompleteDrill& left, const CompleteDrill& right) {
              return left.first_row != right.first_row
                         ? left.first_row < right.first_row
                         : left.id.value < right.id.value;
            });
  return result;
}

[[nodiscard]] ChartEditResult add_showtime_start(MutableDraft& draft,
                                                 std::uint64_t row,
                                                 IdState& ids) {
  std::uint64_t event_id = 0U;
  std::uint64_t showtime_id = 0U;
  if (!allocate_id(ids, event_id) || !allocate_id(ids, showtime_id)) {
    return {ChartEditCode::IdExhausted, false, {0U}};
  }
  draft.showtime_start_events.push_back(
      {{event_id}, row, {showtime_id}});
  draft.showtimes.push_back({{showtime_id}, {event_id}, {}, {}});
  std::sort(draft.showtime_start_events.begin(),
            draft.showtime_start_events.end(),
            [](const ChartShowtimeStartEvent& left,
               const ChartShowtimeStartEvent& right) {
              return left.row != right.row ? left.row < right.row
                                           : left.id.value < right.id.value;
            });
  return {ChartEditCode::Success, true, {event_id}};
}

[[nodiscard]] ChartEditResult complete_showtime(
    MutableDraft& draft, const std::vector<ChartMarker>& markers,
    ChartShowtimeId showtime_id, std::uint64_t showtime_start_row,
    ChartEventId showtime_start_event_id, std::uint64_t first_row,
    IdState& ids) {
  auto* showtime = find_showtime(draft, showtime_id.value);
  if (showtime == nullptr ||
      showtime->start_event_id.value != showtime_start_event_id.value ||
      showtime->show_move_event_ids.size() >
          showtime->dance_move_drill_ids.size()) {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }
  const auto drills =
      collect_complete_drills(draft, markers, showtime_start_row);
  if (showtime->dance_move_drill_ids.size() > drills.size()) {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }
  for (std::size_t index = 0U; index < showtime->dance_move_drill_ids.size();
       ++index) {
    if (showtime->dance_move_drill_ids[index].value !=
        drills[index].id.value) {
      return {ChartEditCode::InvalidDraft, false, {0U}};
    }
  }

  for (std::size_t index = 0U; index < showtime->show_move_event_ids.size();
       ++index) {
    const auto event_id = showtime->show_move_event_ids[index].value;
    const auto event = std::find_if(
        draft.showtime_move_events.begin(), draft.showtime_move_events.end(),
        [event_id](const ChartShowtimeMoveEvent& candidate) {
          return candidate.id.value == event_id;
        });
    if (event == draft.showtime_move_events.end() ||
        event->showtime_id.value != showtime_id.value ||
        event->dance_move_drill_id.value !=
            showtime->dance_move_drill_ids[index].value) {
      return {ChartEditCode::InvalidDraft, false, {0U}};
    }
  }

  for (std::size_t index = showtime->dance_move_drill_ids.size();
       index < drills.size(); ++index) {
    showtime->dance_move_drill_ids.push_back(drills[index].id);
  }

  const auto begin = showtime->show_move_event_ids.size();
  if (begin == drills.size()) {
    return {ChartEditCode::NoRemainingDanceMoves, false, {0U}};
  }

  struct PendingMove final {
    ChartDanceMoveDrillId drill_id;
    std::uint64_t row;
    std::uint64_t show_n_count;
    std::uint64_t range_start;
    std::uint64_t range_end;
  };
  std::vector<PendingMove> pending;
  pending.reserve(drills.size() - begin);
  std::uint64_t row = first_row;
  for (std::size_t index = begin; index < drills.size(); ++index) {
    const auto range_start = row - row % 16U;
    const std::uint64_t duration = drills[index].move_n_count <= 6U ? 16U : 32U;
    if (range_start >
        std::numeric_limits<std::uint64_t>::max() - duration) {
      return {ChartEditCode::RangeOverflow, false, {0U}};
    }
    const auto range_end = range_start + duration;
    pending.push_back(
        {drills[index].id, row, drills[index].move_n_count, range_start,
         range_end});
    row = range_end;
  }

  const auto range_begin = pending.front().range_start;
  const auto range_end = pending.back().range_end;
  const auto open_begin = range_end - 8U;
  for (const auto& marker : markers) {
    if (marker.row < range_begin || marker.row >= range_end) {
      continue;
    }
    if (marker.id.value == showtime_start_event_id.value) {
      continue;
    }
    if (marker.type == ChartMarkerType::NpcBlockStart &&
        marker.row >= open_begin) {
      continue;
    }
    return {ChartEditCode::ShowtimeRangeOccupied, false, {0U}};
  }

  ChartEventId first_event{0U};
  for (std::size_t index = 0U; index < pending.size(); ++index) {
    std::uint64_t event_id = 0U;
    if (!allocate_id(ids, event_id)) {
      return {ChartEditCode::IdExhausted, false, {0U}};
    }
    if (index == 0U) {
      first_event = {event_id};
    }
    draft.showtime_move_events.push_back(
        {{event_id}, pending[index].row, showtime_id, pending[index].drill_id,
         pending[index].show_n_count});
    showtime->show_move_event_ids.push_back({event_id});
  }
  std::sort(draft.showtime_move_events.begin(),
            draft.showtime_move_events.end(),
            [](const ChartShowtimeMoveEvent& left,
               const ChartShowtimeMoveEvent& right) {
              return left.row != right.row ? left.row < right.row
                                           : left.id.value < right.id.value;
            });
  return {ChartEditCode::Success, true, first_event};
}

[[nodiscard]] const ChartShowtimeStartEvent* find_showtime_start_event(
    const MutableDraft& draft, ChartEventId id) noexcept {
  for (const auto& event : draft.showtime_start_events) {
    if (event.id.value == id.value) {
      return &event;
    }
  }
  return nullptr;
}

[[nodiscard]] const ChartShowtimeMoveEvent* find_showtime_move_event(
    const MutableDraft& draft, ChartEventId id) noexcept {
  for (const auto& event : draft.showtime_move_events) {
    if (event.id.value == id.value) {
      return &event;
    }
  }
  return nullptr;
}

[[nodiscard]] ChartEditResult click_empty_showtime(
    MutableDraft& draft, const std::vector<ChartMarker>& markers,
    std::uint64_t row, IdState& ids) {
  const auto* previous = previous_chart_marker(markers, row);
  bool created_start = false;
  ChartEventId start_event_id{0U};
  ChartShowtimeId showtime_id{0U};
  std::uint64_t showtime_start_row = 0U;
  std::uint64_t first_move_row = row;

  if (previous != nullptr &&
      previous->type == ChartMarkerType::PlayerMoveEnd) {
    const auto add = add_showtime_start(draft, row, ids);
    if (!add) {
      return add;
    }
    created_start = true;
    start_event_id = add.primary_event_id;
    const auto* event = find_showtime_start_event(draft, start_event_id);
    if (event == nullptr) {
      return {ChartEditCode::InvalidDraft, false, {0U}};
    }
    showtime_id = event->showtime_id;
    showtime_start_row = event->row;
    if (row % 16U == 0U) {
      if (row == std::numeric_limits<std::uint64_t>::max()) {
        return {ChartEditCode::RangeOverflow, false, {0U}};
      }
      first_move_row = row + 1U;
    } else {
      const auto measure = row / 16U;
      if (measure == std::numeric_limits<std::uint64_t>::max() / 16U) {
        return {ChartEditCode::RangeOverflow, false, {0U}};
      }
      first_move_row = (measure + 1U) * 16U;
    }
  } else if (previous != nullptr &&
             (previous->type == ChartMarkerType::ShowtimeStart ||
              previous->type == ChartMarkerType::ShowtimeMove)) {
    if (previous->type == ChartMarkerType::ShowtimeStart) {
      const auto* event = find_showtime_start_event(draft, previous->id);
      if (event == nullptr) {
        return {ChartEditCode::InvalidDraft, false, {0U}};
      }
      start_event_id = event->id;
      showtime_id = event->showtime_id;
      showtime_start_row = event->row;
      const bool valid = event->row % 16U == 0U
                             ? row % 16U == 0U || row % 16U == 1U
                             : row % 16U == 0U;
      if (!valid) {
        return {ChartEditCode::InvalidShowtimePosition, false, {0U}};
      }
    } else {
      const auto* event = find_showtime_move_event(draft, previous->id);
      if (event == nullptr || row % 16U != 0U) {
        return {ChartEditCode::InvalidShowtimePosition, false, {0U}};
      }
      const auto ranges = project_showtime_moves(
          ChartDraft{draft.role_events, draft.showtime_start_events,
                     draft.showtime_move_events, draft.drill_groups,
                     draft.dance_move_drills, draft.showtimes});
      if (!ranges) {
        return {ranges.error() == ChartProjectionError::RangeOverflow
                    ? ChartEditCode::RangeOverflow
                    : ChartEditCode::InvalidDraft,
                false,
                {0U}};
      }
      bool found = false;
      for (const auto& move : *ranges.moves()) {
        if (move.event_id.value == event->id.value) {
          if (row < move.range_end) {
            return {ChartEditCode::InvalidShowtimePosition, false, {0U}};
          }
          found = true;
          break;
        }
      }
      if (!found) {
        return {ChartEditCode::InvalidDraft, false, {0U}};
      }
      showtime_id = event->showtime_id;
      const auto* showtime = find_showtime(draft, showtime_id.value);
      if (showtime == nullptr) {
        return {ChartEditCode::InvalidDraft, false, {0U}};
      }
      start_event_id = showtime->start_event_id;
      const auto* start = find_showtime_start_event(draft, start_event_id);
      if (start == nullptr) {
        return {ChartEditCode::InvalidDraft, false, {0U}};
      }
      showtime_start_row = start->row;
    }
  } else {
    return {ChartEditCode::CannotPlace, false, {0U}};
  }

  const auto completion =
      complete_showtime(draft, markers, showtime_id, showtime_start_row,
                        start_event_id, first_move_row, ids);
  if (completion.changed) {
    return completion;
  }
  if (created_start &&
      (completion.code == ChartEditCode::NoRemainingDanceMoves ||
       completion.code == ChartEditCode::ShowtimeRangeOccupied)) {
    return {completion.code, true, start_event_id};
  }
  return completion;
}

void erase_showtime_events(MutableDraft& draft,
                           const std::vector<std::uint64_t>& ids) {
  const auto erased = [&ids](ChartEventId id) {
    return std::find(ids.begin(), ids.end(), id.value) != ids.end();
  };
  draft.showtime_start_events.erase(
      std::remove_if(draft.showtime_start_events.begin(),
                     draft.showtime_start_events.end(),
                     [&ids](const ChartShowtimeStartEvent& event) {
                       return std::find(ids.begin(), ids.end(), event.id.value) !=
                              ids.end();
                     }),
      draft.showtime_start_events.end());
  draft.showtime_move_events.erase(
      std::remove_if(draft.showtime_move_events.begin(),
                     draft.showtime_move_events.end(),
                     [&ids](const ChartShowtimeMoveEvent& event) {
                       return std::find(ids.begin(), ids.end(), event.id.value) !=
                              ids.end();
                     }),
      draft.showtime_move_events.end());

  for (auto& showtime : draft.showtimes) {
    if (erased(showtime.start_event_id)) {
      showtime.start_event_id = {0U};
    }
    std::vector<ChartEventId> kept_events;
    kept_events.reserve(showtime.show_move_event_ids.size());
    for (const auto event_id : showtime.show_move_event_ids) {
      if (!erased(event_id)) {
        kept_events.push_back(event_id);
      }
    }
    showtime.show_move_event_ids = std::move(kept_events);
  }
  draft.showtimes.erase(
      std::remove_if(draft.showtimes.begin(), draft.showtimes.end(),
                     [](const ChartShowtime& showtime) {
                       return showtime.start_event_id.value == 0U &&
                              showtime.show_move_event_ids.empty();
                     }),
      draft.showtimes.end());
}

[[nodiscard]] ChartEditResult click_existing_showtime(
    MutableDraft& draft, const std::vector<ChartMarker>& markers,
    const ChartMarker& current) {
  if (current.type != ChartMarkerType::ShowtimeStart &&
      current.type != ChartMarkerType::ShowtimeMove) {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }
  std::vector<std::uint64_t> deleted{current.id.value};
  bool after_current = false;
  for (const auto& marker : markers) {
    if (!after_current) {
      after_current = marker.id.value == current.id.value;
      continue;
    }
    if (marker.type != ChartMarkerType::ShowtimeMove) {
      break;
    }
    deleted.push_back(marker.id.value);
  }
  erase_showtime_events(draft, deleted);
  return {ChartEditCode::Success, true, current.id};
}

}  // namespace

ChartEditor::ChartEditor(ChartDraft draft) noexcept
    : draft_(std::move(draft)), next_id_(0U), id_available_(false) {
  const auto ids = initial_id_state(draft_);
  next_id_ = ids.next;
  id_available_ = ids.available;
}

ChartEditResult ChartEditor::commit(ChartDraft draft, ChartEditResult result,
                                    std::uint64_t next_id,
                                    bool id_available) noexcept {
  try {
    undo_.push_back(draft_);
  } catch (const std::bad_alloc&) {
    return {ChartEditCode::AllocationFailure, false, {0U}};
  }
  draft_ = std::move(draft);
  redo_.clear();
  next_id_ = next_id;
  id_available_ = id_available;
  return result;
}

ChartEditResult ChartEditor::click(ChartColumn column,
                                   std::uint64_t row) noexcept {
  try {
    std::vector<ChartMarker> markers;
    ChartEditCode projection_error = ChartEditCode::Success;
    if (!editable_projection(draft_, markers, projection_error)) {
      return {projection_error, false, {0U}};
    }
    const auto* current = marker_at(markers, row);
    if (current != nullptr && current->column != column) {
      return {ChartEditCode::PositionOccupied, false, current->id};
    }

    MutableDraft candidate{draft_};
    IdState ids{next_id_, id_available_};
    ChartEditResult result{ChartEditCode::CannotPlace, false, {0U}};
    if (column == ChartColumn::Npc) {
      result = current == nullptr
                   ? click_empty_npc(draft_, candidate, markers, row, ids)
                   : click_existing_npc(candidate, markers, *current);
    } else if (column == ChartColumn::Player) {
      result = current == nullptr
                   ? click_empty_player(candidate, markers, row, ids)
                   : click_existing_player(candidate, markers, *current);
    } else if (column == ChartColumn::Showtime) {
      result = current == nullptr
                   ? click_empty_showtime(candidate, markers, row, ids)
                   : click_existing_showtime(candidate, markers, *current);
    } else {
      return {ChartEditCode::InvalidDraft, false, {0U}};
    }
    if (!result.changed) {
      return result;
    }
    return commit(std::move(candidate).finish(), result, ids.next,
                  ids.available);
  } catch (const std::bad_alloc&) {
    return {ChartEditCode::AllocationFailure, false, {0U}};
  }
}

ChartEditResult ChartEditor::clear() noexcept {
  if (draft_.role_events().empty() && draft_.showtime_start_events().empty() &&
      draft_.showtime_move_events().empty() && draft_.drill_groups().empty() &&
      draft_.dance_move_drills().empty() && draft_.showtimes().empty()) {
    return {ChartEditCode::NoChange, false, {0U}};
  }
  return commit(ChartDraft{{}, {}, {}, {}, {}, {}},
                {ChartEditCode::Success, true, {0U}}, next_id_,
                id_available_);
}

ChartEditResult ChartEditor::undo() noexcept {
  if (undo_.empty()) {
    return {ChartEditCode::NothingToUndo, false, {0U}};
  }
  try {
    redo_.push_back(draft_);
  } catch (const std::bad_alloc&) {
    return {ChartEditCode::AllocationFailure, false, {0U}};
  }
  draft_ = std::move(undo_.back());
  undo_.pop_back();
  return {ChartEditCode::Success, true, {0U}};
}

ChartEditResult ChartEditor::redo() noexcept {
  if (redo_.empty()) {
    return {ChartEditCode::NothingToRedo, false, {0U}};
  }
  try {
    undo_.push_back(draft_);
  } catch (const std::bad_alloc&) {
    return {ChartEditCode::AllocationFailure, false, {0U}};
  }
  draft_ = std::move(redo_.back());
  redo_.pop_back();
  return {ChartEditCode::Success, true, {0U}};
}

}  // namespace otgy::core
