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

}  // namespace otgy::core
