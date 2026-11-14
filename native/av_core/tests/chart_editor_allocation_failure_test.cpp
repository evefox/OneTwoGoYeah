// SPDX-License-Identifier: GPL-3.0-or-later

#include "av_core/chart/chart_editor.hpp"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace allocation_fault {
bool armed = false;
bool injected = false;
std::size_t attempts = 0U;
std::size_t fail_at = std::numeric_limits<std::size_t>::max();

void check(std::size_t size) {
  bool faultable = true;
#if defined(_MSC_VER) && defined(_DEBUG)
  // The debug STL allocates a container proxy (two pointers) inside noexcept
  // vector constructors; a failure there terminates the process before any
  // handler can run, so those allocations are not faultable.
  if (size == sizeof(void*) * 2U) faultable = false;
#endif
  if (armed && faultable && attempts++ == fail_at) {
    injected = true;
    throw std::bad_alloc{};
  }
}

void arm(std::size_t index) noexcept {
  attempts = 0U;
  injected = false;
  fail_at = index;
  armed = true;
}

void* allocate(std::size_t size) {
  check(size);
  if (void* memory = std::malloc(size == 0U ? 1U : size)) return memory;
  throw std::bad_alloc{};
}

void* allocate_aligned(std::size_t size, std::size_t alignment) {
  check(size);
#if defined(_MSC_VER)
  if (void* memory = _aligned_malloc(size == 0U ? 1U : size, alignment)) return memory;
#else
  void* memory = nullptr;
  if (posix_memalign(&memory, alignment, size == 0U ? 1U : size) == 0) return memory;
#endif
  throw std::bad_alloc{};
}

void free_aligned(void* memory) noexcept {
#if defined(_MSC_VER)
  _aligned_free(memory);
#else
  std::free(memory);
#endif
}
}  // namespace allocation_fault

// Isolate replacement allocation functions in this executable, never av_core.
void* operator new(std::size_t size) { return allocation_fault::allocate(size); }
void* operator new[](std::size_t size) { return allocation_fault::allocate(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void* operator new(std::size_t size, std::align_val_t alignment) {
  return allocation_fault::allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
  return allocation_fault::allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void operator delete(void* memory, std::align_val_t) noexcept {
  allocation_fault::free_aligned(memory);
}
void operator delete[](void* memory, std::align_val_t) noexcept {
  allocation_fault::free_aligned(memory);
}
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
  allocation_fault::free_aligned(memory);
}
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
  allocation_fault::free_aligned(memory);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  try { return ::operator new(size); } catch (const std::bad_alloc&) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  try { return ::operator new[](size); } catch (const std::bad_alloc&) { return nullptr; }
}
void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }

