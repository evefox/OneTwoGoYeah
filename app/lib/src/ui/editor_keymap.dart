import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

/// A keyboard action the user can rebind from the usage guide on desktop.
///
/// The keymap is a Flutter-only input preference. It never reaches `av_core`,
/// the C ABI, a `ChartDraft`, or SLK output, and a rebound key still calls the
/// exact controller command the default key called.
enum EditorShortcutAction {
  playPause('playPause', '播放 / 暂停'),
  undo('undo', '撤销'),
  redo('redo', '重做'),
  zoomIn('zoomIn', 'Zoom 增大'),
  zoomOut('zoomOut', 'Zoom 减小'),
  zoomReset('zoomReset', 'Zoom 重置');

  const EditorShortcutAction(this.id, this.label);

  final String id;
  final String label;

  static EditorShortcutAction? byId(String id) {
    for (final action in values) {
      if (action.id == id) {
        return action;
      }
    }
    return null;
  }
}

/// One key combination. Persisted by logical key id so it survives layout and
/// Locale changes; modifiers are stored separately and rendered per platform.
@immutable
final class ShortcutBinding {
  const ShortcutBinding({
    required this.keyId,
    this.control = false,
    this.meta = false,
    this.shift = false,
    this.alt = false,
  });

  final int keyId;
  final bool control;
  final bool meta;
  final bool shift;
  final bool alt;

  LogicalKeyboardKey? get key => LogicalKeyboardKey.findKeyByKeyId(keyId);

  SingleActivator? toActivator() {
    final logicalKey = key;
    if (logicalKey == null) {
      return null;
    }
    return SingleActivator(
      logicalKey,
      control: control,
      shift: shift,
      alt: alt,
      meta: meta,
    );
  }

  bool hasSameChord(ShortcutBinding other) =>
      keyId == other.keyId &&
      control == other.control &&
      meta == other.meta &&
      shift == other.shift &&
      alt == other.alt;

  Map<String, Object?> toJson() => {
    'key': keyId,
    if (control) 'ctrl': true,
    if (meta) 'cmd': true,
    if (shift) 'shift': true,
    if (alt) 'alt': true,
  };

  static ShortcutBinding? fromJson(Object? json) {
    if (json is! Map) {
      return null;
    }
    final keyId = json['key'];
    if (keyId is! int || LogicalKeyboardKey.findKeyByKeyId(keyId) == null) {
      return null;
    }
    bool flag(String name) => json[name] == true;
    return ShortcutBinding(
      keyId: keyId,
      control: flag('ctrl'),
      meta: flag('cmd'),
      shift: flag('shift'),
      alt: flag('alt'),
    );
  }

  @override
  bool operator ==(Object other) =>
      other is ShortcutBinding && hasSameChord(other);

  @override
  int get hashCode => Object.hash(keyId, control, meta, shift, alt);

  @override
  String toString() =>
      'ShortcutBinding($keyId, ctrl: $control, cmd: $meta, shift: $shift, alt: $alt)';
}

/// The complete desktop keymap: one ordered list of combinations per action.
///
/// The first combination is the one the usage guide shows first; every entry in
/// the list is live. An empty list is a legitimate "unbound" state, which is
/// how ArrowVortex treats actions without a default key such as Zoom reset.
@immutable
final class EditorKeymap {
  const EditorKeymap._(this._bindings);

  static const int storageVersion = 1;

  final Map<EditorShortcutAction, List<ShortcutBinding>> _bindings;

  factory EditorKeymap.defaults(TargetPlatform platform) =>
      EditorKeymap._(defaultEditorShortcutBindings(platform));

  List<ShortcutBinding> bindingsFor(EditorShortcutAction action) =>
      _bindings[action] ?? const <ShortcutBinding>[];

  EditorKeymap withBindings(
    EditorShortcutAction action,
    List<ShortcutBinding> bindings,
  ) {
    final next = Map<EditorShortcutAction, List<ShortcutBinding>>.of(_bindings);
    next[action] = List<ShortcutBinding>.unmodifiable(bindings);
    return EditorKeymap._(Map.unmodifiable(next));
  }

  bool isDefaultFor(TargetPlatform platform) {
    final defaults = defaultEditorShortcutBindings(platform);
    for (final action in EditorShortcutAction.values) {
      final current = bindingsFor(action);
      final reference = defaults[action] ?? const <ShortcutBinding>[];
      if (current.length != reference.length) {
        return false;
      }
      for (var index = 0; index < current.length; index++) {
        if (!current[index].hasSameChord(reference[index])) {
          return false;
        }
      }
    }
    return true;
  }

  /// Returns the other action that already owns [binding], if any.
  EditorShortcutAction? conflictingAction(
    EditorShortcutAction action,
    ShortcutBinding binding,
  ) {
    for (final other in EditorShortcutAction.values) {
      if (other == action) {
        continue;
      }
      for (final candidate in bindingsFor(other)) {
        if (candidate.hasSameChord(binding)) {
          return other;
        }
      }
    }
    return null;
  }

