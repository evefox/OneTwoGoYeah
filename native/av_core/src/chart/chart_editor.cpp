// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_editor.hpp"

#include <algorithm>
#include <cstddef>
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

  /// The single conversion from a working draft to a committable one: every
  /// draft-changing command ends here, so the cleanup that keeps a commit
  /// self-consistent cannot be bypassed. Defined after the helpers it needs.
  [[nodiscard]] ChartDraft finish() &&;
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

[[nodiscard]] const ChartRoleEvent* find_role_event_in(
    const ChartDraft& draft, std::uint64_t id) noexcept {
  for (const auto& event : draft.role_events()) {
    if (event.id.value == id) {
      return &event;
    }
  }
  return nullptr;
}

[[nodiscard]] const ChartDanceMoveDrill* find_drill_in(
    const ChartDraft& draft, std::uint64_t id) noexcept {
  for (const auto& drill : draft.dance_move_drills()) {
    if (drill.id.value == id) {
      return &drill;
    }
  }
  return nullptr;
}

[[nodiscard]] const ChartShowtimeMoveEvent* find_showtime_move_in(
    const ChartDraft& draft, std::uint64_t id) noexcept {
  for (const auto& event : draft.showtime_move_events()) {
    if (event.id.value == id) {
      return &event;
    }
  }
  return nullptr;
}

/// Derives the selectable row span of one dance move. This deliberately does
/// not reuse complete_drill: an editor draft may hold a dance move whose
/// MoveEnd, player block or move_n_count are still missing, and selection must
/// survive that. Membership comes from the drill reference carried by each role
/// event, so a group whose id list was emptied by an earlier deletion does not
/// break the selection, and a drill spanning several groups keeps one span.
[[nodiscard]] bool derive_selection(const ChartDraft& draft,
                                    ChartDanceMoveDrillId drill_id,
                                    ChartSelection& out) noexcept {
  if (find_drill_in(draft, drill_id.value) == nullptr) {
    return false;
  }
  bool has_row = false;
  std::uint64_t first_row = 0U;
  std::uint64_t last_row = 0U;
  for (const auto& event : draft.role_events()) {
    if (event.dance_move_drill_id.value != drill_id.value) {
      continue;
    }
    if (!has_row) {
      first_row = event.row;
      last_row = event.row;
      has_row = true;
    } else {
      first_row = std::min(first_row, event.row);
      last_row = std::max(last_row, event.row);
    }
  }
  if (!has_row) {
    return false;
  }
  out = {true, drill_id, first_row, last_row};
  return true;
}

[[nodiscard]] bool role_event_less(const ChartRoleEvent& left,
                                   const ChartRoleEvent& right) noexcept {
  return left.row != right.row ? left.row < right.row
                               : left.id.value < right.id.value;
}

[[nodiscard]] std::vector<ChartEventId> member_event_ids(
    const std::vector<ChartEventId>& ids,
    const std::vector<ChartRoleEvent>& members) {
  std::vector<ChartEventId> kept;
  for (const auto id : ids) {
    const bool member =
        std::any_of(members.begin(), members.end(),
                    [id](const ChartRoleEvent& event) {
                      return event.id.value == id.value;
                    });
    if (member) {
      kept.push_back(id);
    }
  }
  return kept;
}

[[nodiscard]] std::uint64_t member_first_row(
    const ChartDrillGroup& group,
    const std::vector<ChartRoleEvent>& members) noexcept {
  std::uint64_t first = std::numeric_limits<std::uint64_t>::max();
  const auto consider = [&first, &members](const std::vector<ChartEventId>& ids) {
    for (const auto id : ids) {
      for (const auto& event : members) {
        if (event.id.value == id.value) {
          first = std::min(first, event.row);
          break;
        }
      }
    }
  };
  consider(group.npc_event_ids);
  consider(group.player_event_ids);
  return first;
}

/// Builds the session clipboard payload for one dance move. The fragment holds
/// exactly the selected drill, the drill groups that exist for it and both
/// roles' bound events, with every row rebased so the smallest offset is zero.
/// Group and event order is rebuilt from the fragment itself, so the payload
/// stays deterministic even when the source draft's relation vectors are stale.
/// The three Showtime vectors stay empty: a Showtime's display range is derived
/// from the chart, not transportable content.
[[nodiscard]] bool build_fragment(const ChartDraft& draft,
                                  ChartDanceMoveDrillId drill_id,
                                  ChartClipboardFragment& out) {
  const auto* drill = find_drill_in(draft, drill_id.value);
  if (drill == nullptr) {
    return false;
  }
  std::vector<ChartRoleEvent> members;
  for (const auto& event : draft.role_events()) {
    if (event.dance_move_drill_id.value == drill_id.value) {
      members.push_back(event);
    }
  }
  if (members.empty()) {
    return false;
  }
  std::sort(members.begin(), members.end(), role_event_less);
  const std::uint64_t base_row = members.front().row;

  std::vector<ChartDrillGroupId> group_ids;
  const auto append_group_id = [&group_ids](ChartDrillGroupId id) {
    const bool known =
        std::any_of(group_ids.begin(), group_ids.end(),
                    [id](ChartDrillGroupId candidate) {
                      return candidate.value == id.value;
                    });
    if (!known) {
      group_ids.push_back(id);
    }
  };
  for (const auto id : drill->drill_group_ids) {
    append_group_id(id);
  }
  for (const auto& group : draft.drill_groups()) {
    if (group.dance_move_drill_id.value == drill_id.value) {
      append_group_id(group.id);
    }
  }

  struct CollectedGroup final {
    ChartDrillGroup group;
    std::uint64_t first_row;
  };
  std::vector<CollectedGroup> collected;
  for (const auto id : group_ids) {
    for (const auto& group : draft.drill_groups()) {
      if (group.id.value != id.value ||
          group.dance_move_drill_id.value != drill_id.value) {
        continue;
      }
      ChartDrillGroup copy = group;
      copy.dance_move_drill_id = drill_id;
      copy.npc_event_ids = member_event_ids(group.npc_event_ids, members);
      copy.player_event_ids = member_event_ids(group.player_event_ids, members);
      const auto first_row = member_first_row(copy, members);
      collected.push_back({std::move(copy), first_row});
      break;
    }
  }
  std::sort(collected.begin(), collected.end(),
            [](const CollectedGroup& left, const CollectedGroup& right) {
              return left.first_row != right.first_row
                         ? left.first_row < right.first_row
                         : left.group.id.value < right.group.id.value;
            });

  std::vector<ChartRoleEvent> events;
  events.reserve(members.size());
  for (const auto& event : members) {
    ChartRoleEvent copy = event;
    copy.row = event.row - base_row;
    events.push_back(copy);
  }
  std::vector<ChartDrillGroup> groups;
  std::vector<ChartDrillGroupId> ordered_group_ids;
  groups.reserve(collected.size());
  ordered_group_ids.reserve(collected.size());
  for (auto& entry : collected) {
    ordered_group_ids.push_back(entry.group.id);
    groups.push_back(std::move(entry.group));
  }

  ChartDraft content{std::move(events),
                     {},
                     {},
                     std::move(groups),
                     std::vector<ChartDanceMoveDrill>{ChartDanceMoveDrill{
                         drill_id, drill->move_n_count,
                         std::move(ordered_group_ids)}},
                     {}};
  out = {base_row, std::move(content)};
  return true;
}

[[nodiscard]] bool selection_less(const ChartSelection& left,
                                   const ChartSelection& right) noexcept {
  return left.first_row != right.first_row
             ? left.first_row < right.first_row
             : left.drill_id.value < right.drill_id.value;
}

[[nodiscard]] bool selected_drill(const std::vector<ChartSelection>& selections,
                                  ChartDanceMoveDrillId id) noexcept {
  return std::any_of(selections.begin(), selections.end(),
                     [id](const ChartSelection& item) {
                       return item.drill_id.value == id.value;
                     });
}

/// Keep raw source values separately from the normalized transport fragment:
/// relation repairs in the latter must never hide a changed pending-cut source.
[[nodiscard]] ChartDraft snapshot_selection(
    const ChartDraft& draft, const std::vector<ChartSelection>& selections) {
  std::vector<ChartRoleEvent> events;
  std::vector<ChartDrillGroup> groups;
  std::vector<ChartDanceMoveDrill> drills;
  for (const auto& event : draft.role_events()) {
    if (selected_drill(selections, event.dance_move_drill_id)) {
      events.push_back(event);
    }
  }
  for (const auto& group : draft.drill_groups()) {
    if (selected_drill(selections, group.dance_move_drill_id)) {
      groups.push_back(group);
    }
  }
  for (const auto& drill : draft.dance_move_drills()) {
    if (selected_drill(selections, drill.id)) {
      drills.push_back(drill);
    }
  }
  const auto by_id = [](const auto& left, const auto& right) {
    return left.id.value < right.id.value;
  };
  std::sort(events.begin(), events.end(), by_id);
  std::sort(groups.begin(), groups.end(), by_id);
  std::sort(drills.begin(), drills.end(), by_id);
  return {std::move(events), {}, {}, std::move(groups), std::move(drills), {}};
}

