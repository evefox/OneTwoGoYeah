// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_projection.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace otgy::core {
namespace {

[[nodiscard]] bool project_role_event(const ChartRoleEvent& event,
                                      ChartMarker& marker) noexcept {
  marker.id = event.id;
  marker.row = event.row;
  marker.show_n_count = 0U;
  switch (event.role) {
    case ChartRole::Npc:
      marker.column = ChartColumn::Npc;
      switch (event.type) {
        case ChartRoleEventType::BlockStart:
          marker.type = ChartMarkerType::NpcBlockStart;
          return true;
        case ChartRoleEventType::Note:
          marker.type = ChartMarkerType::NpcNote;
          return true;
        case ChartRoleEventType::MoveEnd:
          marker.type = ChartMarkerType::NpcMoveEnd;
          return true;
      }
      return false;
    case ChartRole::Player:
      marker.column = ChartColumn::Player;
      switch (event.type) {
        case ChartRoleEventType::BlockStart:
          marker.type = ChartMarkerType::PlayerBlockStart;
          return true;
        case ChartRoleEventType::Note:
          marker.type = ChartMarkerType::PlayerNote;
          return true;
        case ChartRoleEventType::MoveEnd:
          marker.type = ChartMarkerType::PlayerMoveEnd;
          return true;
      }
      return false;
  }
  return false;
}

[[nodiscard]] bool marker_less(const ChartMarker& left,
                               const ChartMarker& right) noexcept {
  if (left.row != right.row) {
    return left.row < right.row;
  }
  if (left.id.value != right.id.value) {
    return left.id.value < right.id.value;
  }
  if (left.column != right.column) {
    return static_cast<std::uint8_t>(left.column) <
           static_cast<std::uint8_t>(right.column);
  }
  return static_cast<std::uint8_t>(left.type) <
         static_cast<std::uint8_t>(right.type);
}

}  // namespace

ChartProjectionResult::ChartProjectionResult(
    std::vector<ChartMarker> markers) noexcept
    : storage_(std::move(markers)) {}

ChartProjectionResult::ChartProjectionResult(ChartProjectionError error) noexcept
    : storage_(error) {}

ChartProjectionResult::operator bool() const noexcept {
  return std::holds_alternative<std::vector<ChartMarker>>(storage_);
}

const std::vector<ChartMarker>* ChartProjectionResult::markers()
    const noexcept {
  return std::get_if<std::vector<ChartMarker>>(&storage_);
}

ChartProjectionError ChartProjectionResult::error() const noexcept {
  const auto* error = std::get_if<ChartProjectionError>(&storage_);
  return error == nullptr ? ChartProjectionError::None : *error;
}

std::vector<ChartMarker> ChartProjectionResult::take_markers() && {
  return std::move(std::get<std::vector<ChartMarker>>(storage_));
}

ChartProjectionResult project_chart_markers(const ChartDraft& draft) noexcept {
  try {
    std::vector<ChartMarker> markers;
    markers.reserve(draft.role_events().size() +
                    draft.showtime_start_events().size() +
                    draft.showtime_move_events().size());
    for (const auto& event : draft.role_events()) {
      ChartMarker marker{};
      if (!project_role_event(event, marker)) {
        return ChartProjectionResult{ChartProjectionError::InvalidDraft};
      }
      markers.push_back(marker);
    }
    for (const auto& event : draft.showtime_start_events()) {
      markers.push_back({event.id, event.row, ChartColumn::Showtime,
                         ChartMarkerType::ShowtimeStart, 0U});
    }
    for (const auto& event : draft.showtime_move_events()) {
      markers.push_back({event.id, event.row, ChartColumn::Showtime,
                         ChartMarkerType::ShowtimeMove, event.show_n_count});
    }
    std::sort(markers.begin(), markers.end(), marker_less);
    return ChartProjectionResult{std::move(markers)};
  } catch (const std::bad_alloc&) {
    return ChartProjectionResult{ChartProjectionError::AllocationFailure};
  }
}

