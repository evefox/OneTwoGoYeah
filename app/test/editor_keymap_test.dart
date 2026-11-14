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
