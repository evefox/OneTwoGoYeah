// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/audio/audio_export_name.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <utility>

namespace {

using otgy::core::AudioExportLocalTime;
using otgy::core::AudioExportNameError;

void test_normalization() {
  const auto unicode = otgy::core::normalize_audio_export_base_name(
      "  歌曲:名称.  ");
  assert(unicode && *unicode.name() == "歌曲_名称");

  const auto illegal = otgy::core::normalize_audio_export_base_name(
      "a<b>c:d\"e/f\\g|h?i*j");
  assert(illegal && *illegal.name() == "a_b_c_d_e_f_g_h_i_j");

  const std::string controls{"a\0b\xC2\x85" "c", 6U};
  const auto control =
      otgy::core::normalize_audio_export_base_name(controls);
  assert(control && *control.name() == "a_b_c");

  const std::string invalid{"bad\xFFname", 8U};
  const auto invalid_utf8 =
      otgy::core::normalize_audio_export_base_name(invalid);
  assert(invalid_utf8 && *invalid_utf8.name() == "bad_name");

  assert(*otgy::core::normalize_audio_export_base_name(" . . ").name() ==
         "audio");
  assert(*otgy::core::normalize_audio_export_base_name("con").name() ==
         "_con");
  assert(*otgy::core::normalize_audio_export_base_name("COM9").name() ==
         "_COM9");
  assert(*otgy::core::normalize_audio_export_base_name("COM0").name() ==
         "COM0");
  assert(*otgy::core::normalize_audio_export_base_name("LPT10").name() ==
         "LPT10");
}

void test_default_base_name() {
  assert(*otgy::core::default_audio_export_base_name(
              "/music/song.final.MP3")
              .name() == "song.final");
  assert(*otgy::core::default_audio_export_base_name("C:\\music\\track.ogg")
              .name() == "track");
  assert(*otgy::core::default_audio_export_base_name("").name() == "audio");
  assert(*otgy::core::default_audio_export_base_name(".mp3").name() ==
         "audio");
}

void test_timestamp_and_collisions() {
  const AudioExportLocalTime time{2026U, 8U, 30U, 14U, 25U, 0U};
  const auto first =
      otgy::core::make_audio_export_file_name(" song ", time);
  auto second =
      otgy::core::make_audio_export_file_name("song", time, 2U);
  const auto tenth =
      otgy::core::make_audio_export_file_name("song", time, 10U);
  assert(first && *first.name() == "song_20260830_142500.ogg");
  assert(second && *second.name() == "song_20260830_142500_2.ogg");
  assert(tenth && *tenth.name() == "song_20260830_142500_10.ogg");

  const auto copy = *first.name();
  auto moved = std::move(second).take_name();
  moved[0U] = 'S';
  assert(copy == "song_20260830_142500.ogg");
}

void test_errors() {
  const AudioExportLocalTime invalid_month{2026U, 13U, 1U, 0U, 0U, 0U};
  const AudioExportLocalTime invalid_day{2025U, 2U, 29U, 0U, 0U, 0U};
  const AudioExportLocalTime leap_day{2024U, 2U, 29U, 23U, 59U, 59U};
  assert(otgy::core::make_audio_export_file_name("x", invalid_month).error() ==
         AudioExportNameError::InvalidLocalTime);
  assert(otgy::core::make_audio_export_file_name("x", invalid_day).error() ==
         AudioExportNameError::InvalidLocalTime);
  assert(otgy::core::make_audio_export_file_name("x", leap_day));
  assert(otgy::core::make_audio_export_file_name("x", leap_day, 0U).error() ==
         AudioExportNameError::InvalidCollisionIndex);

  for (unsigned int raw = 0U;
       raw <= static_cast<unsigned int>(AudioExportNameError::AllocationFailure);
       ++raw) {
    const auto message = otgy::core::audio_export_name_error_message(
        static_cast<AudioExportNameError>(raw));
    if (raw == 0U) {
      assert(message.empty());
    } else {
      assert(!message.empty());
    }
  }
  assert(!otgy::core::audio_export_name_error_message(
              static_cast<AudioExportNameError>(255))
              .empty());
}

}  // namespace

int main() {
  test_normalization();
  test_default_base_name();
  test_timestamp_and_collisions();
  test_errors();
  return 0;
}