[[nodiscard]] bool build_fragments(
    const ChartDraft& draft, const std::vector<ChartSelection>& selections,
    ChartClipboardFragment& out) {
  if (selections.empty()) {
    return false;
  }
  std::uint64_t base = std::numeric_limits<std::uint64_t>::max();
  for (const auto& item : selections) {
    ChartSelection derived{};
    if (!derive_selection(draft, item.drill_id, derived)) {
      return false;
    }
    base = std::min(base, derived.first_row);
  }
  std::vector<ChartRoleEvent> events;
  std::vector<ChartDrillGroup> groups;
  std::vector<ChartDanceMoveDrill> drills;
  for (const auto& item : selections) {
    ChartClipboardFragment fragment{};
    if (!build_fragment(draft, item.drill_id, fragment)) {
      return false;
    }
    for (auto event : fragment.content.role_events()) {
      event.row += fragment.base_row - base;
      events.push_back(event);
    }
    groups.insert(groups.end(), fragment.content.drill_groups().begin(),
                  fragment.content.drill_groups().end());
    drills.insert(drills.end(), fragment.content.dance_move_drills().begin(),
                  fragment.content.dance_move_drills().end());
  }
  std::sort(events.begin(), events.end(), role_event_less);
  out = {base, ChartDraft{std::move(events), {}, {}, std::move(groups),
                          std::move(drills), {}}};
  return true;
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

[[nodiscard]] bool merge_drill_into(MutableDraft& draft,
                                    ChartDanceMoveDrillId destination_id,
                                    ChartDanceMoveDrillId source_id) {
  if (destination_id.value == source_id.value) {
    return true;
  }
  auto* destination = find_drill(draft, destination_id.value);
  const auto* source = find_drill(draft, source_id.value);
  if (destination == nullptr || source == nullptr) {
    return false;
  }
  const auto source_groups = source->drill_group_ids;
  for (const auto group_id : source_groups) {
    const auto* group = find_group_const(draft, group_id.value);
    if (group == nullptr ||
        group->dance_move_drill_id.value != source_id.value) {
      return false;
    }
  }
  for (const auto& group : draft.drill_groups) {
    if (group.dance_move_drill_id.value == source_id.value &&
        std::none_of(source_groups.begin(), source_groups.end(),
                     [&group](ChartDrillGroupId id) {
                       return id.value == group.id.value;
                     })) {
      return false;
    }
  }
  for (const auto& event : draft.role_events) {
    const bool belongs_to_source_group =
        std::any_of(source_groups.begin(), source_groups.end(),
                    [&event](ChartDrillGroupId id) {
                      return id.value == event.drill_group_id.value;
                    });
    const bool references_source_drill =
        event.dance_move_drill_id.value == source_id.value;
    if (belongs_to_source_group != references_source_drill) {
      return false;
    }
  }

  destination = find_drill(draft, destination_id.value);
  destination->drill_group_ids.insert(destination->drill_group_ids.end(),
                                      source_groups.begin(), source_groups.end());
  for (const auto group_id : source_groups) {
    auto* group = find_group(draft, group_id.value);
    group->dance_move_drill_id = destination_id;
    for (auto& event : draft.role_events) {
      if (event.drill_group_id.value == group_id.value) {
        event.dance_move_drill_id = destination_id;
      }
    }
  }
  draft.dance_move_drills.erase(
      std::remove_if(draft.dance_move_drills.begin(),
                     draft.dance_move_drills.end(),
                     [source_id](const ChartDanceMoveDrill& drill) {
                       return drill.id.value == source_id.value;
                     }),
      draft.dance_move_drills.end());
  destination = find_drill(draft, destination_id.value);
  if (destination == nullptr) {
    return false;
  }
  sort_drill_group_ids(draft, *destination);
  recompute_move_n_count(draft, destination_id);
  return true;
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

[[nodiscard]] bool npc_move_end_between(const MutableDraft& draft,
                                        std::uint64_t begin_row,
                                        std::uint64_t end_row) noexcept {
  for (const auto& event : draft.role_events) {
    if (event.role == ChartRole::Npc &&
        event.type == ChartRoleEventType::MoveEnd &&
        event.row > begin_row && event.row < end_row) {
      return true;
    }
  }
  return false;
}

/// Whether the group already ends its move. A group may carry an NPC-S later
/// than the row a new NPC-C is placed at, because a segment can be created
/// inside an already finished group's span. Appending a further group to the
/// same drill would leave that end in a non-final group, so the new C must
/// start its own drill instead.
[[nodiscard]] bool group_has_npc_end(const MutableDraft& draft,
                                     ChartDrillGroupId group_id) noexcept {
  for (const auto& event : draft.role_events) {
    if (event.role == ChartRole::Npc &&
        event.type == ChartRoleEventType::MoveEnd &&
        event.drill_group_id.value == group_id.value) {
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
  const auto* previous_group =
      previous_npc_start == nullptr
          ? nullptr
          : find_group_const(draft,
                             previous_npc_start->drill_group_id.value);
  if (previous_npc_start != nullptr && previous_group != nullptr &&
      find_drill(draft, previous_npc_start->dance_move_drill_id.value) !=
          nullptr &&
      !drill_is_owned_by_showtime(
          draft, previous_npc_start->dance_move_drill_id) &&
      !group_has_npc_end(draft, previous_group->id) &&
      !npc_move_end_between(draft, previous_npc_start->row, row)) {
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

/// Ends the current group of a drill and moves every later group of that drill
/// into a new DanceMoveDrill. Both creating an NPC-S from an existing N and the
/// empty-click auto-end branch must use this same transition.
[[nodiscard]] ChartEditCode split_trailing_drill_groups(
    MutableDraft& draft, ChartDanceMoveDrillId drill_id,
    ChartDrillGroupId current_group_id, IdState& ids) {
  auto* drill = find_drill(draft, drill_id.value);
  if (drill == nullptr) {
    return ChartEditCode::InvalidDraft;
  }
  sort_drill_group_ids(draft, *drill);
  const auto group = std::find_if(
      drill->drill_group_ids.begin(), drill->drill_group_ids.end(),
      [current_group_id](ChartDrillGroupId id) {
        return id.value == current_group_id.value;
      });
  if (group == drill->drill_group_ids.end()) {
    return ChartEditCode::InvalidDraft;
  }
  const auto trailing = group + 1;
  if (trailing == drill->drill_group_ids.end()) {
    return ChartEditCode::Success;
  }
  std::uint64_t split_id = 0U;
  if (!allocate_id(ids, split_id)) {
    return ChartEditCode::IdExhausted;
  }
  std::vector<ChartDrillGroupId> trailing_groups(trailing,
                                                drill->drill_group_ids.end());
  drill->drill_group_ids.erase(trailing, drill->drill_group_ids.end());
  for (const auto trailing_group_id : trailing_groups) {
    auto* trailing_group = find_group(draft, trailing_group_id.value);
    if (trailing_group == nullptr) {
      return ChartEditCode::InvalidDraft;
    }
    trailing_group->dance_move_drill_id = {split_id};
    for (auto& member : draft.role_events) {
      if (member.drill_group_id.value == trailing_group_id.value) {
        member.dance_move_drill_id = {split_id};
      }
    }
  }
  draft.dance_move_drills.push_back(
      {{split_id}, 0U, std::move(trailing_groups)});
  recompute_move_n_count(draft, {split_id});
  return ChartEditCode::Success;
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
  if (type == ChartRoleEventType::MoveEnd) {
    const auto* source = find_role_event(draft, previous->id.value);
    if (source == nullptr) {
      return {ChartEditCode::InvalidDraft, false, {0U}};
    }
    const auto split = split_trailing_drill_groups(
        draft, source->dance_move_drill_id, source->drill_group_id, ids);
    if (split != ChartEditCode::Success) {
      return {split, false, {0U}};
    }
  }
  return add_npc_action(draft, *previous, row, type, ids);
}

[[nodiscard]] ChartEditResult click_existing_npc(
    MutableDraft& draft, const std::vector<ChartMarker>& markers,
    const ChartMarker& current, IdState& ids) {
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
    const auto drill_id = event->dance_move_drill_id;
    const auto group_id = event->drill_group_id;
    const auto split = split_trailing_drill_groups(
        draft, drill_id, group_id, ids);
    if (split != ChartEditCode::Success) {
      return {split, false, {0U}};
    }
    event = find_role_event(draft, current.id.value);
    if (event == nullptr) {
      return {ChartEditCode::InvalidDraft, false, {0U}};
    }
    event->type = ChartRoleEventType::MoveEnd;
    recompute_move_n_count(draft, drill_id);
  } else if (current.type == ChartMarkerType::NpcMoveEnd) {
    const auto* event = find_role_event_const(draft, current.id.value);
    if (event == nullptr) {
      return {ChartEditCode::InvalidDraft, false, {0U}};
    }
    const auto destination_id = event->dance_move_drill_id;
    const auto current_group_id = event->drill_group_id;
    auto* destination = find_drill(draft, destination_id.value);
    if (destination == nullptr) {
      return {ChartEditCode::InvalidDraft, false, {0U}};
    }
    sort_drill_group_ids(draft, *destination);
    const bool is_last_group =
        !destination->drill_group_ids.empty() &&
        destination->drill_group_ids.back().value == current_group_id.value;

    const auto current_row = group_first_row(draft, current_group_id);
    const ChartDrillGroup* next_group = nullptr;
    std::uint64_t next_row = std::numeric_limits<std::uint64_t>::max();
    for (const auto& candidate : draft.drill_groups) {
      const auto candidate_row = group_first_row(draft, candidate.id);
      if (candidate_row > current_row && candidate_row < next_row) {
        next_group = &candidate;
        next_row = candidate_row;
      }
    }
    const bool has_sequence_barrier =
        next_group != nullptr &&
        std::any_of(markers.begin(), markers.end(),
                    [&draft, current, current_group_id,
                     next_row](const ChartMarker& marker) {
                      if (marker.row <= current.row ||
                          marker.row >= next_row) {
                        return false;
                      }
                      if (marker.column == ChartColumn::Showtime) {
                        return true;
                      }
                      if (marker.column != ChartColumn::Npc ||
                          marker.type != ChartMarkerType::NpcMoveEnd) {
                        return false;
                      }
                      const auto* event =
                          find_role_event_const(draft, marker.id.value);
                      return event == nullptr ||
                             event->drill_group_id.value !=
                                 current_group_id.value;
                    });
    // Deleting an end only concatenates the following move when it really opens
    // this move. Converting a note inside a move and converting it back is the
    // common cancel path: while the end existed the group kept the notes after
    // it, and when another end already exists the group still ends there.
    // Merging in either state would swallow the next dance move and make the
    // combined move impossible for Showtime completion.
    const bool destination_still_ends = std::any_of(
        destination->drill_group_ids.begin(),
        destination->drill_group_ids.end(),
        [&draft, current](ChartDrillGroupId group_id) {
          return std::any_of(
              draft.role_events.begin(), draft.role_events.end(),
              [current, group_id](const ChartRoleEvent& event) {
                return event.id.value != current.id.value &&
                       event.role == ChartRole::Npc &&
                       event.type == ChartRoleEventType::MoveEnd &&
                       event.drill_group_id.value == group_id.value;
              });
        });
    const bool group_has_later_note = std::any_of(
        draft.role_events.begin(), draft.role_events.end(),
        [current, current_group_id](const ChartRoleEvent& event) {
          return event.role == ChartRole::Npc &&
                 event.type == ChartRoleEventType::Note &&
                 event.drill_group_id.value == current_group_id.value &&
                 event.row > current.row;
        });
    if (is_last_group && next_group != nullptr && !has_sequence_barrier &&
        !destination_still_ends && !group_has_later_note &&
        next_group->dance_move_drill_id.value != destination_id.value) {
      const auto source_id = next_group->dance_move_drill_id;
      auto* source = find_drill(draft, source_id.value);
      if (source == nullptr) {
        return {ChartEditCode::InvalidDraft, false, {0U}};
      }
      sort_drill_group_ids(draft, *source);
      if (source->drill_group_ids.empty() ||
          !merge_drill_into(draft, destination_id, source_id)) {
        return {ChartEditCode::InvalidDraft, false, {0U}};
      }
    }
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

[[nodiscard]] std::vector<ChartMarker> player_source_actions(
    const std::vector<ChartMarker>& markers, ChartEventId npc_c,
    std::uint64_t player_c_row, bool include_leading_end);

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
  const auto source_actions = player_source_actions(
      markers, markers[npc_c_index].id, player_start.row, false);
  std::vector<const ChartMarker*> source;
  for (const auto& action : source_actions) {
    source.push_back(&action);
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

/// Initial generation searches for the first N between the C markers, then
/// follows consecutive NPC actions. Synchronization starts immediately after
/// NPC-C, maps a leading S like any other S, and never crosses another marker.
[[nodiscard]] std::vector<ChartMarker> player_source_actions(
    const std::vector<ChartMarker>& markers, ChartEventId npc_c,
    std::uint64_t player_c_row, bool include_leading_end) {
  const auto* start = find_chart_marker(markers, npc_c);
  if (start == nullptr || start->type != ChartMarkerType::NpcBlockStart) {
    return {};
  }
  auto first = markers.end();
  bool after_start = false;
  for (auto it = markers.begin(); it != markers.end(); ++it) {
    if (!after_start) {
      after_start = it->id.value == npc_c.value;
      continue;
    }
    if (it->row >= player_c_row) break;
    if (it->type == ChartMarkerType::NpcNote ||
        (include_leading_end && it->type == ChartMarkerType::NpcMoveEnd)) {
      first = it;
      break;
    }
    if (include_leading_end) return {};
  }
  std::vector<ChartMarker> source;
  for (auto it = first; it != markers.end(); ++it) {
    if (it->type != ChartMarkerType::NpcNote &&
        it->type != ChartMarkerType::NpcMoveEnd) break;
    source.push_back(*it);
  }
  return source;
}

[[nodiscard]] bool same_source_actions(const std::vector<ChartMarker>& left,
                                        const std::vector<ChartMarker>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t i = 0U; i < left.size(); ++i) {
    if (left[i].id.value != right[i].id.value || left[i].row != right[i].row ||
        left[i].type != right[i].type) return false;
  }
  return true;
}

/// Read source data from a fixed post-NPC snapshot, but check occupied rows in
/// the current candidate for each block. Failed blocks are never retried after
/// another block is cleared. Everything remains inside the click transaction.
void synchronize_npc_edit(const ChartDraft& original,
                          const std::vector<ChartMarker>& before,
                          MutableDraft& after, IdState& ids,
                          ChartEditResult& result) {
  const auto projection = project_chart_markers(ChartDraft{
      after.role_events, after.showtime_start_events, after.showtime_move_events,
      after.drill_groups, after.dance_move_drills, after.showtimes});
  if (!projection) throw std::bad_alloc{};
  const auto& source_markers = *projection.markers();
  ChartEventId source_c{0U};
  for (std::size_t index = 0U; index < before.size(); ++index) {
    const auto& marker = before[index];
    if (marker.type == ChartMarkerType::NpcBlockStart) source_c = marker.id;
    if (marker.type != ChartMarkerType::PlayerBlockStart || source_c.value == 0U)
      continue;

    const auto* old_player_c = find_role_event_in(original, marker.id.value);
    const auto* old_npc_c = find_role_event_in(original, source_c.value);
    if (old_player_c == nullptr || old_npc_c == nullptr ||
        old_player_c->drill_group_id.value != old_npc_c->drill_group_id.value ||
        old_player_c->dance_move_drill_id.value != old_npc_c->dance_move_drill_id.value)
      continue;
    std::vector<std::uint64_t> old_ids;
    const ChartMarker* old_player_anchor = nullptr;
    for (auto i = index + 1U; i < before.size(); ++i) {
      if (before[i].type != ChartMarkerType::PlayerNote &&
          before[i].type != ChartMarkerType::PlayerMoveEnd) break;
      const auto* action = find_role_event_in(original, before[i].id.value);
      if (action == nullptr ||
          action->drill_group_id.value != old_player_c->drill_group_id.value ||
          action->dance_move_drill_id.value != old_player_c->dance_move_drill_id.value)
        break;
      old_ids.push_back(before[i].id.value);
      if (old_player_anchor == nullptr && before[i].type == ChartMarkerType::PlayerNote)
        old_player_anchor = &before[i];
    }
    if (find_chart_marker(source_markers, source_c) == nullptr) {
      old_ids.push_back(marker.id.value);
      erase_role_events(after, old_ids);
      continue;
    }
    if (old_ids.empty()) continue;
    const auto old_source = player_source_actions(before, source_c, marker.row, true);
    const auto source = player_source_actions(source_markers, source_c, marker.row, true);
    if (same_source_actions(old_source, source)) continue;
    const auto old_anchor = player_source_actions(before, source_c, marker.row, false);
    const auto* player_c = find_role_event_const(after, marker.id.value);
    const auto* npc_c = find_role_event_const(after, source_c.value);
    const auto* group = npc_c == nullptr ? nullptr :
        find_group_const(after, npc_c->drill_group_id.value);
    bool valid = !source.empty() &&
        !old_anchor.empty() && old_player_anchor != nullptr && player_c != nullptr &&
        group != nullptr && find_drill(after, group->dance_move_drill_id.value) != nullptr;
    std::vector<ChartRoleEvent> replacements;
    IdState planned_ids = ids;
    if (valid) {
      const bool positive = old_player_anchor->row >= old_anchor.front().row;
      const auto distance = positive ? old_player_anchor->row - old_anchor.front().row
                                     : old_anchor.front().row - old_player_anchor->row;
      for (std::size_t i = 0U; i < source.size(); ++i) {
        const auto row = source[i].row;
        if ((positive && row > std::numeric_limits<std::uint64_t>::max() - distance) ||
            (!positive && row < distance)) {
          valid = false;
          break;
        }
        const auto target = positive ? row + distance : row - distance;
        if (target <= player_c->row) {
          valid = false;
          break;
        }
        std::uint64_t id = i < old_ids.size() ? old_ids[i] : 0U;
        if (i >= old_ids.size() && !allocate_id(planned_ids, id)) {
          valid = false;
          break;
        }
        replacements.push_back({{id}, target, ChartRole::Player,
            source[i].type == ChartMarkerType::NpcNote ? ChartRoleEventType::Note
                                                       : ChartRoleEventType::MoveEnd,
            group->id, group->dance_move_drill_id});
      }
    }
    if (valid) {
      const auto occupied = [&old_ids, &replacements](const auto& event) {
        return event.row >= replacements.front().row &&
            event.row <= replacements.back().row &&
            std::find(old_ids.begin(), old_ids.end(), event.id.value) == old_ids.end();
      };
      valid = std::none_of(after.role_events.begin(), after.role_events.end(), occupied) &&
          std::none_of(after.showtime_start_events.begin(), after.showtime_start_events.end(), occupied) &&
          std::none_of(after.showtime_move_events.begin(), after.showtime_move_events.end(), occupied);
    }
    erase_role_events(after, old_ids);
    if (!valid) {
      result.player_actions_cleared = true;
      continue;
    }
    const auto group_id = replacements.front().drill_group_id;
    auto* updated_group = find_group(after, group_id.value);
    auto* updated_c = find_role_event(after, marker.id.value);
    updated_c->drill_group_id = group_id;
    updated_c->dance_move_drill_id = replacements.front().dance_move_drill_id;
    // Rebuild only this block's membership, preserving other PLY-C blocks in
    // the same group and using the post-split/merge drill identity.
    erase_event_references(after, {marker.id.value});
    updated_group->player_event_ids.push_back(marker.id);
    for (const auto& event : replacements) {
      after.role_events.push_back(event);
      updated_group->player_event_ids.push_back(event.id);
    }
    ids = planned_ids;
    sort_role_events(after);
    sort_relation_ids(after, updated_group->player_event_ids);
  }
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
  // The earliest N of either role. A block start is not part of the dance
  // move itself, so it must not decide whether the move belongs to the
  // previous Showtime's display range.
  std::uint64_t first_note_row;
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
  bool has_notes = false;
  bool has_relation_row = false;
  std::uint64_t relation_row = 0U;
  std::uint64_t first_row = 0U;
  std::uint64_t first_note_row = 0U;
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
      if (event->type == ChartRoleEventType::Note) {
        first_note_row =
            !has_notes ? event->row : std::min(first_note_row, event->row);
        has_notes = true;
      }
    }
    for (const auto* event : player.events) {
      first_row = std::min(first_row, event->row);
      last_row = std::max(last_row, event->row);
      if (event->type == ChartRoleEventType::Note) {
        first_note_row =
            !has_notes ? event->row : std::min(first_note_row, event->row);
        has_notes = true;
      }
    }
  }
  if (!has_rows || !has_notes || note_count != drill.move_n_count) {
    return false;
  }
  result = {drill.id, first_row, first_note_row, last_row, drill.move_n_count};
  return true;
}

[[nodiscard]] std::vector<CompleteDrill> collect_complete_drills(
    const MutableDraft& draft, const std::vector<ChartMarker>& markers,
    std::uint64_t showtime_start_row) {
  bool has_previous_showtime_move = false;
  std::uint64_t previous_showtime_range_end = 0U;
  for (const auto& marker : markers) {
    if (marker.row >= showtime_start_row) {
      break;
    }
    if (marker.type == ChartMarkerType::ShowtimeMove) {
      const auto range_start = marker.row - marker.row % 16U;
      const auto duration = marker.show_n_count <= 6U ? 16U : 32U;
      previous_showtime_range_end =
          range_start > std::numeric_limits<std::uint64_t>::max() - duration
              ? std::numeric_limits<std::uint64_t>::max()
              : range_start + duration;
      has_previous_showtime_move = true;
    }
  }

  std::vector<CompleteDrill> result;
  for (const auto& drill : draft.dance_move_drills) {
    CompleteDrill candidate{};
    if (!complete_drill(draft, drill, candidate) ||
        candidate.last_row >= showtime_start_row ||
        (has_previous_showtime_move &&
         candidate.first_note_row < previous_showtime_range_end)) {
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
  // The Showtime relation order is also the finished-chart and export order.
  // A Showtime placed earlier than an existing one must be inserted at its row
  // instead of being appended, or validation rejects the draft and export
  // writes Showtimes out of time order.
  const auto start_row = [&draft](ChartEventId id) {
    for (const auto& event : draft.showtime_start_events) {
      if (event.id.value == id.value) {
        return event.row;
      }
    }
    return std::uint64_t{0U};
  };
  std::sort(draft.showtimes.begin(), draft.showtimes.end(),
            [&start_row](const ChartShowtime& left,
                         const ChartShowtime& right) {
              const auto left_row = start_row(left.start_event_id);
              const auto right_row = start_row(right.start_event_id);
              return left_row != right_row ? left_row < right_row
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
      showtime->start_event_id.value != showtime_start_event_id.value) {
    return {ChartEditCode::InvalidDraft, false, {0U}};
  }
  // The marker pair counts, the recorded dance moves and the move sequence that
  // the chart would generate now no longer agree. A stale Showtime is not
  // repaired in place: the user deletes it and places it again, which rebuilds
  // the whole segment from the current dance moves.
  if (showtime->dance_move_drill_ids.size() !=
      showtime->show_move_event_ids.size()) {
    return {ChartEditCode::ShowtimeContentStale, false, {0U}};
  }
  const auto drills =
      collect_complete_drills(draft, markers, showtime_start_row);
  if (showtime->dance_move_drill_ids.size() > drills.size()) {
    return {ChartEditCode::ShowtimeContentStale, false, {0U}};
  }
  for (std::size_t index = 0U; index < showtime->dance_move_drill_ids.size();
       ++index) {
    if (showtime->dance_move_drill_ids[index].value !=
        drills[index].id.value) {
      return {ChartEditCode::ShowtimeContentStale, false, {0U}};
    }
  }
  const auto begin = showtime->dance_move_drill_ids.size();
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
    showtime->dance_move_drill_ids.push_back(pending[index].drill_id);
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

/// A Showtime whose stored dance moves no longer support it: a referenced drill
/// is gone, a referenced drill is no longer a complete dance move, or a stored
/// SHOW-D count no longer matches its drill. Every one of those already fails
/// the finished-chart rules, so the Showtime can only be rebuilt. A Showtime
/// that merely cannot be extended any more - the candidate list moved on, or an
/// imported Showtime carries fewer move markers than drills - is left alone:
/// its stored content is still consistent, and only continuing it is refused.
[[nodiscard]] bool showtime_is_stale(MutableDraft& draft,
                                     const ChartShowtime& showtime) {
  // A recorded dance move is examined on its own: an imported Showtime may
  // carry fewer move markers than dance moves, so a recorded dance move without
  // a stored SHOW-D is legal and does not make the Showtime stale.
  for (const auto drill_id : showtime.dance_move_drill_ids) {
    const auto* drill = find_drill(draft, drill_id.value);
    if (drill == nullptr) {
      return true;
    }
    CompleteDrill complete{};
    if (!complete_drill(draft, *drill, complete)) {
      return true;
    }
  }
  // Every stored SHOW-D is compared against the dance move it points at itself,
  // not against the recorded dance move at the same position: the marker owns
  // the reference, and only it knows which dance move its count was taken from.
  // The markers are walked on their own as well, so a registered marker that is
  // gone or that sits past the recorded dance moves is still examined.
  for (const auto event_id : showtime.show_move_event_ids) {
    const auto* move = find_showtime_move_event(draft, event_id);
    if (move == nullptr) {
      return true;
    }
    const auto* referenced =
        find_drill(draft, move->dance_move_drill_id.value);
    if (referenced == nullptr) {
      return true;
    }
    CompleteDrill complete{};
    if (!complete_drill(draft, *referenced, complete)) {
      return true;
    }
    if (move->show_n_count != referenced->move_n_count) {
      return true;
    }
  }
  return false;
}

/// Every dance move the Showtime records is still the leading prefix of the
/// moves its current content would offer. A shorter prefix is legal: an
/// imported Showtime may carry fewer move markers than dance moves.
[[nodiscard]] bool showtime_matches_candidate_prefix(
    const MutableDraft& draft, const std::vector<ChartMarker>& markers,
    const ChartShowtime& showtime, std::uint64_t start_row) {
  const auto drills = collect_complete_drills(draft, markers, start_row);
  if (showtime.dance_move_drill_ids.size() > drills.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < showtime.dance_move_drill_ids.size();
       ++index) {
    if (showtime.dance_move_drill_ids[index].value != drills[index].id.value) {
      return false;
    }
  }
  return true;
}

/// Drops every stale Showtime together with its start marker and all of its
/// move markers, so that one edit never leaves Showtime markers behind.
void prune_stale_showtimes(MutableDraft& draft) {
  std::size_t index = 0U;
  while (index < draft.showtimes.size()) {
    if (!showtime_is_stale(draft, draft.showtimes[index])) {
      ++index;
      continue;
    }
    const auto showtime_id = draft.showtimes[index].id.value;
    const auto belongs = [showtime_id](ChartShowtimeId id) {
      return id.value == showtime_id;
    };
    draft.showtime_start_events.erase(
        std::remove_if(draft.showtime_start_events.begin(),
                       draft.showtime_start_events.end(),
                       [&belongs](const ChartShowtimeStartEvent& event) {
                         return belongs(event.showtime_id);
                       }),
        draft.showtime_start_events.end());
    draft.showtime_move_events.erase(
        std::remove_if(draft.showtime_move_events.begin(),
                       draft.showtime_move_events.end(),
                       [&belongs](const ChartShowtimeMoveEvent& event) {
                         return belongs(event.showtime_id);
                       }),
        draft.showtime_move_events.end());
    draft.showtimes.erase(draft.showtimes.begin() +
                          static_cast<std::ptrdiff_t>(index));
  }
}

/// Drops a dance move that no longer has any event bound to it. Deleting the
/// content of a dance move leaves its drill and its groups behind with nothing
/// left to click, and that leftover still fails the finished-chart rules even
/// when the chart holds no Showtime at all. A dance move being built always
/// keeps at least the block start event that created it, so only leftovers go.
void prune_eventless_drills(MutableDraft& draft) {
  const auto is_eventless = [&draft](ChartDanceMoveDrillId drill_id) {
    return std::none_of(draft.role_events.begin(), draft.role_events.end(),
                        [drill_id](const ChartRoleEvent& event) {
                          return event.dance_move_drill_id.value ==
                                 drill_id.value;
                        });
  };
  std::vector<ChartDanceMoveDrillId> dropped;
  for (const auto& drill : draft.dance_move_drills) {
    if (is_eventless(drill.id)) {
      dropped.push_back(drill.id);
    }
  }
  if (dropped.empty()) {
    return;
  }
  const auto is_dropped = [&dropped](ChartDanceMoveDrillId id) {
    return std::any_of(dropped.begin(), dropped.end(),
                       [id](ChartDanceMoveDrillId candidate) {
                         return candidate.value == id.value;
                       });
  };
  draft.drill_groups.erase(
      std::remove_if(draft.drill_groups.begin(), draft.drill_groups.end(),
                     [&is_dropped](const ChartDrillGroup& group) {
                       return is_dropped(group.dance_move_drill_id);
                     }),
      draft.drill_groups.end());
  draft.dance_move_drills.erase(
      std::remove_if(draft.dance_move_drills.begin(),
                     draft.dance_move_drills.end(),
                     [&is_dropped](const ChartDanceMoveDrill& drill) {
                       return is_dropped(drill.id);
                     }),
      draft.dance_move_drills.end());
}

/// A fully deleted group has no remaining event to edit. Drop it and its
/// registration, but retain a group with any bound event even when its stored
/// event lists are incomplete. Membership is authoritative on the events.
void prune_eventless_groups(MutableDraft& draft) {
  const auto is_eventless = [&draft](ChartDrillGroupId group_id) {
    return std::none_of(draft.role_events.begin(), draft.role_events.end(),
                        [group_id](const ChartRoleEvent& event) {
                          return event.drill_group_id.value == group_id.value;
                        });
  };
  for (const auto& group : draft.drill_groups) {
    if (!is_eventless(group.id)) {
      continue;
    }
    for (auto& drill : draft.dance_move_drills) {
      drill.drill_group_ids.erase(
          std::remove_if(drill.drill_group_ids.begin(),
                         drill.drill_group_ids.end(),
                         [&group](ChartDrillGroupId id) {
                           return id.value == group.id.value;
                         }),
          drill.drill_group_ids.end());
    }
  }
  draft.drill_groups.erase(
      std::remove_if(draft.drill_groups.begin(), draft.drill_groups.end(),
                     [&is_eventless](const ChartDrillGroup& group) {
                       return is_eventless(group.id);
                     }),
      draft.drill_groups.end());
}

[[nodiscard]] ChartDraft MutableDraft::finish() && {
  prune_eventless_groups(*this);
  prune_stale_showtimes(*this);
  prune_eventless_drills(*this);
  return {std::move(role_events), std::move(showtime_start_events),
          std::move(showtime_move_events), std::move(drill_groups),
          std::move(dance_move_drills), std::move(showtimes)};
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
    const auto pair_count = std::min(showtime.dance_move_drill_ids.size(),
                                     showtime.show_move_event_ids.size());
    std::vector<ChartDanceMoveDrillId> kept_drills;
    std::vector<ChartEventId> kept_events;
    kept_drills.reserve(showtime.dance_move_drill_ids.size());
    kept_events.reserve(showtime.show_move_event_ids.size());
    for (std::size_t index = 0U; index < pair_count; ++index) {
      if (!erased(showtime.show_move_event_ids[index])) {
        kept_drills.push_back(showtime.dance_move_drill_ids[index]);
        kept_events.push_back(showtime.show_move_event_ids[index]);
      }
    }
    showtime.dance_move_drill_ids = std::move(kept_drills);
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

struct PastePlan final {
  ChartPastePreview preview;
  ChartDraft destination{{}, {}, {}, {}, {}, {}};
  IdState ids;
  std::vector<std::uint64_t> drill_ids;
  std::vector<std::uint64_t> group_ids;
  std::vector<std::uint64_t> event_ids;
};

/// Preview and commit share the exact same source verification, cleanup,
/// occupancy and identifier budget checks. All state here is speculative.
[[nodiscard]] PastePlan plan_paste(
    const ChartDraft& draft, const ChartClipboardFragment& clipboard,
    const ChartDraft* cut_source, std::uint64_t anchor, IdState ids) {
  PastePlan plan{{ChartEditCode::ClipboardEmpty, anchor, anchor, {}, {}},
                 ChartDraft{{}, {}, {}, {}, {}, {}}, ids, {}, {}, {}};
  const auto& content = clipboard.content;
  if (content.role_events().empty() || content.dance_move_drills().empty()) {
    return plan;
  }
  auto& preview = plan.preview;
  for (const auto& event : content.role_events()) {
    if (event.row > std::numeric_limits<std::uint64_t>::max() - anchor) {
      preview.code = ChartEditCode::RangeOverflow;
      return plan;
    }
    preview.last_row = std::max(preview.last_row, anchor + event.row);
  }
  for (const auto& drill : content.dance_move_drills()) {
    ChartSelection span{};
    if (!derive_selection(content, drill.id, span)) {
      preview.code = ChartEditCode::InvalidDraft;
      return plan;
    }
    span.first_row += anchor;
    span.last_row += anchor;
    preview.spans.push_back(span);
  }
  std::sort(preview.spans.begin(), preview.spans.end(), selection_less);
  if (!editable_projection(content, preview.ghost_markers, preview.code)) {
    return plan;
  }
  for (auto& marker : preview.ghost_markers) {
    marker.row += anchor;
  }

  if (cut_source != nullptr) {
    std::vector<ChartSelection> sources;
    for (const auto& drill : cut_source->dance_move_drills()) {
      sources.push_back({true, drill.id, 0U, 0U});
    }
    const auto current = snapshot_selection(draft, sources);
    if (!chart_drafts_equal(current, *cut_source)) {
      preview.code = ChartEditCode::CutSourceChanged;
      return plan;
    }
  }
  std::vector<ChartMarker> markers;
  if (!editable_projection(draft, markers, preview.code)) {
    return plan;
  }
  MutableDraft candidate{draft};
  if (cut_source != nullptr) {
    const auto erased_drill = [cut_source](ChartDanceMoveDrillId id) {
      return std::any_of(cut_source->dance_move_drills().begin(),
                         cut_source->dance_move_drills().end(),
                         [id](const ChartDanceMoveDrill& drill) {
                           return id.value == drill.id.value;
                         });
    };
    std::vector<std::uint64_t> erased_events;
    std::vector<std::uint64_t> erased_groups;
    for (const auto& event : cut_source->role_events()) {
      erased_events.push_back(event.id.value);
    }
    for (const auto& group : cut_source->drill_groups()) {
      erased_groups.push_back(group.id.value);
    }
    candidate.role_events.erase(
        std::remove_if(candidate.role_events.begin(), candidate.role_events.end(),
                       [&erased_drill](const ChartRoleEvent& event) {
                         return erased_drill(event.dance_move_drill_id);
                       }),
        candidate.role_events.end());
    candidate.drill_groups.erase(
        std::remove_if(candidate.drill_groups.begin(), candidate.drill_groups.end(),
                       [&erased_drill](const ChartDrillGroup& group) {
                         return erased_drill(group.dance_move_drill_id);
                       }),
        candidate.drill_groups.end());
    candidate.dance_move_drills.erase(
        std::remove_if(candidate.dance_move_drills.begin(),
                       candidate.dance_move_drills.end(),
                       [&erased_drill](const ChartDanceMoveDrill& drill) {
                         return erased_drill(drill.id);
                       }),
        candidate.dance_move_drills.end());
    erase_event_references(candidate, erased_events);
    for (auto& drill : candidate.dance_move_drills) {
      auto& groups = drill.drill_group_ids;
      groups.erase(std::remove_if(groups.begin(), groups.end(),
                                  [&erased_groups](ChartDrillGroupId id) {
                                    return std::find(erased_groups.begin(),
                                                     erased_groups.end(), id.value) !=
                                           erased_groups.end();
                                  }),
                   groups.end());
    }
    // finish() applies the same cleanup as every other committed edit, before
    // checking whether the now-vacant Showtime/source rows can be reused.
    plan.destination = std::move(candidate).finish();
  } else {
    plan.destination = draft;
  }
  if (!editable_projection(plan.destination, markers, preview.code)) {
    return plan;
  }
  for (const auto& marker : markers) {
    if (marker.row >= anchor && marker.row <= preview.last_row) {
      preview.code = ChartEditCode::PasteRangeOccupied;
      return plan;
    }
  }
  for (const auto& event : content.role_events()) {
    if (event.role == ChartRole::Npc &&
        event.type == ChartRoleEventType::BlockStart &&
        !npc_start_allowed(plan.destination, anchor + event.row, preview.code)) {
      return plan;
    }
  }
  plan.drill_ids.resize(content.dance_move_drills().size());
  plan.group_ids.resize(content.drill_groups().size());
  plan.event_ids.resize(content.role_events().size());
  for (auto* values : {&plan.drill_ids, &plan.group_ids, &plan.event_ids}) {
    for (auto& value : *values) {
      if (!allocate_id(plan.ids, value)) {
        preview.code = ChartEditCode::IdExhausted;
        return plan;
      }
    }
  }
  preview.code = ChartEditCode::Success;
  return plan;
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
  revalidate_selection();
  return result;
}

void ChartEditor::revalidate_selection() noexcept {
  selections_.erase(
      std::remove_if(selections_.begin(), selections_.end(),
                     [this](ChartSelection& item) {
                       ChartSelection derived{};
                       if (!derive_selection(draft_, item.drill_id, derived)) {
                         return true;
                       }
                       item = derived;
                       return false;
                     }),
      selections_.end());
  std::sort(selections_.begin(), selections_.end(), selection_less);
}

ChartRepairStatus ChartEditor::repair_relations() noexcept {
  try {
    MutableDraft candidate{draft_};
    prune_eventless_groups(candidate);

    struct GroupInfo final {
      ChartDrillGroupId id;
      std::uint64_t first_row;
      bool has_npc_end;
    };
    std::vector<GroupInfo> groups;
    groups.reserve(candidate.drill_groups.size());
    for (const auto& group : candidate.drill_groups) {
      std::uint64_t first_row = std::numeric_limits<std::uint64_t>::max();
      bool has_npc_end = false;
      bool has_event = false;
      for (const auto& event : candidate.role_events) {
        if (event.drill_group_id.value != group.id.value) {
          continue;
        }
        has_event = true;
        first_row = std::min(first_row, event.row);
        if (event.role == ChartRole::Npc &&
            event.type == ChartRoleEventType::MoveEnd) {
          has_npc_end = true;
        }
      }
      if (has_event) {
        groups.push_back({group.id, first_row, has_npc_end});
      }
    }
    std::sort(groups.begin(), groups.end(),
              [](const GroupInfo& left, const GroupInfo& right) {
                return left.first_row != right.first_row
                           ? left.first_row < right.first_row
                           : left.id.value < right.id.value;
              });

    std::vector<std::vector<ChartDrillGroupId>> sets;
    std::vector<ChartDrillGroupId> current;
    for (const auto& group : groups) {
      current.push_back(group.id);
      if (group.has_npc_end) {
        sets.push_back(std::move(current));
        current.clear();
      }
    }
    if (!current.empty()) {
      sets.push_back(std::move(current));
    }

    const auto row_of = [&groups](ChartDrillGroupId id) {
      for (const auto& group : groups) {
        if (group.id.value == id.value) {
          return group.first_row;
        }
      }
      return std::numeric_limits<std::uint64_t>::max();
    };
    const auto same_group_set =
        [&row_of](const std::vector<ChartDrillGroupId>& left,
                  const std::vector<ChartDrillGroupId>& right) {
          if (left.size() != right.size()) {
            return false;
          }
          auto sorted_left = left;
          auto sorted_right = right;
          const auto less = [&row_of](ChartDrillGroupId a,
                                      ChartDrillGroupId b) {
            const auto a_row = row_of(a);
            const auto b_row = row_of(b);
            return a_row != b_row ? a_row < b_row : a.value < b.value;
          };
          std::sort(sorted_left.begin(), sorted_left.end(), less);
          std::sort(sorted_right.begin(), sorted_right.end(), less);
          for (std::size_t index = 0U; index < sorted_left.size(); ++index) {
            if (sorted_left[index].value != sorted_right[index].value) {
              return false;
            }
          }
          return true;
        };

    struct OldDrill final {
      ChartDanceMoveDrillId id;
      std::vector<ChartDrillGroupId> groups;
    };
    std::vector<OldDrill> old_drills;
    old_drills.reserve(candidate.dance_move_drills.size());
    for (const auto& drill : candidate.dance_move_drills) {
      old_drills.push_back({drill.id, drill.drill_group_ids});
    }

    IdState ids{next_id_, id_available_};
    std::vector<ChartDanceMoveDrillId> new_ids;
    new_ids.reserve(sets.size());
    std::vector<bool> old_used(old_drills.size(), false);
    for (const auto& set : sets) {
      ChartDanceMoveDrillId chosen{0U};
      for (std::size_t index = 0U; index < old_drills.size(); ++index) {
        if (!old_used[index] &&
            same_group_set(set, old_drills[index].groups)) {
          chosen = old_drills[index].id;
          old_used[index] = true;
          break;
        }
      }
      if (chosen.value == 0U) {
        std::uint64_t value = 0U;
        if (!allocate_id(ids, value)) {
          return ChartRepairStatus::AllocationFailure;
        }
        chosen = {value};
      }
      new_ids.push_back(chosen);
    }

    std::vector<ChartDanceMoveDrill> new_drills;
    new_drills.reserve(sets.size());
    for (std::size_t index = 0U; index < sets.size(); ++index) {
      ChartDanceMoveDrill drill{new_ids[index], 0U, sets[index]};
      for (const auto group_id : sets[index]) {
        auto* group = find_group(candidate, group_id.value);
        if (group == nullptr) {
          continue;
        }
        group->dance_move_drill_id = drill.id;
        for (auto& event : candidate.role_events) {
          if (event.drill_group_id.value != group_id.value) {
            continue;
          }
          event.dance_move_drill_id = drill.id;
          if (event.role == ChartRole::Npc &&
              event.type == ChartRoleEventType::Note) {
            ++drill.move_n_count;
          }
        }
      }
      new_drills.push_back(std::move(drill));
    }
    candidate.dance_move_drills = std::move(new_drills);

    // A Showtime laid out against the old relation keeps the moves it was
    // given, but regrouping can reveal complete dance moves that the broken
    // relation had merged into an incomplete drill. Its recorded moves are then
    // no longer the current candidate prefix, so it is rebuilt in place from
    // the repaired complete moves; a Showtime that still matches the prefix is
    // left untouched, as an edit that only inserted a move must be.
    bool drills_rebuilt = false;
    for (const auto& group : candidate.drill_groups) {
      const auto original = std::find_if(
          draft_.drill_groups().begin(), draft_.drill_groups().end(),
          [&group](const ChartDrillGroup& existing) {
            return existing.id.value == group.id.value;
          });
      if (original == draft_.drill_groups().end() ||
          original->dance_move_drill_id.value !=
              group.dance_move_drill_id.value) {
        drills_rebuilt = true;
        break;
      }
    }

    std::size_t showtime_index = 0U;
    while (showtime_index < candidate.showtimes.size()) {
      const auto showtime_id =
          candidate.showtimes[showtime_index].id.value;
      const auto start_event_id =
          candidate.showtimes[showtime_index].start_event_id;
      const bool stale = showtime_is_stale(
          candidate, candidate.showtimes[showtime_index]);
      const auto* start =
          find_showtime_start_event(candidate, start_event_id);
      bool rebuild = stale;
      if (!stale && drills_rebuilt && start != nullptr) {
        auto markers = project_chart_markers(ChartDraft{
            candidate.role_events, candidate.showtime_start_events,
            candidate.showtime_move_events, candidate.drill_groups,
            candidate.dance_move_drills, candidate.showtimes});
        if (!markers) {
          return markers.error() == ChartProjectionError::AllocationFailure
                     ? ChartRepairStatus::AllocationFailure
                     : ChartRepairStatus::InvalidDraft;
        }
        rebuild = !showtime_matches_candidate_prefix(
            candidate, *markers.markers(),
            candidate.showtimes[showtime_index], start->row);
      }
      if (!rebuild) {
        ++showtime_index;
        continue;
      }
      if (start == nullptr) {
        candidate.showtimes.erase(candidate.showtimes.begin() +
                                  static_cast<std::ptrdiff_t>(showtime_index));
        candidate.showtime_start_events.erase(
            std::remove_if(candidate.showtime_start_events.begin(),
                           candidate.showtime_start_events.end(),
                           [showtime_id](const ChartShowtimeStartEvent& event) {
                             return event.showtime_id.value == showtime_id;
                           }),
            candidate.showtime_start_events.end());
        candidate.showtime_move_events.erase(
            std::remove_if(candidate.showtime_move_events.begin(),
                           candidate.showtime_move_events.end(),
                           [showtime_id](const ChartShowtimeMoveEvent& event) {
                             return event.showtime_id.value == showtime_id;
                           }),
            candidate.showtime_move_events.end());
        continue;
      }
      const auto start_row = start->row;
      std::uint64_t first_row = start_row;
      bool position_ok = true;
      if (start_row % 16U == 0U) {
        if (start_row == std::numeric_limits<std::uint64_t>::max()) {
          position_ok = false;
        } else {
          first_row = start_row + 1U;
        }
      } else {
        const auto measure = start_row / 16U;
        if (measure == std::numeric_limits<std::uint64_t>::max() / 16U) {
          position_ok = false;
        } else {
          first_row = (measure + 1U) * 16U;
        }
      }
      // Rebuild on a copy so a Showtime that no longer fits keeps its stored
      // content instead of losing it to a failed attempt.
      MutableDraft attempt{candidate};
      auto attempt_ids = ids;
      auto* attempt_showtime = find_showtime(attempt, showtime_id);
      if (attempt_showtime == nullptr) {
        ++showtime_index;
        continue;
      }
      attempt.showtime_move_events.erase(
          std::remove_if(attempt.showtime_move_events.begin(),
                         attempt.showtime_move_events.end(),
                         [showtime_id](const ChartShowtimeMoveEvent& event) {
                           return event.showtime_id.value == showtime_id;
                         }),
          attempt.showtime_move_events.end());
      attempt_showtime->show_move_event_ids.clear();
      attempt_showtime->dance_move_drill_ids.clear();
      auto rebuilt = false;
      if (position_ok) {
        auto markers = project_chart_markers(ChartDraft{
            attempt.role_events, attempt.showtime_start_events,
            attempt.showtime_move_events, attempt.drill_groups,
            attempt.dance_move_drills, attempt.showtimes});
        if (!markers) {
          return markers.error() == ChartProjectionError::AllocationFailure
                     ? ChartRepairStatus::AllocationFailure
                     : ChartRepairStatus::InvalidDraft;
        }
        const auto completion = complete_showtime(
            attempt, *markers.markers(), {showtime_id}, start_row,
            start_event_id, first_row, attempt_ids);
        if (!completion &&
            (completion.code == ChartEditCode::AllocationFailure ||
             completion.code == ChartEditCode::IdExhausted)) {
          return ChartRepairStatus::AllocationFailure;
        }
        rebuilt = static_cast<bool>(completion);
      }
      if (rebuilt) {
        candidate = std::move(attempt);
        ids = attempt_ids;
        ++showtime_index;
        continue;
      }
      if (!stale) {
        ++showtime_index;
        continue;
      }
      candidate.showtime_start_events.erase(
          std::remove_if(candidate.showtime_start_events.begin(),
                         candidate.showtime_start_events.end(),
                         [showtime_id](const ChartShowtimeStartEvent& event) {
                           return event.showtime_id.value == showtime_id;
                         }),
          candidate.showtime_start_events.end());
      candidate.showtime_move_events.erase(
          std::remove_if(candidate.showtime_move_events.begin(),
                         candidate.showtime_move_events.end(),
                         [showtime_id](const ChartShowtimeMoveEvent& event) {
                           return event.showtime_id.value == showtime_id;
                         }),
          candidate.showtime_move_events.end());
      candidate.showtimes.erase(candidate.showtimes.begin() +
                                static_cast<std::ptrdiff_t>(showtime_index));
    }

    auto finished = std::move(candidate).finish();
    if (chart_drafts_equal(draft_, finished)) {
      return ChartRepairStatus::Unchanged;
    }
    draft_ = std::move(finished);
    next_id_ = ids.next;
    id_available_ = ids.available;
    undo_.clear();
    redo_.clear();
    selections_.clear();
    clipboard_ = ChartClipboardFragment{};
    has_clipboard_ = false;
    pending_cut_ = false;
    cut_source_ = ChartDraft{{}, {}, {}, {}, {}, {}};
    return ChartRepairStatus::Repaired;
  } catch (const std::bad_alloc&) {
    return ChartRepairStatus::AllocationFailure;
  }
}

ChartSessionResult ChartEditor::select(ChartColumn column,
                                       std::uint64_t row) noexcept {
  try {
    std::vector<ChartMarker> markers;
    ChartEditCode projection_error = ChartEditCode::Success;
    if (!editable_projection(draft_, markers, projection_error)) {
      return {projection_error, false};
    }
    const auto* current = marker_at(markers, row);
    if (current == nullptr ||
        (column != ChartColumn::Npc && column != ChartColumn::Player &&
         column != ChartColumn::Showtime)) {
      return {ChartEditCode::Success, false};
    }
    ChartDanceMoveDrillId drill_id{0U};
    if (current->type == ChartMarkerType::ShowtimeStart) {
      return {ChartEditCode::SelectionUnavailable, false};
    }
    if (current->type == ChartMarkerType::ShowtimeMove) {
      const auto* event = find_showtime_move_in(draft_, current->id.value);
      if (event == nullptr) {
        return {ChartEditCode::InvalidDraft, false};
      }
      drill_id = event->dance_move_drill_id;
    } else {
      const auto* event = find_role_event_in(draft_, current->id.value);
      if (event == nullptr) {
        return {ChartEditCode::InvalidDraft, false};
      }
      drill_id = event->dance_move_drill_id;
    }
    ChartSelection derived{};
    if (!derive_selection(draft_, drill_id, derived)) {
      return {ChartEditCode::SelectionUnavailable, false};
    }
    const auto selected = std::find_if(
        selections_.begin(), selections_.end(),
        [drill_id](const ChartSelection& item) {
          return item.drill_id.value == drill_id.value;
        });
    if (selected == selections_.end()) {
      selections_.push_back(derived);
      std::sort(selections_.begin(), selections_.end(), selection_less);
    } else {
      selections_.erase(selected);
    }
    return {ChartEditCode::Success, true};
  } catch (const std::bad_alloc&) {
    return {ChartEditCode::AllocationFailure, false};
  }
}

ChartSessionResult ChartEditor::clear_selection() noexcept {
  const bool changed = !selections_.empty();
  selections_.clear();
  return {ChartEditCode::Success, changed};
}

ChartSessionResult ChartEditor::prepare_clipboard(bool cut) noexcept {
  try {
    if (selections_.empty()) {
      return {ChartEditCode::SelectionUnavailable, false};
    }
    std::vector<ChartMarker> markers;
    ChartEditCode projection_error = ChartEditCode::Success;
    if (!editable_projection(draft_, markers, projection_error)) {
      return {projection_error, false};
    }
    ChartClipboardFragment fragment{};
    if (!build_fragments(draft_, selections_, fragment)) {
      return {ChartEditCode::SelectionUnavailable, false};
    }
    ChartDraft source = cut ? snapshot_selection(draft_, selections_)
                            : ChartDraft{{}, {}, {}, {}, {}, {}};
    clipboard_ = std::move(fragment);
    cut_source_ = std::move(source);
    pending_cut_ = cut;
    has_clipboard_ = true;
    return {ChartEditCode::Success, true};
  } catch (const std::bad_alloc&) {
    return {ChartEditCode::AllocationFailure, false};
  }
}

ChartSessionResult ChartEditor::copy_selection() noexcept {
  return prepare_clipboard(false);
}

ChartSessionResult ChartEditor::clear_clipboard() noexcept {
  const bool changed = has_clipboard_;
  clipboard_ = ChartClipboardFragment{};
  cut_source_ = ChartDraft{{}, {}, {}, {}, {}, {}};
  has_clipboard_ = false;
  pending_cut_ = false;
  return {ChartEditCode::Success, changed};
}

ChartSessionResult ChartEditor::cut_selection() noexcept {
  return prepare_clipboard(true);
}

ChartPastePreview ChartEditor::preview_paste(
    std::uint64_t anchor_row) const noexcept {
  try {
    return plan_paste(draft_, clipboard_, pending_cut_ ? &cut_source_ : nullptr,
                      anchor_row, {next_id_, id_available_}).preview;
  } catch (const std::bad_alloc&) {
    return {ChartEditCode::AllocationFailure, anchor_row, anchor_row, {}, {}};
  }
}

ChartEditResult ChartEditor::paste(std::uint64_t anchor_row) noexcept {
  try {
    const auto& content = clipboard_.content;
    const auto& source_events = content.role_events();
    const auto& source_drills = content.dance_move_drills();
    auto plan = plan_paste(draft_, clipboard_, pending_cut_ ? &cut_source_ : nullptr,
                           anchor_row, {next_id_, id_available_});
    if (!plan.preview) {
      return {plan.preview.code, false, {0U}};
    }
    const auto& group_ids = plan.group_ids;
    const auto& event_ids = plan.event_ids;
    const auto mapped_drill = [&source_drills, &plan](ChartDanceMoveDrillId id) {
      for (std::size_t index = 0U; index < source_drills.size(); ++index) {
        if (source_drills[index].id.value == id.value) {
          return ChartDanceMoveDrillId{plan.drill_ids[index]};
        }
      }
      return ChartDanceMoveDrillId{0U};
    };

    const auto mapped_group = [&content, &group_ids](ChartDrillGroupId id) {
      for (std::size_t index = 0U; index < content.drill_groups().size();
           ++index) {
        if (content.drill_groups()[index].id.value == id.value) {
          return ChartDrillGroupId{group_ids[index]};
        }
      }
      return ChartDrillGroupId{0U};
    };
    const auto mapped_event = [&source_events, &event_ids](ChartEventId id) {
      for (std::size_t index = 0U; index < source_events.size(); ++index) {
        if (source_events[index].id.value == id.value) {
          return ChartEventId{event_ids[index]};
        }
      }
      return ChartEventId{0U};
    };

    MutableDraft candidate{plan.destination};
    for (std::size_t index = 0U; index < source_events.size(); ++index) {
      ChartRoleEvent event = source_events[index];
      event.id = {event_ids[index]};
      event.row = anchor_row + source_events[index].row;
      event.drill_group_id = mapped_group(event.drill_group_id);
      event.dance_move_drill_id = mapped_drill(event.dance_move_drill_id);
      candidate.role_events.push_back(event);
    }
    for (std::size_t index = 0U; index < content.drill_groups().size();
         ++index) {
      ChartDrillGroup group = content.drill_groups()[index];
      group.id = {group_ids[index]};
      group.dance_move_drill_id = mapped_drill(group.dance_move_drill_id);
      for (auto& id : group.npc_event_ids) {
        id = mapped_event(id);
      }
      for (auto& id : group.player_event_ids) {
        id = mapped_event(id);
      }
      const auto drop_unmapped = [](std::vector<ChartEventId>& ids) {
        ids.erase(std::remove_if(ids.begin(), ids.end(),
                                 [](ChartEventId id) {
                                   return id.value == 0U;
                                 }),
                  ids.end());
      };
      drop_unmapped(group.npc_event_ids);
      drop_unmapped(group.player_event_ids);
      candidate.drill_groups.push_back(std::move(group));
    }
    for (const auto& source : source_drills) {
      ChartDanceMoveDrill drill{mapped_drill(source.id), source.move_n_count, {}};
      for (const auto id : source.drill_group_ids) {
        drill.drill_group_ids.push_back(mapped_group(id));
      }
      candidate.dance_move_drills.push_back(std::move(drill));
      sort_drill_group_ids(candidate, candidate.dance_move_drills.back());
    }
    sort_role_events(candidate);

    // Allocate the complete new selection before commit; no session mutation
    // after a successful draft commit may fail with an allocation error.
    auto pasted_selection = std::move(plan.preview.spans);
    for (auto& item : pasted_selection) {
      item.drill_id = mapped_drill(item.drill_id);
    }
    const ChartEditResult result{ChartEditCode::Success, true,
                                 ChartEventId{event_ids.front()}};
    auto committed = commit(std::move(candidate).finish(), result,
                             plan.ids.next, plan.ids.available);
    if (!committed.changed) {
      return committed;
    }
    selections_ = std::move(pasted_selection);
    pending_cut_ = false;
    cut_source_ = ChartDraft{{}, {}, {}, {}, {}, {}};
    return committed;
  } catch (const std::bad_alloc&) {
    return {ChartEditCode::AllocationFailure, false, {0U}};
  }
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
    // A recovered draft can contain a fully deleted group from an older editor.
    // Prepare only the candidate so completion sees the remaining live groups;
    // a rejected click still leaves the authoritative draft untouched.
    prune_eventless_groups(candidate);
    IdState ids{next_id_, id_available_};
    ChartEditResult result{ChartEditCode::CannotPlace, false, {0U}};
    const bool npc_command = column == ChartColumn::Npc;
    if (npc_command) {
      result = current == nullptr
                   ? click_empty_npc(draft_, candidate, markers, row, ids)
                   : click_existing_npc(candidate, markers, *current, ids);
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
    if (npc_command) {
      synchronize_npc_edit(draft_, markers, candidate, ids, result);
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
  revalidate_selection();
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
  revalidate_selection();
  return {ChartEditCode::Success, true, {0U}};
}

}  // namespace otgy::core
