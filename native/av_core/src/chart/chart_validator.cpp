// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_validator.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace otgy::core {
namespace {

struct EventKey final {
  std::uint64_t id;
  std::uint64_t row;
};

struct BlockSummary final {
  bool valid;
  std::vector<const ChartRoleEvent*> events;
  std::uint64_t n_count;
  bool has_end;
};

void add_diagnostic(std::vector<ChartDiagnostic>& diagnostics,
                    ChartDiagnosticCode code, std::uint64_t object_id = 0U,
                    std::uint64_t related_id = 0U, std::uint64_t row = 0U) {
  diagnostics.push_back({code, object_id, related_id, row});
}

[[nodiscard]] const ChartRoleEvent* find_role_event(const ChartDraft& draft,
                                                    std::uint64_t id) noexcept {
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

[[nodiscard]] const ChartDrillGroup* find_group(const ChartDraft& draft,
                                                std::uint64_t id) noexcept {
  for (const auto& group : draft.drill_groups()) {
    if (group.id.value == id) {
      return &group;
    }
  }
  return nullptr;
}

[[nodiscard]] const ChartDanceMoveDrill* find_drill(const ChartDraft& draft,
                                                    std::uint64_t id) noexcept {
  for (const auto& drill : draft.dance_move_drills()) {
    if (drill.id.value == id) {
      return &drill;
    }
  }
  return nullptr;
}

template <typename Collection, typename Id>
void diagnose_duplicate_ids(const Collection& values, Id id_of,
                            ChartDiagnosticCode code,
                            std::vector<ChartDiagnostic>& diagnostics) {
  for (std::size_t index = 0U; index < values.size(); ++index) {
    for (std::size_t other = index + 1U; other < values.size(); ++other) {
      if (id_of(values[index]) == id_of(values[other])) {
        add_diagnostic(diagnostics, code, id_of(values[other]));
      }
    }
  }
}

[[nodiscard]] BlockSummary summarize_block(
    const ChartDraft& draft, const std::vector<ChartEventId>& ids,
    ChartRole role, const ChartDrillGroup& group,
    std::vector<ChartDiagnostic>& diagnostics) {
  BlockSummary summary{true, {}, 0U, false};
  for (const auto id : ids) {
    const auto* event = find_role_event(draft, id.value);
    if (event == nullptr) {
      add_diagnostic(diagnostics, ChartDiagnosticCode::MissingReference,
                     group.id.value, id.value);
      summary.valid = false;
      continue;
    }
    if (event->role != role || event->drill_group_id.value != group.id.value ||
        event->dance_move_drill_id.value != group.dance_move_drill_id.value) {
      add_diagnostic(diagnostics, ChartDiagnosticCode::InvalidEventRelation,
                     event->id.value, group.id.value, event->row);
      summary.valid = false;
    }
    summary.events.push_back(event);
  }

  if (summary.events.size() < 2U ||
      summary.events.front()->type != ChartRoleEventType::BlockStart) {
    summary.valid = false;
  }
  for (std::size_t index = 0U; index < summary.events.size(); ++index) {
    const auto* event = summary.events[index];
    if (index > 0U && summary.events[index - 1U]->row >= event->row) {
      summary.valid = false;
    }
    if (index == 0U) {
      continue;
    }
    if (event->type == ChartRoleEventType::Note) {
      if (summary.has_end) {
        summary.valid = false;
      }
      ++summary.n_count;
    } else if (event->type == ChartRoleEventType::MoveEnd &&
               index + 1U == summary.events.size() && !summary.has_end) {
      summary.has_end = true;
    } else {
      summary.valid = false;
    }
  }
  if (summary.n_count == 0U) {
    summary.valid = false;
  }
  if (!summary.valid) {
    add_diagnostic(diagnostics, ChartDiagnosticCode::InvalidDrillBlock,
                   group.id.value, static_cast<std::uint64_t>(role));
  }
  return summary;
}

[[nodiscard]] bool blocks_match(const BlockSummary& npc,
                                const BlockSummary& player) noexcept {
  if (!npc.valid || !player.valid ||
      npc.events.size() != player.events.size() ||
      npc.n_count != player.n_count || npc.has_end != player.has_end) {
    return false;
  }
  const auto npc_start = npc.events[1U]->row;
  const auto player_start = player.events[1U]->row;
  for (std::size_t index = 0U; index < npc.events.size(); ++index) {
    if (npc.events[index]->type != player.events[index]->type) {
      return false;
    }
    if (index > 0U && npc.events[index]->row - npc_start !=
                          player.events[index]->row - player_start) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::size_t role_event_occurrences(const ChartDraft& draft,
                                                 std::uint64_t id) noexcept {
  std::size_t count = 0U;
  for (const auto& group : draft.drill_groups()) {
    count += static_cast<std::size_t>(
        std::count_if(group.npc_event_ids.begin(), group.npc_event_ids.end(),
                      [id](ChartEventId value) { return value.value == id; }));
    count += static_cast<std::size_t>(std::count_if(
        group.player_event_ids.begin(), group.player_event_ids.end(),
        [id](ChartEventId value) { return value.value == id; }));
  }
  return count;
}

[[nodiscard]] std::size_t start_event_occurrences(const ChartDraft& draft,
                                                  std::uint64_t id) noexcept {
  return static_cast<std::size_t>(std::count_if(
      draft.showtimes().begin(), draft.showtimes().end(),
      [id](const ChartShowtime& showtime) {
        return showtime.start_event_id.value == id;
      }));
}

[[nodiscard]] std::size_t move_event_occurrences(const ChartDraft& draft,
                                                 std::uint64_t id) noexcept {
  std::size_t count = 0U;
  for (const auto& showtime : draft.showtimes()) {
    count += static_cast<std::size_t>(std::count_if(
        showtime.show_move_event_ids.begin(),
        showtime.show_move_event_ids.end(),
        [id](ChartEventId value) { return value.value == id; }));
  }
  return count;
}

template <typename Event>
void diagnose_ordered_event(const Event* event, bool& has_previous,
                            EventKey& previous,
                            std::vector<ChartDiagnostic>& diagnostics) {
  if (event == nullptr) {
    return;
  }
  if (has_previous && previous.row >= event->row) {
    add_diagnostic(diagnostics, ChartDiagnosticCode::InvalidEventRelation,
                   event->id.value, previous.id, event->row);
  }
  previous = {event->id.value, event->row};
  has_previous = true;
}

void diagnose_finished_event_order(
    const ChartDraft& draft, std::vector<ChartDiagnostic>& diagnostics) {
  bool has_previous = false;
  EventKey previous{0U, 0U};
  for (const auto& showtime : draft.showtimes()) {
    for (const auto drill_id : showtime.dance_move_drill_ids) {
      const auto* drill = find_drill(draft, drill_id.value);
      if (drill == nullptr) {
        continue;
      }
      for (const auto group_id : drill->drill_group_ids) {
        const auto* group = find_group(draft, group_id.value);
        if (group == nullptr) {
          continue;
        }
        for (const auto event_id : group->npc_event_ids) {
          diagnose_ordered_event(find_role_event(draft, event_id.value),
                                 has_previous, previous, diagnostics);
        }
        for (const auto event_id : group->player_event_ids) {
          diagnose_ordered_event(find_role_event(draft, event_id.value),
                                 has_previous, previous, diagnostics);
        }
      }
    }
    diagnose_ordered_event(
        find_start_event(draft, showtime.start_event_id.value), has_previous,
        previous, diagnostics);
    for (const auto event_id : showtime.show_move_event_ids) {
      diagnose_ordered_event(find_move_event(draft, event_id.value),
                             has_previous, previous, diagnostics);
    }
  }
}

}  // namespace

FinishedChart::FinishedChart(ChartDraft draft) noexcept
    : draft_(std::move(draft)) {}

ChartValidationResult::ChartValidationResult(FinishedChart chart) noexcept
    : storage_(std::move(chart)) {}

ChartValidationResult::ChartValidationResult(
    std::vector<ChartDiagnostic> diagnostics) noexcept
    : storage_(std::move(diagnostics)) {}

ChartValidationResult::ChartValidationResult(
    ChartValidationError error) noexcept
    : storage_(error) {}

bool ChartValidationResult::is_finished() const noexcept {
  return std::holds_alternative<FinishedChart>(storage_);
}

const FinishedChart* ChartValidationResult::finished_chart() const noexcept {
  return std::get_if<FinishedChart>(&storage_);
}

const std::vector<ChartDiagnostic>* ChartValidationResult::diagnostics()
    const noexcept {
  return std::get_if<std::vector<ChartDiagnostic>>(&storage_);
}

ChartValidationError ChartValidationResult::error() const noexcept {
  const auto* error = std::get_if<ChartValidationError>(&storage_);
  return error == nullptr ? ChartValidationError::None : *error;
}

FinishedChart ChartValidationResult::take_finished_chart() && {
  return std::move(std::get<FinishedChart>(storage_));
}

ChartValidationResult validate_finished_chart(
    const ChartDraft& draft) noexcept {
  try {
    std::vector<ChartDiagnostic> diagnostics;
    if (draft.showtimes().empty()) {
      add_diagnostic(diagnostics, ChartDiagnosticCode::EmptyChart);
    }

    std::vector<EventKey> event_keys;
    for (const auto& event : draft.role_events()) {
      event_keys.push_back({event.id.value, event.row});
    }
    for (const auto& event : draft.showtime_start_events()) {
      event_keys.push_back({event.id.value, event.row});
    }
    for (const auto& event : draft.showtime_move_events()) {
      event_keys.push_back({event.id.value, event.row});
    }
    for (std::size_t index = 0U; index < event_keys.size(); ++index) {
      for (std::size_t other = index + 1U; other < event_keys.size(); ++other) {
        if (event_keys[index].id == event_keys[other].id) {
          add_diagnostic(diagnostics, ChartDiagnosticCode::DuplicateEventId,
                         event_keys[other].id);
        }
        if (event_keys[index].row == event_keys[other].row) {
          add_diagnostic(diagnostics, ChartDiagnosticCode::DuplicateEventRow,
                         event_keys[other].id, event_keys[index].id,
                         event_keys[other].row);
        }
      }
    }

    diagnose_duplicate_ids(
        draft.drill_groups(),
        [](const ChartDrillGroup& value) { return value.id.value; },
        ChartDiagnosticCode::DuplicateDrillGroupId, diagnostics);
    diagnose_duplicate_ids(
        draft.dance_move_drills(),
        [](const ChartDanceMoveDrill& value) { return value.id.value; },
        ChartDiagnosticCode::DuplicateDanceMoveDrillId, diagnostics);
    diagnose_duplicate_ids(
        draft.showtimes(),
        [](const ChartShowtime& value) { return value.id.value; },
        ChartDiagnosticCode::DuplicateShowtimeId, diagnostics);

    for (const auto& group : draft.drill_groups()) {
      const auto npc = summarize_block(draft, group.npc_event_ids,
                                       ChartRole::Npc, group, diagnostics);
      const auto player = summarize_block(
          draft, group.player_event_ids, ChartRole::Player, group, diagnostics);
      if (!blocks_match(npc, player)) {
        add_diagnostic(diagnostics, ChartDiagnosticCode::DrillBlockMismatch,
                       group.id.value);
      }
      if (find_drill(draft, group.dance_move_drill_id.value) == nullptr) {
        add_diagnostic(diagnostics, ChartDiagnosticCode::MissingReference,
                       group.id.value, group.dance_move_drill_id.value);
      }
    }

    for (const auto& drill : draft.dance_move_drills()) {
      bool valid = drill.move_n_count >= 1U && drill.move_n_count <= 11U &&
                   !drill.drill_group_ids.empty();
      std::uint64_t total_n_count = 0U;
      for (std::size_t index = 0U; index < drill.drill_group_ids.size();
           ++index) {
        const auto* group =
            find_group(draft, drill.drill_group_ids[index].value);
        if (group == nullptr) {
          add_diagnostic(diagnostics, ChartDiagnosticCode::MissingReference,
                         drill.id.value, drill.drill_group_ids[index].value);
          valid = false;
          continue;
        }
        if (group->dance_move_drill_id.value != drill.id.value) {
          valid = false;
        }
        const auto npc = summarize_block(draft, group->npc_event_ids,
                                         ChartRole::Npc, *group, diagnostics);
        const bool should_end = index + 1U == drill.drill_group_ids.size();
        if (!npc.valid || npc.has_end != should_end) {
          valid = false;
        }
        total_n_count += npc.n_count;
      }
      if (total_n_count != drill.move_n_count) {
        valid = false;
      }
      if (!valid) {
        add_diagnostic(diagnostics, ChartDiagnosticCode::InvalidDanceMoveDrill,
                       drill.id.value, 0U, total_n_count);
      }
    }

    for (const auto& showtime : draft.showtimes()) {
      bool valid = !showtime.dance_move_drill_ids.empty() &&
                   !showtime.show_move_event_ids.empty() &&
                   showtime.show_move_event_ids.size() <=
                       showtime.dance_move_drill_ids.size();
      const auto* start =
          find_start_event(draft, showtime.start_event_id.value);
      if (start == nullptr) {
        add_diagnostic(diagnostics, ChartDiagnosticCode::MissingReference,
                       showtime.id.value, showtime.start_event_id.value);
        valid = false;
      } else if (start->showtime_id.value != showtime.id.value) {
        valid = false;
      }
      const auto pair_count = std::min(showtime.dance_move_drill_ids.size(),
                                       showtime.show_move_event_ids.size());
      for (std::size_t index = 0U; index < pair_count; ++index) {
        const auto* drill =
            find_drill(draft, showtime.dance_move_drill_ids[index].value);
        const auto* event =
            find_move_event(draft, showtime.show_move_event_ids[index].value);
        if (drill == nullptr || event == nullptr) {
          add_diagnostic(diagnostics, ChartDiagnosticCode::MissingReference,
                         showtime.id.value,
                         drill == nullptr
                             ? showtime.dance_move_drill_ids[index].value
                             : showtime.show_move_event_ids[index].value);
          valid = false;
          continue;
        }
        if (event->showtime_id.value != showtime.id.value ||
            event->dance_move_drill_id.value != drill->id.value ||
            event->show_n_count != drill->move_n_count) {
          valid = false;
        }
        if (event->show_n_count >= 1U && event->show_n_count <= 11U) {
          const auto range_start = event->row - event->row % 16U;
          const std::uint64_t duration =
              event->show_n_count <= 6U ? 16U : 32U;
          if (range_start >
              std::numeric_limits<std::uint64_t>::max() - duration) {
            valid = false;
          }
        }
      }
      if (!valid) {
        add_diagnostic(diagnostics, ChartDiagnosticCode::InvalidShowtime,
                       showtime.id.value);
      }
    }

    for (const auto& event : draft.role_events()) {
      if (role_event_occurrences(draft, event.id.value) != 1U) {
        add_diagnostic(diagnostics, ChartDiagnosticCode::OrphanEvent,
                       event.id.value, 0U, event.row);
      }
    }
    for (const auto& event : draft.showtime_start_events()) {
      if (start_event_occurrences(draft, event.id.value) != 1U) {
        add_diagnostic(diagnostics, ChartDiagnosticCode::OrphanEvent,
                       event.id.value, 0U, event.row);
      }
    }
    for (const auto& event : draft.showtime_move_events()) {
      if (move_event_occurrences(draft, event.id.value) != 1U) {
        add_diagnostic(diagnostics, ChartDiagnosticCode::OrphanEvent,
                       event.id.value, 0U, event.row);
      }
    }
    for (const auto& group : draft.drill_groups()) {
      std::size_t count = 0U;
      for (const auto& drill : draft.dance_move_drills()) {
        count += static_cast<std::size_t>(std::count_if(
            drill.drill_group_ids.begin(), drill.drill_group_ids.end(),
            [&group](ChartDrillGroupId id) {
              return id.value == group.id.value;
            }));
      }
      if (count != 1U) {
        add_diagnostic(diagnostics, ChartDiagnosticCode::OrphanDrillGroup,
                       group.id.value);
      }
    }
    for (const auto& drill : draft.dance_move_drills()) {
      std::size_t count = 0U;
      for (const auto& showtime : draft.showtimes()) {
        count += static_cast<std::size_t>(
            std::count_if(showtime.dance_move_drill_ids.begin(),
                          showtime.dance_move_drill_ids.end(),
                          [&drill](ChartDanceMoveDrillId id) {
                            return id.value == drill.id.value;
                          }));
      }
      if (count != 1U) {
        add_diagnostic(diagnostics, ChartDiagnosticCode::OrphanDanceMoveDrill,
                       drill.id.value);
      }
    }

    diagnose_finished_event_order(draft, diagnostics);

    if (!diagnostics.empty()) {
      return ChartValidationResult{std::move(diagnostics)};
    }
    return ChartValidationResult{FinishedChart{draft}};
  } catch (const std::bad_alloc&) {
    return ChartValidationResult{ChartValidationError::AllocationFailure};
  }
}

}  // namespace otgy::core