const ChartMarker* find_chart_marker(
    const std::vector<ChartMarker>& markers, ChartEventId id) noexcept {
  for (const auto& marker : markers) {
    if (marker.id.value == id.value) {
      return &marker;
    }
  }
  return nullptr;
}

const ChartMarker* previous_chart_marker(
    const std::vector<ChartMarker>& markers, std::uint64_t row) noexcept {
  const ChartMarker* result = nullptr;
  for (const auto& marker : markers) {
    if (marker.row >= row) {
      break;
    }
    result = &marker;
  }
  return result;
}

const ChartMarker* previous_chart_marker_in_column(
    const std::vector<ChartMarker>& markers, ChartColumn column,
    std::uint64_t row) noexcept {
  const ChartMarker* result = nullptr;
  for (const auto& marker : markers) {
    if (marker.row >= row) {
      break;
    }
    if (marker.column == column) {
      result = &marker;
    }
  }
  return result;
}

const ChartMarker* next_chart_marker(
    const std::vector<ChartMarker>& markers, std::uint64_t row) noexcept {
  for (const auto& marker : markers) {
    if (marker.row > row) {
      return &marker;
    }
  }
  return nullptr;
}

ShowtimeMoveProjectionResult::ShowtimeMoveProjectionResult(
    std::vector<ShowtimeMoveProjection> moves) noexcept
    : storage_(std::move(moves)) {}

ShowtimeMoveProjectionResult::ShowtimeMoveProjectionResult(
    ChartProjectionError error) noexcept
    : storage_(error) {}

ShowtimeMoveProjectionResult::operator bool() const noexcept {
  return std::holds_alternative<std::vector<ShowtimeMoveProjection>>(storage_);
}

const std::vector<ShowtimeMoveProjection>*
ShowtimeMoveProjectionResult::moves() const noexcept {
  return std::get_if<std::vector<ShowtimeMoveProjection>>(&storage_);
}

ChartProjectionError ShowtimeMoveProjectionResult::error() const noexcept {
  const auto* error = std::get_if<ChartProjectionError>(&storage_);
  return error == nullptr ? ChartProjectionError::None : *error;
}

std::vector<ShowtimeMoveProjection>
ShowtimeMoveProjectionResult::take_moves() && {
  return std::move(std::get<std::vector<ShowtimeMoveProjection>>(storage_));
}

ShowtimeMoveProjectionResult project_showtime_moves(
    const ChartDraft& draft) noexcept {
  const auto marker_result = project_chart_markers(draft);
  if (!marker_result) {
    return ShowtimeMoveProjectionResult{marker_result.error()};
  }
  try {
    std::vector<ShowtimeMoveProjection> result;
    result.reserve(draft.showtime_move_events().size());
    const auto& markers = *marker_result.markers();
    for (std::size_t index = 0U; index < markers.size(); ++index) {
      const auto& marker = markers[index];
      if (marker.type != ChartMarkerType::ShowtimeMove) {
        continue;
      }
      if (marker.show_n_count == 0U) {
        return ShowtimeMoveProjectionResult{
            ChartProjectionError::InvalidShowNCount};
      }
      const auto range_start = marker.row - marker.row % 16U;
      const std::uint64_t duration =
          marker.show_n_count <= 6U ? 16U : 32U;
      if (range_start >
          std::numeric_limits<std::uint64_t>::max() - duration) {
        return ShowtimeMoveProjectionResult{
            ChartProjectionError::RangeOverflow};
      }
      const bool is_first =
          index == 0U ||
          markers[index - 1U].type != ChartMarkerType::ShowtimeMove;
      const bool is_last =
          index + 1U == markers.size() ||
          markers[index + 1U].type != ChartMarkerType::ShowtimeMove;
      result.push_back({marker.id, marker.row, marker.show_n_count, range_start,
                        range_start + duration, is_first, is_last});
    }
    return ShowtimeMoveProjectionResult{std::move(result)};
  } catch (const std::bad_alloc&) {
    return ShowtimeMoveProjectionResult{
        ChartProjectionError::AllocationFailure};
  }
}

