import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/main.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/infrastructure/local_project_store.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  late Directory directory;
  late LocalProjectStore store;
  late EditorController controller;

  setUp(() async {
    directory = await Directory.systemTemp.createTemp('otgy-judgement-layer-');
    store = await LocalProjectStore.openAt(directory);
    controller = EditorController(editor: NativeChartEditor.createEmpty());
  });

  tearDown(() async {
    controller.dispose();
    if (await directory.exists()) {
      await directory.delete(recursive: true);
    }
  });

  Future<void> openEditor(WidgetTester tester) async {
    await tester.binding.setSurfaceSize(const Size(1280, 800));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    await tester.pumpWidget(
      OneTwoGoYeahApp(controller: controller, storeFactory: () async => store),
    );
    await tester.pumpAndSettle();
  }

  Future<void> runDesktop(
    WidgetTester tester,
    Future<void> Function() body,
  ) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.windows;
    try {
      await openEditor(tester);
      await body();
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  }

  const perfect = PlaybackJudgement(
    grade: PlaybackJudgementGrade.perfect,
    deltaMs: 4,
    noteId: 1,
    expectedSound: PlaybackSoundId.f1_2,
    pressedSound: PlaybackSoundId.f1_2,
    noteSeconds: 1.5,
    pressSeconds: 1.504,
  );

  final layer = find.byKey(const ValueKey('desktop-judgement-layer'));
  final layerText = find.byKey(const ValueKey('desktop-judgement-text'));

  testWidgets('desktop simulation playback shows and clears the judgement', (
    tester,
  ) async {
    await runDesktop(tester, () async {
      expect(layer, findsNothing);

      controller.setPlaybackModeForTest(EditorPlaybackMode.simulation);
      await tester.pump();
      expect(layer, findsOneWidget);
      expect(layerText, findsNothing);

      controller.applyJudgementForTest(perfect);
      await tester.pump();
      expect(layerText, findsOneWidget);
      expect(find.text('PERFECT'), findsOneWidget);

      // The controller owns the 320 ms display window; the layer only renders
      // what it exposes, exactly like the screen keypad.
      await tester.pump(const Duration(milliseconds: 400));
      expect(layerText, findsNothing);
      expect(layer, findsOneWidget);

      controller.setPlaybackModeForTest(EditorPlaybackMode.idle);
      await tester.pump();
      expect(layer, findsNothing);
    });
  });

  testWidgets('chart preview and rhythm test keep the chart undimmed', (
    tester,
  ) async {
    await runDesktop(tester, () async {
      controller.setPlaybackModeForTest(EditorPlaybackMode.chartPreview);
      await tester.pump();
      expect(layer, findsNothing);

      controller.setPlaybackModeForTest(EditorPlaybackMode.rhythmTest);
      await tester.pump();
      expect(layer, findsNothing);
    });
  });

  testWidgets('a disabled judgement switch keeps only the scrim', (
    tester,
  ) async {
    await runDesktop(tester, () async {
      controller.setJudgementEnabled(false);
      controller.setPlaybackModeForTest(EditorPlaybackMode.simulation);
      controller.applyJudgementForTest(perfect);
      await tester.pump();

      expect(layer, findsOneWidget);
      expect(layerText, findsNothing);
      expect(find.text('PERFECT'), findsNothing);

      await tester.pump(const Duration(milliseconds: 400));
    });
  });

  testWidgets('the layer never takes pointer input from the rail', (
    tester,
  ) async {
    await runDesktop(tester, () async {
      controller.setPlaybackModeForTest(EditorPlaybackMode.simulation);
      await tester.pump();
      expect(layer, findsOneWidget);

      // Waveform visibility is the one view toggle playback keeps available.
      // Tapping it through the layer proves the rail is still on top and that
      // the scrim ignores pointers.
      expect(controller.waveformVisible, isTrue);
      await tester.tap(find.byKey(const ValueKey('waveform-toggle-button')));
      await tester.pump();
      expect(controller.waveformVisible, isFalse);
    });
  });

  testWidgets('an open screen keypad replaces the desktop layer', (
    tester,
  ) async {
    await runDesktop(tester, () async {
      controller.setSimulationEnabled(true);
      controller.openScreenKeypad();
      controller.setPlaybackModeForTest(EditorPlaybackMode.simulation);
      controller.applyJudgementForTest(perfect);
      await tester.pump();

      expect(controller.screenKeypadOpen, isTrue);
      expect(layer, findsNothing);
      expect(layerText, findsNothing);
      // The keypad keeps its own single judgement presentation.
      expect(find.text('PERFECT'), findsOneWidget);

      await tester.pump(const Duration(milliseconds: 400));
    });
  });

  testWidgets('mobile keeps its own keypad presentation', (tester) async {
    final previous = debugDefaultTargetPlatformOverride;
    debugDefaultTargetPlatformOverride = TargetPlatform.android;
    try {
      await openEditor(tester);
      controller.setPlaybackModeForTest(EditorPlaybackMode.simulation);
      controller.applyJudgementForTest(perfect);
      await tester.pump();

      expect(layer, findsNothing);
      expect(layerText, findsNothing);

      await tester.pump(const Duration(milliseconds: 400));
    } finally {
      debugDefaultTargetPlatformOverride = previous;
    }
  });
}