namespace {
using namespace otgy::core;

[[nodiscard]] ChartEditor prepared_editor() {
  ChartEditor editor{ChartDraft{{}, {}, {}, {}, {}, {}}};
  for (const auto row : {0U, 10U, 14U, 18U}) {
    assert(editor.click(ChartColumn::Npc, row));
  }
  assert(editor.click(ChartColumn::Npc, 18U));
  assert(editor.click(ChartColumn::Player, 24U));
  assert(editor.click(ChartColumn::Player, 30U));
  assert(editor.draft().dance_move_drills().size() == 1U);
  assert(editor.click(ChartColumn::Showtime, 52U));
  assert(editor.draft().showtime_move_events().size() == 1U);
  // Keep nonempty undo AND redo stacks while the source remains unchanged.
  assert(editor.click(ChartColumn::Npc, 160U));
  assert(editor.undo());
  assert(editor.select(ChartColumn::Npc, 0U));
  assert(editor.cut_selection());
  assert(editor.has_pending_cut());
  return editor;
}

void assert_same_state(const ChartEditor& actual, const ChartEditor& expected) {
  assert(chart_drafts_equal(actual.draft(), expected.draft()));
  assert(actual.undo_count() == expected.undo_count());
  assert(actual.redo_count() == expected.redo_count());
  assert(actual.can_undo() == expected.can_undo());
  assert(actual.can_redo() == expected.can_redo());
  assert(actual.has_clipboard() == expected.has_clipboard());
  assert(actual.has_pending_cut() == expected.has_pending_cut());
  assert(actual.clipboard().base_row == expected.clipboard().base_row);
  assert(chart_drafts_equal(actual.clipboard().content, expected.clipboard().content));
  assert(actual.selections().size() == expected.selections().size());
  for (std::size_t i = 0U; i < actual.selections().size(); ++i) {
    const auto& left = actual.selections()[i];
    const auto& right = expected.selections()[i];
    assert(left.active == right.active);
    assert(left.drill_id.value == right.drill_id.value);
    assert(left.first_row == right.first_row && left.last_row == right.last_row);
  }
}

void assert_same_result(const ChartEditResult& actual, const ChartEditResult& expected) {
  assert(actual.code == expected.code && actual.changed == expected.changed);
  assert(actual.primary_event_id.value == expected.primary_event_id.value);
  assert(actual.player_actions_cleared == expected.player_actions_cleared);
}

[[nodiscard]] ChartEditor fixture(bool fresh_history, bool split_source) {
  auto editor = prepared_editor();
  if (split_source) {
    ChartEditor split{ChartDraft{{}, {}, {}, {}, {}, {}}};
    for (const auto row : {0U, 10U, 14U}) assert(split.click(ChartColumn::Npc, row));
    assert(split.click(ChartColumn::Player, 20U));
    assert(split.click(ChartColumn::Player, 30U));
    for (const auto row : {50U, 60U, 64U}) assert(split.click(ChartColumn::Npc, row));
    assert(split.click(ChartColumn::Npc, 64U));
    assert(split.click(ChartColumn::Player, 70U));
    assert(split.click(ChartColumn::Player, 80U));
    assert(split.draft().dance_move_drills().size() == 1U);
    assert(split.draft().drill_groups().size() == 2U);
    assert(split.click(ChartColumn::Npc, 160U));
    assert(split.undo());
    assert(split.select(ChartColumn::Npc, 0U));
    assert(split.cut_selection());
    return split;
  }
  if (!fresh_history) return editor;
  ChartEditor fresh{editor.draft()};
  assert(fresh.select(ChartColumn::Npc, 0U));
  assert(fresh.cut_selection());
  return fresh;
}

void sweep_click_allocations(std::uint64_t row, bool fresh_history = false,
                             bool split_source = false) {
  auto measured = fixture(fresh_history, split_source);
  allocation_fault::arm(std::numeric_limits<std::size_t>::max());
  const auto success = measured.click(ChartColumn::Npc, row);
  allocation_fault::armed = false;
  const auto count = allocation_fault::attempts;
  assert(success && success.changed && count > 0U);

  // Every successful-path allocation is tested, not a handful of guessed
  // offsets. The tail includes commit's history-vector growth/deep copies.
  for (std::size_t index = 0U; index < count; ++index) {
    auto editor = fixture(fresh_history, split_source);
    auto control = fixture(fresh_history, split_source);
    allocation_fault::arm(index);
    const auto failed = editor.click(ChartColumn::Npc, row);
    allocation_fault::armed = false;
    assert(allocation_fault::injected);
    if (failed.code != ChartEditCode::AllocationFailure || failed.changed ||
        failed.player_actions_cleared) {
      std::fprintf(stderr, "row %llu allocation %zu/%zu: code %u changed %d cleared %d\n",
          static_cast<unsigned long long>(row), index, count,
          static_cast<unsigned>(failed.code), failed.changed,
          failed.player_actions_cleared);
      std::abort();
    }
    assert(failed.primary_event_id.value == 0U);
    assert_same_state(editor, control);
    // Compare actual historical contents, not just their counts.
    assert_same_result(editor.redo(), control.redo());
    assert_same_state(editor, control);
    assert_same_result(editor.undo(), control.undo());
    assert_same_state(editor, control);
    assert_same_result(editor.undo(), control.undo());
    assert_same_state(editor, control);
    assert_same_result(editor.redo(), control.redo());
    assert_same_state(editor, control);
    // A real pending cut checks the private saved source and all pasted IDs.
    assert(editor.preview_paste(300U));
    assert(control.preview_paste(300U));
    const auto pasted = editor.paste(300U);
    const auto expected_paste = control.paste(300U);
    assert(pasted && expected_paste);
    assert_same_result(pasted, expected_paste);
    assert_same_state(editor, control);
    // New NPC-C allocates a new group/drill/event after all failed work.
    const auto next = editor.click(ChartColumn::Npc, 500U);
    const auto expected_next = control.click(ChartColumn::Npc, 500U);
    assert(next && expected_next);
    assert_same_result(next, expected_next);
    assert_same_state(editor, control);
  }
  std::printf("NPC row %llu: verified %zu allocation failures\n",
      static_cast<unsigned long long>(row), count);
}
}  // namespace

int main() {
  sweep_click_allocations(8U, true);  // Empty undo vector must grow at commit.
  sweep_click_allocations(8U);    // NPC insertion and successful player growth.
  sweep_click_allocations(10U);   // First N -> S and player-clear fallback.
  sweep_click_allocations(14U, false, true);  // Nonterminal N -> S splits.
  sweep_click_allocations(0U);    // Source and corresponding player deletion.
  sweep_click_allocations(160U);  // New NPC-C allocates event/group/drill IDs.
  return 0;
}
