// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/timing/timeline_navigation.hpp"

#include <array>
#include <cassert>
#include <limits>

namespace {

using otgy::core::TimelineNavigation;
using otgy::core::TimelineNavigationError;
using otgy::core::TimelineNavigationResult;
using otgy::core::TimelineRange;

[[nodiscard]] TimelineRange default_range() {
  return TimelineRange{0.0, 9.6, 64.0, false};
}

[[nodiscard]] const TimelineNavigation& navigation_of(
    const TimelineNavigationResult& result) {
  assert(result);
  assert(result.error() == TimelineNavigationError::None);
  assert(result.navigation() != nullptr);
  return *result.navigation();
}

void assert_error(const TimelineNavigationResult& result,
                  TimelineNavigationError expected) {
  assert(!result);
  assert(result.navigation() == nullptr);
  assert(result.error() == expected);
}

void test_exact_unsnapped_page_targets() {
  const auto result = otgy::core::calculate_timeline_navigation(
      default_range(), 30.125, 4.5);
  const auto& navigation = navigation_of(result);
  assert(navigation.to_begin_row() == 0.0);
  assert(navigation.page_up_row() == 16.125);
  assert(navigation.page_down_row() == 44.125);
  assert(navigation.to_end_row() == 64.0);
  assert(navigation.can_to_begin());
  assert(navigation.can_page_up());
  assert(navigation.can_page_down());
  assert(navigation.can_to_end());
}

void test_boundary_clamping_and_states() {
  const auto near_begin = otgy::core::calculate_timeline_navigation(
      default_range(), 4.0, 5.0);
  const auto& begin_navigation = navigation_of(near_begin);
  assert(begin_navigation.page_up_row() == 0.0);
  assert(begin_navigation.page_down_row() == 20.0);
  assert(begin_navigation.can_page_up());

  const auto near_end = otgy::core::calculate_timeline_navigation(
      default_range(), 60.0, 5.0);
  const auto& end_navigation = navigation_of(near_end);
  assert(end_navigation.page_up_row() == 44.0);
  assert(end_navigation.page_down_row() == 64.0);
  assert(end_navigation.can_page_down());

  const auto at_begin = otgy::core::calculate_timeline_navigation(
      default_range(), 0.0, 5.0);
  const auto& at_begin_navigation = navigation_of(at_begin);
  assert(!at_begin_navigation.can_to_begin());
  assert(!at_begin_navigation.can_page_up());
  assert(at_begin_navigation.page_up_row() == 0.0);
  assert(at_begin_navigation.can_page_down());
  assert(at_begin_navigation.can_to_end());

  const auto at_end = otgy::core::calculate_timeline_navigation(
      default_range(), 64.0, 5.0);
  const auto& at_end_navigation = navigation_of(at_end);
  assert(at_end_navigation.can_to_begin());
  assert(at_end_navigation.can_page_up());
  assert(!at_end_navigation.can_page_down());
  assert(!at_end_navigation.can_to_end());
  assert(at_end_navigation.page_down_row() == 64.0);
}

void test_one_visible_beat_disables_only_paging() {
  const auto result = otgy::core::calculate_timeline_navigation(
      default_range(), 32.0, 1.0);
  const auto& navigation = navigation_of(result);
  assert(navigation.page_up_row() == 32.0);
  assert(navigation.page_down_row() == 32.0);
  assert(!navigation.can_page_up());
  assert(!navigation.can_page_down());
  assert(navigation.can_to_begin());
  assert(navigation.can_to_end());
}

void test_errors_and_messages() {
  constexpr double kNan = std::numeric_limits<double>::quiet_NaN();
  constexpr double kInfinity = std::numeric_limits<double>::infinity();

  for (const TimelineRange range : {
           TimelineRange{0.0, 0.0, -1.0, false},
           TimelineRange{0.0, 0.0, kInfinity, false},
       }) {
    assert_error(otgy::core::calculate_timeline_navigation(
                     range, 0.0, 5.0),
                 TimelineNavigationError::InvalidRange);
  }
  for (const double position : {kNan, -0.001, 64.001}) {
    assert_error(otgy::core::calculate_timeline_navigation(
                     default_range(), position, 5.0),
                 TimelineNavigationError::InvalidPosition);
  }
  for (const double visible_beats : {kNan, kInfinity, 0.999}) {
    assert_error(otgy::core::calculate_timeline_navigation(
                     default_range(), 32.0, visible_beats),
                 TimelineNavigationError::InvalidVisibleBeatCount);
  }
  assert_error(otgy::core::calculate_timeline_navigation(
                   TimelineRange{0.0, 0.0,
                                 std::numeric_limits<double>::max(), false},
                   std::numeric_limits<double>::max(),
                   std::numeric_limits<double>::max()),
               TimelineNavigationError::ResultOutOfRange);

  assert(otgy::core::timeline_navigation_error_message(
             TimelineNavigationError::None)
             .empty());
  constexpr std::array<TimelineNavigationError, 4U> kErrors{{
      TimelineNavigationError::InvalidRange,
      TimelineNavigationError::InvalidPosition,
      TimelineNavigationError::InvalidVisibleBeatCount,
      TimelineNavigationError::ResultOutOfRange,
  }};
  for (const auto error : kErrors) {
    assert(!otgy::core::timeline_navigation_error_message(error).empty());
  }
  assert(!otgy::core::timeline_navigation_error_message(
              static_cast<TimelineNavigationError>(255))
              .empty());
}

}  // namespace

int main() {
  test_exact_unsnapped_page_targets();
  test_boundary_clamping_and_states();
  test_one_visible_beat_disables_only_paging();
  test_errors_and_messages();
  return 0;
}