  Map<String, Object?> toJson() => {
    'version': storageVersion,
    'bindings': {
      for (final action in EditorShortcutAction.values)
        action.id: [
          for (final binding in bindingsFor(action)) binding.toJson(),
        ],
    },
  };

  /// Restores every action from [platform] defaults when the stored value is
  /// missing, not a map, or contains an entry that cannot be decoded. An
  /// explicitly empty list is preserved because "unbound" is a real state.
  static EditorKeymap fromJson(Object? json, TargetPlatform platform) {
    final defaults = defaultEditorShortcutBindings(platform);
    if (json is! Map) {
      return EditorKeymap._(defaults);
    }
    final rawBindings = json['bindings'];
    if (rawBindings is! Map) {
      return EditorKeymap._(defaults);
    }
    final bindings = Map<EditorShortcutAction, List<ShortcutBinding>>.of(
      defaults,
    );
    for (final entry in rawBindings.entries) {
      final action = EditorShortcutAction.byId(entry.key.toString());
      if (action == null) {
        continue;
      }
      final rawList = entry.value;
      if (rawList is! List) {
        continue;
      }
      final parsed = <ShortcutBinding>[];
      var valid = true;
      for (final item in rawList) {
        final binding = ShortcutBinding.fromJson(item);
        if (binding == null) {
          valid = false;
          break;
        }
        parsed.add(binding);
      }
      if (!valid) {
        continue;
      }
      bindings[action] = List<ShortcutBinding>.unmodifiable(parsed);
    }
    return EditorKeymap._(Map.unmodifiable(bindings));
  }
}

ShortcutBinding _bind(
  LogicalKeyboardKey key, {
  bool control = false,
  bool meta = false,
  bool shift = false,
  bool alt = false,
}) => ShortcutBinding(
  keyId: key.keyId,
  control: control,
  meta: meta,
  shift: shift,
  alt: alt,
);

/// Defaults are ArrowVortex's own bindings where they exist, because desktop
/// users arrive with those habits:
///
/// * `settings/shortcuts.txt`: `ZOOM_IN = numpad add`, `ZOOM_OUT = numpad
///   subtract`, `ZOOM_RESET` empty, `ctrl + scroll` zoom and `ctrl + alt +
///   scroll` scale (already handled by the canvas scroll path).
/// * `Menubar.cpp`: Undo `Ctrl+Z`, Redo `Ctrl+Y`.
/// * `View.cpp`: play/pause on `Space`.
///
/// macOS keeps its existing Command-based defaults; ArrowVortex is Windows-only
/// and has no macOS reference. Windows has no Zoom reset default because
/// ArrowVortex leaves `ZOOM_RESET` unbound.
Map<EditorShortcutAction, List<ShortcutBinding>> defaultEditorShortcutBindings(
  TargetPlatform platform,
) {
  final List<ShortcutBinding> undo;
  final List<ShortcutBinding> redo;
  final List<ShortcutBinding> zoomIn;
  final List<ShortcutBinding> zoomOut;
  final List<ShortcutBinding> zoomReset;
  if (platform == TargetPlatform.macOS) {
    undo = [
      _bind(LogicalKeyboardKey.keyZ, meta: true),
      _bind(LogicalKeyboardKey.keyZ, control: true),
    ];
    redo = [
      _bind(LogicalKeyboardKey.keyZ, meta: true, shift: true),
      _bind(LogicalKeyboardKey.keyZ, control: true, shift: true),
    ];
    zoomIn = [
      _bind(LogicalKeyboardKey.equal, meta: true),
      _bind(LogicalKeyboardKey.equal, meta: true, shift: true),
      _bind(LogicalKeyboardKey.add, meta: true),
      _bind(LogicalKeyboardKey.numpadAdd, meta: true),
    ];
    zoomOut = [
      _bind(LogicalKeyboardKey.minus, meta: true),
      _bind(LogicalKeyboardKey.numpadSubtract, meta: true),
    ];
    zoomReset = [_bind(LogicalKeyboardKey.digit0, meta: true)];
  } else {
    undo = [_bind(LogicalKeyboardKey.keyZ, control: true)];
    redo = [
      _bind(LogicalKeyboardKey.keyY, control: true),
      _bind(LogicalKeyboardKey.keyZ, control: true, shift: true),
    ];
    zoomIn = [_bind(LogicalKeyboardKey.numpadAdd)];
    zoomOut = [_bind(LogicalKeyboardKey.numpadSubtract)];
    zoomReset = const <ShortcutBinding>[];
  }
  return Map<EditorShortcutAction, List<ShortcutBinding>>.unmodifiable({
    EditorShortcutAction.playPause: List<ShortcutBinding>.unmodifiable([
      _bind(LogicalKeyboardKey.space),
    ]),
    EditorShortcutAction.undo: List<ShortcutBinding>.unmodifiable(undo),
    EditorShortcutAction.redo: List<ShortcutBinding>.unmodifiable(redo),
    EditorShortcutAction.zoomIn: List<ShortcutBinding>.unmodifiable(zoomIn),
    EditorShortcutAction.zoomOut: List<ShortcutBinding>.unmodifiable(zoomOut),
    EditorShortcutAction.zoomReset: List<ShortcutBinding>.unmodifiable(
      zoomReset,
    ),
  });
}