ChartNoteOrdinalResult chart_note_ordinal(const ChartDraft& draft,
                                          ChartEventId event_id) noexcept {
  const ChartRoleEvent* current = nullptr;
  for (const auto& event : draft.role_events()) {
    if (event.id.value == event_id.value) {
      current = &event;
      break;
    }
  }
  if (current == nullptr) {
    return {ChartNoteOrdinalError::EventNotFound, 0U};
  }
  if (current->type != ChartRoleEventType::Note) {
    return {ChartNoteOrdinalError::EventIsNotNote, 0U};
  }

  bool has_previous_end = false;
  std::uint64_t previous_end_row = 0U;
  for (const auto& event : draft.role_events()) {
    if (event.role == current->role &&
        event.type == ChartRoleEventType::MoveEnd &&
        event.row < current->row &&
        (!has_previous_end || event.row > previous_end_row)) {
      previous_end_row = event.row;
      has_previous_end = true;
    }
  }

  std::uint64_t ordinal = 1U;
  for (const auto& event : draft.role_events()) {
    if (event.role == current->role &&
        event.type == ChartRoleEventType::Note && event.row < current->row &&
        (!has_previous_end || event.row > previous_end_row)) {
      ++ordinal;
    }
  }
  return {ChartNoteOrdinalError::None, ordinal};
}

ChartTimelineRangeResult::ChartTimelineRangeResult(
    TimelineRange range) noexcept
    : storage_(std::move(range)) {}

ChartTimelineRangeResult::ChartTimelineRangeResult(
    ChartTimelineRangeFailure failure) noexcept
    : storage_(failure) {}

ChartTimelineRangeResult::operator bool() const noexcept {
  return std::holds_alternative<TimelineRange>(storage_);
}

const TimelineRange* ChartTimelineRangeResult::range() const noexcept {
  return std::get_if<TimelineRange>(&storage_);
}

const ChartTimelineRangeFailure* ChartTimelineRangeResult::failure()
    const noexcept {
  return std::get_if<ChartTimelineRangeFailure>(&storage_);
}

ChartTimelineRangeResult calculate_chart_timeline_range(
    const ChartDraft& draft, TimingParameters timing,
    std::optional<double> audio_end_seconds) noexcept {
  const auto marker_result = project_chart_markers(draft);
  if (!marker_result) {
    return ChartTimelineRangeResult{{
        ChartTimelineRangeError::ChartProjectionFailed,
        marker_result.error(), TimelineRangeError::None}};
  }
  const auto showtime_result = project_showtime_moves(draft);
  if (!showtime_result) {
    return ChartTimelineRangeResult{{
        ChartTimelineRangeError::ChartProjectionFailed,
        showtime_result.error(), TimelineRangeError::None}};
  }
  try {
    std::vector<std::uint64_t> event_rows;
    std::vector<std::uint64_t> show_range_end_rows;
    event_rows.reserve(marker_result.markers()->size());
    show_range_end_rows.reserve(showtime_result.moves()->size());
    for (const auto& marker : *marker_result.markers()) {
      event_rows.push_back(marker.row);
    }
    for (const auto& move : *showtime_result.moves()) {
      show_range_end_rows.push_back(move.range_end);
    }
    const auto range_result = calculate_timeline_range(
        timing, audio_end_seconds, event_rows, show_range_end_rows);
    if (!range_result) {
      return ChartTimelineRangeResult{{
          ChartTimelineRangeError::TimelineRangeFailed,
          ChartProjectionError::None, range_result.error()}};
    }
    return ChartTimelineRangeResult{*range_result.range()};
  } catch (const std::bad_alloc&) {
    return ChartTimelineRangeResult{{
        ChartTimelineRangeError::AllocationFailure,
        ChartProjectionError::None, TimelineRangeError::None}};
  }
}

}  // namespace otgy::core
