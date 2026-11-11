// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "av_core/chart/chart_draft.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>
#include <vector>

namespace otgy::core {

inline constexpr std::uint16_t kProjectPayloadVersion = 2U;

// Version 1 formal drafts carried BPM and Offset before the draft was limited
// to chart data. They remain readable; the timing prefix is validated and
// discarded.
inline constexpr std::uint16_t kProjectPayloadVersionLegacyTiming = 1U;

enum class ProjectPayloadKind : std::uint8_t {
  FormalDraft = 1,
  RecoverySnapshot = 2,
};

enum class ProjectSerializationError : std::uint8_t {
  None = 0,
  NullInput = 1,
  EmptyInput = 2,
  InvalidMagic = 3,
  UnsupportedVersion = 4,
  UnexpectedPayloadKind = 5,
  InvalidHeader = 6,
  LengthMismatch = 7,
  IntegrityMismatch = 8,
  MalformedPayload = 9,
  InvalidDraft = 10,
  InvalidTiming = 11,
  AllocationFailure = 12,
};

[[nodiscard]] constexpr std::string_view project_serialization_error_message(
    ProjectSerializationError error) noexcept {
  switch (error) {
    case ProjectSerializationError::None:
      return {};
    case ProjectSerializationError::NullInput:
      return "project payload pointer is null";
    case ProjectSerializationError::EmptyInput:
      return "project payload is empty";
    case ProjectSerializationError::InvalidMagic:
      return "project payload magic is invalid";
    case ProjectSerializationError::UnsupportedVersion:
      return "project payload version is unsupported";
    case ProjectSerializationError::UnexpectedPayloadKind:
      return "project payload kind does not match the requested operation";
    case ProjectSerializationError::InvalidHeader:
      return "project payload header is invalid";
    case ProjectSerializationError::LengthMismatch:
      return "project payload length does not match its header";
    case ProjectSerializationError::IntegrityMismatch:
      return "project payload integrity check failed";
    case ProjectSerializationError::MalformedPayload:
      return "project payload data is malformed";
    case ProjectSerializationError::InvalidDraft:
      return "chart draft contains an invalid enum value";
    case ProjectSerializationError::InvalidTiming:
      return "formal draft timing values are invalid";
    case ProjectSerializationError::AllocationFailure:
      return "unable to allocate project payload state";
  }
  return "unknown project serialization error";
}

class ProjectBytesResult final {
 public:
  explicit ProjectBytesResult(std::vector<std::uint8_t> bytes) noexcept;
  explicit ProjectBytesResult(ProjectSerializationError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const std::vector<std::uint8_t>* bytes() const noexcept;
  [[nodiscard]] ProjectSerializationError error() const noexcept;
  [[nodiscard]] std::vector<std::uint8_t> take_bytes() &&;

 private:
  std::variant<std::vector<std::uint8_t>, ProjectSerializationError> storage_;
};

struct FormalDraftPayload final {
  ChartDraft chart;
};

class FormalDraftDecodeResult final {
 public:
  explicit FormalDraftDecodeResult(FormalDraftPayload payload) noexcept;
  explicit FormalDraftDecodeResult(ProjectSerializationError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const FormalDraftPayload* payload() const noexcept;
  [[nodiscard]] ProjectSerializationError error() const noexcept;
  [[nodiscard]] FormalDraftPayload take_payload() &&;

 private:
  std::variant<FormalDraftPayload, ProjectSerializationError> storage_;
};

class RecoverySnapshotDecodeResult final {
 public:
  explicit RecoverySnapshotDecodeResult(ChartDraft chart) noexcept;
  explicit RecoverySnapshotDecodeResult(
      ProjectSerializationError error) noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;
  [[nodiscard]] const ChartDraft* chart() const noexcept;
  [[nodiscard]] ProjectSerializationError error() const noexcept;
  [[nodiscard]] ChartDraft take_chart() &&;

 private:
  std::variant<ChartDraft, ProjectSerializationError> storage_;
};

[[nodiscard]] ProjectBytesResult encode_formal_draft(
    const ChartDraft& chart) noexcept;
[[nodiscard]] FormalDraftDecodeResult decode_formal_draft(
    const std::uint8_t* bytes, std::size_t byte_count) noexcept;

[[nodiscard]] ProjectBytesResult encode_recovery_snapshot(
    const ChartDraft& chart) noexcept;
[[nodiscard]] RecoverySnapshotDecodeResult decode_recovery_snapshot(
    const std::uint8_t* bytes, std::size_t byte_count) noexcept;

}  // namespace otgy::core