/// Combinations owned by fixed editor behavior. They are never assignable, so
/// a rebind cannot silently break text input, marker activation or the
/// selection-session escape hatch.
bool isReservedShortcut(ShortcutBinding binding) {
  final key = binding.key;
  if (key == null) {
    return true;
  }
  if (binding.alt) {
    return false;
  }
  if (key == LogicalKeyboardKey.escape || key == LogicalKeyboardKey.enter) {
    return true;
  }
  if (binding.control || binding.meta) {
    if (!binding.shift &&
        (key == LogicalKeyboardKey.keyC || key == LogicalKeyboardKey.keyX)) {
      return true;
    }
  }
  return false;
}

/// Modifier-only presses cannot become a binding; they are used while capturing.
bool isModifierOnlyKey(LogicalKeyboardKey key) =>
    key == LogicalKeyboardKey.shiftLeft ||
    key == LogicalKeyboardKey.shiftRight ||
    key == LogicalKeyboardKey.controlLeft ||
    key == LogicalKeyboardKey.controlRight ||
    key == LogicalKeyboardKey.altLeft ||
    key == LogicalKeyboardKey.altRight ||
    key == LogicalKeyboardKey.metaLeft ||
    key == LogicalKeyboardKey.metaRight ||
    key == LogicalKeyboardKey.capsLock ||
    key == LogicalKeyboardKey.numLock ||
    key == LogicalKeyboardKey.scrollLock ||
    key == LogicalKeyboardKey.fn;

/// Short, readable key names. Falls back to the layout label so unusual keys
/// still render; an empty label means the key has no printable representation.
String shortcutKeyLabel(LogicalKeyboardKey key) {
  final name = _shortcutKeyNames[key];
  if (name != null) {
    return name;
  }
  final label = key.keyLabel;
  if (label.isNotEmpty) {
    return label.toUpperCase();
  }
  final debugName = key.debugName;
  return debugName == null || debugName.isEmpty ? '按键' : debugName;
}

final Map<LogicalKeyboardKey, String> _shortcutKeyNames = {
  LogicalKeyboardKey.space: '空格',
  LogicalKeyboardKey.escape: 'Esc',
  LogicalKeyboardKey.enter: 'Enter',
  LogicalKeyboardKey.tab: 'Tab',
  LogicalKeyboardKey.backspace: 'Backspace',
  LogicalKeyboardKey.delete: 'Delete',
  LogicalKeyboardKey.home: 'Home',
  LogicalKeyboardKey.end: 'End',
  LogicalKeyboardKey.pageUp: 'Page Up',
  LogicalKeyboardKey.pageDown: 'Page Down',
  LogicalKeyboardKey.arrowUp: '↑',
  LogicalKeyboardKey.arrowDown: '↓',
  LogicalKeyboardKey.arrowLeft: '←',
  LogicalKeyboardKey.arrowRight: '→',
  LogicalKeyboardKey.equal: '=',
  LogicalKeyboardKey.minus: '-',
  LogicalKeyboardKey.add: '+',
  LogicalKeyboardKey.numpadAdd: '小键盘 +',
  LogicalKeyboardKey.numpadSubtract: '小键盘 -',
  LogicalKeyboardKey.numpadMultiply: '小键盘 *',
  LogicalKeyboardKey.numpadDivide: '小键盘 /',
  LogicalKeyboardKey.numpadEqual: '小键盘 =',
};

/// Renders one chord with the platform's modifier names.
String formatShortcutBinding(ShortcutBinding binding, TargetPlatform platform) {
  final key = binding.key;
  if (key == null) {
    return '未绑定';
  }
  final label = shortcutKeyLabel(key);
  final mac = platform == TargetPlatform.macOS;
  final parts = <String>[];
  if (mac) {
    if (binding.control) parts.add('⌃');
    if (binding.alt) parts.add('⌥');
    if (binding.shift) parts.add('⇧');
    if (binding.meta) parts.add('⌘');
    return '${parts.join()}$label';
  }
  if (binding.control) parts.add('Ctrl');
  if (binding.alt) parts.add('Alt');
  if (binding.shift) parts.add('Shift');
  if (binding.meta) parts.add('Win');
  return [...parts, label].join(' + ');
}

String formatShortcutBindings(
  List<ShortcutBinding> bindings,
  TargetPlatform platform,
) {
  if (bindings.isEmpty) {
    return '未绑定';
  }
  return bindings
      .map((binding) => formatShortcutBinding(binding, platform))
      .join(' 或 ');
}

/// Human label for the primary modifier of [platform].
String primaryModifierLabel(TargetPlatform platform) =>
    platform == TargetPlatform.macOS ? 'Command' : 'Ctrl';
