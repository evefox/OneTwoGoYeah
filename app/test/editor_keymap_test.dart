import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/ui/editor_keymap.dart';

List<String> chordsFor(
  EditorKeymap keymap,
  EditorShortcutAction action,
  TargetPlatform platform,
) => keymap
    .bindingsFor(action)
    .map((binding) => formatShortcutBinding(binding, platform))
    .toList();

void main() {
  group('default keymaps', () {
    test('desktop follows ArrowVortex where it has a default', () {
      final keymap = EditorKeymap.defaults(TargetPlatform.windows);

      expect(
        chordsFor(
          keymap,
          EditorShortcutAction.playPause,
          TargetPlatform.windows,
        ),
        ['空格'],
      );
      expect(
        chordsFor(keymap, EditorShortcutAction.undo, TargetPlatform.windows),
        ['Ctrl + Z'],
      );
      expect(
        chordsFor(keymap, EditorShortcutAction.redo, TargetPlatform.windows),
        ['Ctrl + Y', 'Ctrl + Shift + Z'],
      );
      expect(
        chordsFor(keymap, EditorShortcutAction.zoomIn, TargetPlatform.windows),
        ['小键盘 +'],
      );
      expect(
        chordsFor(keymap, EditorShortcutAction.zoomOut, TargetPlatform.windows),
        ['小键盘 -'],
      );
      // ArrowVortex leaves ZOOM_RESET unbound.
      expect(keymap.bindingsFor(EditorShortcutAction.zoomReset), isEmpty);
    });

    test('macOS keeps its Command-based defaults', () {
      final keymap = EditorKeymap.defaults(TargetPlatform.macOS);

      expect(
        chordsFor(keymap, EditorShortcutAction.playPause, TargetPlatform.macOS),
        ['空格'],
      );
      expect(
        chordsFor(keymap, EditorShortcutAction.undo, TargetPlatform.macOS),
        ['⌘Z', '⌃Z'],
      );
      expect(
        chordsFor(keymap, EditorShortcutAction.zoomReset, TargetPlatform.macOS),
        ['⌘0'],
      );
    });

    test('every default is reported as default for its platform', () {
      for (final platform in [
        TargetPlatform.windows,
        TargetPlatform.linux,
        TargetPlatform.macOS,
      ]) {
        expect(EditorKeymap.defaults(platform).isDefaultFor(platform), isTrue);
      }
    });

    test('default bindings never collide across actions', () {
      for (final platform in [
        TargetPlatform.windows,
        TargetPlatform.linux,
        TargetPlatform.macOS,
      ]) {
        final keymap = EditorKeymap.defaults(platform);
        for (final action in EditorShortcutAction.values) {
          for (final binding in keymap.bindingsFor(action)) {
            expect(
              keymap.conflictingAction(action, binding),
              isNull,
              reason:
                  '${action.name} shares '
                  '${formatShortcutBinding(binding, platform)} on $platform',
            );
          }
        }
      }
    });

    test('number keys place the five explicit marker types', () {
      for (final platform in [TargetPlatform.windows, TargetPlatform.macOS]) {
        final keymap = EditorKeymap.defaults(platform);
        expect(chordsFor(keymap, EditorShortcutAction.npcStart, platform), [
          '1',
        ]);
        expect(chordsFor(keymap, EditorShortcutAction.npcNote, platform), [
          '2',
        ]);
        expect(chordsFor(keymap, EditorShortcutAction.npcEnd, platform), ['3']);
        expect(chordsFor(keymap, EditorShortcutAction.showtime, platform), [
          '4',
        ]);
        expect(chordsFor(keymap, EditorShortcutAction.player, platform), ['5']);
      }
    });

    test('placement defaults are distinct from every other action', () {
      final keymap = EditorKeymap.defaults(TargetPlatform.windows);
      for (final action in [
        EditorShortcutAction.npcStart,
        EditorShortcutAction.npcNote,
        EditorShortcutAction.npcEnd,
        EditorShortcutAction.showtime,
        EditorShortcutAction.player,
      ]) {
        expect(
          keymap.conflictingAction(action, keymap.bindingsFor(action).single),
          isNull,
        );
      }
    });

    test('cursor and paste defaults follow ArrowVortex on Windows', () {
      const platform = TargetPlatform.windows;
      final keymap = EditorKeymap.defaults(platform);

      expect(chordsFor(keymap, EditorShortcutAction.cursorUp, platform), ['↑']);
      expect(chordsFor(keymap, EditorShortcutAction.cursorDown, platform), [
        '↓',
      ]);
      expect(
        chordsFor(keymap, EditorShortcutAction.cursorPreviousMeasure, platform),
        ['Page Up'],
      );
      expect(
        chordsFor(keymap, EditorShortcutAction.cursorNextMeasure, platform),
        ['Page Down'],
      );
      expect(
        chordsFor(keymap, EditorShortcutAction.cursorPreviousBeat, platform),
        ['Shift + Page Up'],
      );
      expect(chordsFor(keymap, EditorShortcutAction.cursorNextBeat, platform), [
        'Shift + Page Down',
      ]);
      expect(
        chordsFor(keymap, EditorShortcutAction.cursorChartStart, platform),
        ['Home'],
      );
      expect(chordsFor(keymap, EditorShortcutAction.cursorChartEnd, platform), [
        'End',
      ]);
      expect(
        chordsFor(keymap, EditorShortcutAction.cursorPreviousMarker, platform),
        ['Ctrl + ↑'],
      );
      expect(
        chordsFor(keymap, EditorShortcutAction.cursorNextMarker, platform),
        ['Ctrl + ↓'],
      );
      expect(chordsFor(keymap, EditorShortcutAction.pasteAtCursor, platform), [
        'Ctrl + V',
      ]);
    });

    test('macOS uses Command for marker jumps and paste', () {
      const platform = TargetPlatform.macOS;
      final keymap = EditorKeymap.defaults(platform);

      expect(
        chordsFor(keymap, EditorShortcutAction.cursorPreviousMarker, platform),
        ['⌘↑'],
      );
      expect(
        chordsFor(keymap, EditorShortcutAction.cursorNextMarker, platform),
        ['⌘↓'],
      );
      expect(chordsFor(keymap, EditorShortcutAction.pasteAtCursor, platform), [
        '⌘V',
      ]);
      expect(
        chordsFor(keymap, EditorShortcutAction.cursorPreviousBeat, platform),
        ['⇧Page Up'],
      );
    });

    test('entry and selection defaults follow ArrowVortex', () {
      const windows = TargetPlatform.windows;
      final win = EditorKeymap.defaults(windows);
      expect(chordsFor(win, EditorShortcutAction.selectAll, windows), [
        'Ctrl + A',
      ]);
      expect(
        chordsFor(win, EditorShortcutAction.cursorSelectionStart, windows),
        ['Ctrl + Home'],
      );
      expect(chordsFor(win, EditorShortcutAction.cursorSelectionEnd, windows), [
        'Ctrl + End',
      ]);
      expect(chordsFor(win, EditorShortcutAction.scaleIn, windows), [
        'Ctrl + 小键盘 +',
      ]);
      expect(chordsFor(win, EditorShortcutAction.scaleOut, windows), [
        'Ctrl + 小键盘 -',
      ]);
      expect(chordsFor(win, EditorShortcutAction.openDraft, windows), [
        'Ctrl + O',
      ]);
      expect(chordsFor(win, EditorShortcutAction.saveDraft, windows), [
        'Ctrl + S',
      ]);
      expect(chordsFor(win, EditorShortcutAction.saveDraftAs, windows), [
        'Ctrl + Shift + S',
      ]);
      expect(chordsFor(win, EditorShortcutAction.openGuide, windows), ['F1']);

      const macos = TargetPlatform.macOS;
      final mac = EditorKeymap.defaults(macos);
      expect(chordsFor(mac, EditorShortcutAction.selectAll, macos), ['⌘A']);
      expect(chordsFor(mac, EditorShortcutAction.cursorSelectionStart, macos), [
        '⌘Home',
      ]);
      expect(chordsFor(mac, EditorShortcutAction.saveDraftAs, macos), ['⇧⌘S']);
      expect(chordsFor(mac, EditorShortcutAction.scaleIn, macos), ['⌘小键盘 +']);
      expect(chordsFor(mac, EditorShortcutAction.openGuide, macos), ['F1']);
    });
  });

  group('persistence', () {
    test('round-trips a rebound action and an explicitly unbound action', () {
      final custom = EditorKeymap.defaults(TargetPlatform.windows)
          .withBindings(EditorShortcutAction.zoomIn, [
            ShortcutBinding(keyId: LogicalKeyboardKey.f5.keyId),
          ])
          .withBindings(EditorShortcutAction.zoomOut, const []);

      final restored = EditorKeymap.fromJson(
        custom.toJson(),
        TargetPlatform.windows,
      );

      expect(
        restored.bindingsFor(EditorShortcutAction.zoomIn).single.key,
        LogicalKeyboardKey.f5,
      );
      expect(restored.bindingsFor(EditorShortcutAction.zoomOut), isEmpty);
      expect(restored.isDefaultFor(TargetPlatform.windows), isFalse);
    });

    test('falls back per action when one stored entry is malformed', () {
      final restored = EditorKeymap.fromJson({
        'version': 1,
        'bindings': {
          'undo': [
            {'key': 999999999},
          ],
          'redo': <Object?>[],
        },
      }, TargetPlatform.windows);

      expect(
        chordsFor(restored, EditorShortcutAction.undo, TargetPlatform.windows),
        ['Ctrl + Z'],
      );
      expect(restored.bindingsFor(EditorShortcutAction.redo), isEmpty);
    });

    test('ignores unknown actions and non-map documents', () {
      final unknownAction = EditorKeymap.fromJson({
        'bindings': {
          'teleport': [
            {'key': LogicalKeyboardKey.f7.keyId},
          ],
        },
      }, TargetPlatform.windows);
      expect(unknownAction.isDefaultFor(TargetPlatform.windows), isTrue);

      final garbage = EditorKeymap.fromJson('nope', TargetPlatform.windows);
      expect(garbage.isDefaultFor(TargetPlatform.windows), isTrue);
    });

    test('decodes bindings without modifier keys', () {
      final restored = EditorKeymap.fromJson({
        'bindings': {
          'playPause': [
            {'key': LogicalKeyboardKey.f9.keyId, 'ctrl': true, 'shift': true},
          ],
        },
      }, TargetPlatform.windows);

      final binding = restored
          .bindingsFor(EditorShortcutAction.playPause)
          .single;
      expect(binding.control, isTrue);
      expect(binding.shift, isTrue);
      expect(binding.meta, isFalse);
      expect(binding.alt, isFalse);
    });
  });

  group('conflicts', () {
    test('reports the action that already owns a chord', () {
      final keymap = EditorKeymap.defaults(TargetPlatform.windows);
      final redoChord = keymap.bindingsFor(EditorShortcutAction.redo).first;

      expect(
        keymap.conflictingAction(EditorShortcutAction.undo, redoChord),
        EditorShortcutAction.redo,
      );
    });

    test('the action being edited never conflicts with itself', () {
      final keymap = EditorKeymap.defaults(TargetPlatform.windows);
      final undoChord = keymap.bindingsFor(EditorShortcutAction.undo).single;

      expect(
        keymap.conflictingAction(EditorShortcutAction.undo, undoChord),
        isNull,
      );
    });

    test('reserves Escape, Enter, copy and cut for fixed behavior', () {
      expect(
        isReservedShortcut(
          ShortcutBinding(keyId: LogicalKeyboardKey.escape.keyId),
        ),
        isTrue,
      );
      expect(
        isReservedShortcut(
          ShortcutBinding(keyId: LogicalKeyboardKey.enter.keyId),
        ),
        isTrue,
      );
      expect(
        isReservedShortcut(
          ShortcutBinding(keyId: LogicalKeyboardKey.keyC.keyId, control: true),
        ),
        isTrue,
      );
      expect(
        isReservedShortcut(
          ShortcutBinding(keyId: LogicalKeyboardKey.keyX.keyId, meta: true),
        ),
        isTrue,
      );
      expect(
        isReservedShortcut(
          ShortcutBinding(
            keyId: LogicalKeyboardKey.keyC.keyId,
            control: true,
            shift: true,
          ),
        ),
        isFalse,
      );
      expect(
        isReservedShortcut(
          ShortcutBinding(keyId: LogicalKeyboardKey.space.keyId),
        ),
        isFalse,
      );
    });
  });

  group('formatting', () {
    test('renders platform modifier names and the unbound state', () {
      final chord = ShortcutBinding(
        keyId: LogicalKeyboardKey.keyZ.keyId,
        control: true,
        shift: true,
      );
      expect(
        formatShortcutBinding(chord, TargetPlatform.windows),
        'Ctrl + Shift + Z',
      );
      expect(formatShortcutBindings(const [], TargetPlatform.windows), '未绑定');
    });
  });
}
