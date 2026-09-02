import 'package:flutter/material.dart';

abstract final class EditorColors {
  static const canvas = Color(0xFF10151A);
  static const surface = Color(0xFF171D23);
  static const surfaceRaised = Color(0xFF202831);
  static const border = Color(0xFF34404A);
  static const text = Color(0xFFDCE5EC);
  static const textMuted = Color(0xFF92A1AD);
  static const accent = Color(0xFF65B8C5);
  static const waveform = Color(0xFF5EABB8);
  static const judgement = Color(0xFFE6B75A);
  static const markerNpc = Color(0xFF3D8FC6);
  static const markerPlayer = Color(0xFF3BAA73);
  static const markerS = Color(0xFFD96C75);
  static const markerD = Color(0xFFA989C7);
  static const markerText = Color(0xFFF8FBFF);
  static const danger = Color(0xFFE07B84);
}

ThemeData buildEditorTheme() {
  final scheme = ColorScheme.fromSeed(
    seedColor: EditorColors.accent,
    brightness: Brightness.dark,
    surface: EditorColors.surface,
    error: EditorColors.danger,
  );
  return ThemeData(
    brightness: Brightness.dark,
    colorScheme: scheme,
    scaffoldBackgroundColor: EditorColors.canvas,
    canvasColor: EditorColors.surface,
    dividerColor: EditorColors.border,
    focusColor: EditorColors.accent.withValues(alpha: 0.26),
    hoverColor: EditorColors.text.withValues(alpha: 0.06),
    splashFactory: NoSplash.splashFactory,
    visualDensity: VisualDensity.compact,
    textTheme: const TextTheme(
      bodyMedium: TextStyle(color: EditorColors.text, fontSize: 13),
      bodySmall: TextStyle(color: EditorColors.textMuted, fontSize: 12),
      labelLarge: TextStyle(
        color: EditorColors.text,
        fontSize: 13,
        fontWeight: FontWeight.w600,
      ),
    ),
    tooltipTheme: const TooltipThemeData(
      waitDuration: Duration(milliseconds: 450),
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: EditorColors.canvas,
      isDense: true,
      contentPadding: const EdgeInsets.symmetric(horizontal: 10, vertical: 9),
      enabledBorder: const OutlineInputBorder(
        borderSide: BorderSide(color: EditorColors.border),
        borderRadius: BorderRadius.all(Radius.circular(6)),
      ),
      focusedBorder: const OutlineInputBorder(
        borderSide: BorderSide(color: EditorColors.accent, width: 1.5),
        borderRadius: BorderRadius.all(Radius.circular(6)),
      ),
    ),
    iconButtonTheme: IconButtonThemeData(
      style: ButtonStyle(
        minimumSize: WidgetStateProperty.all(const Size(40, 40)),
        foregroundColor: WidgetStateProperty.resolveWith(
          (states) => states.contains(WidgetState.disabled)
              ? EditorColors.textMuted.withValues(alpha: 0.42)
              : EditorColors.text,
        ),
        shape: WidgetStateProperty.all(
          RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
        ),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        foregroundColor: EditorColors.text,
        side: const BorderSide(color: EditorColors.border),
        minimumSize: const Size(40, 38),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
      ),
    ),
  );
}
