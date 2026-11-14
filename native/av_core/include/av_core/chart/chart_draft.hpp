// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace otgy::core {

struct ChartEventId final {
  std::uint64_t value;
};

struct ChartDrillGroupId final {
  std::uint64_t value;
};

struct ChartDanceMoveDrillId final {
  std::uint64_t value;
};

struct ChartShowtimeId final {
  std::uint64_t value;
};

enum class ChartRole : std::uint8_t {
  Npc = 0,
  Player = 1,
};

enum class ChartRoleEventType : std::uint8_t {
  BlockStart = 0,
  Note = 1,
  MoveEnd = 2,
};

struct ChartRoleEvent final {
  ChartEventId id;
  std::uint64_t row;
  ChartRole role;
  ChartRoleEventType type;
  ChartDrillGroupId drill_group_id;
  ChartDanceMoveDrillId dance_move_drill_id;
};

struct ChartShowtimeStartEvent final {
  ChartEventId id;
  std::uint64_t row;
  ChartShowtimeId showtime_id;
};

struct ChartShowtimeMoveEvent final {
  ChartEventId id;
  std::uint64_t row;
  ChartShowtimeId showtime_id;
  ChartDanceMoveDrillId dance_move_drill_id;
  std::uint64_t show_n_count;
};

struct ChartDrillGroup final {
  ChartDrillGroupId id;
  ChartDanceMoveDrillId dance_move_drill_id;
  std::vector<ChartEventId> npc_event_ids;
  std::vector<ChartEventId> player_event_ids;
};

struct ChartDanceMoveDrill final {
  ChartDanceMoveDrillId id;
  std::uint64_t move_n_count;
  std::vector<ChartDrillGroupId> drill_group_ids;
};

struct ChartShowtime final {
  ChartShowtimeId id;
  ChartEventId start_event_id;
  std::vector<ChartDanceMoveDrillId> dance_move_drill_ids;
  std::vector<ChartEventId> show_move_event_ids;
};

class ChartDraft final {
 public:
  ChartDraft(std::vector<ChartRoleEvent> role_events,
             std::vector<ChartShowtimeStartEvent> showtime_start_events,
             std::vector<ChartShowtimeMoveEvent> showtime_move_events,
             std::vector<ChartDrillGroup> drill_groups,
             std::vector<ChartDanceMoveDrill> dance_move_drills,
             std::vector<ChartShowtime> showtimes) noexcept;

  [[nodiscard]] const std::vector<ChartRoleEvent>& role_events()
      const noexcept {
    return role_events_;
  }
  [[nodiscard]] const std::vector<ChartShowtimeStartEvent>&
  showtime_start_events() const noexcept {
    return showtime_start_events_;
  }
  [[nodiscard]] const std::vector<ChartShowtimeMoveEvent>&
  showtime_move_events() const noexcept {
    return showtime_move_events_;
  }
  [[nodiscard]] const std::vector<ChartDrillGroup>& drill_groups()
      const noexcept {
    return drill_groups_;
  }
  [[nodiscard]] const std::vector<ChartDanceMoveDrill>& dance_move_drills()
      const noexcept {
    return dance_move_drills_;
  }
  [[nodiscard]] const std::vector<ChartShowtime>& showtimes() const noexcept {
    return showtimes_;
  }

 private:
  std::vector<ChartRoleEvent> role_events_;
  std::vector<ChartShowtimeStartEvent> showtime_start_events_;
  std::vector<ChartShowtimeMoveEvent> showtime_move_events_;
  std::vector<ChartDrillGroup> drill_groups_;
  std::vector<ChartDanceMoveDrill> dance_move_drills_;
  std::vector<ChartShowtime> showtimes_;
};

/// Exact semantic equality used by persistence, dirty-state and history tests.
/// Container order is part of the encoded draft snapshot.
[[nodiscard]] bool chart_drafts_equal(const ChartDraft& left,
                                      const ChartDraft& right) noexcept;

/// First row bound to the drill through its groups, or nullopt when the drill
/// has no reachable events. Showtime-less drills are ordered by this row.
[[nodiscard]] std::optional<std::uint64_t> dance_move_drill_first_row(
    const ChartDraft& draft, ChartDanceMoveDrillId id) noexcept;

/// First N row bound to the drill through its groups, or nullopt when the drill
/// has no N events. A block start is not part of the dance move itself, so a
/// move whose NPC-C sits in the previous Showtime's open half measure is still
/// attributed to the next stage.
[[nodiscard]] std::optional<std::uint64_t> dance_move_drill_first_note_row(
    const ChartDraft& draft, ChartDanceMoveDrillId id) noexcept;

}  // namespace otgy::core
