import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:one_two_go_yeah/src/application/editor_controller.dart';
import 'package:one_two_go_yeah/src/ui/screen_keypad_overlay.dart';
import 'package:otgy_native/otgy_native.dart';

void main() {
  test('simulation key mapping follows the selected dance mode', () {
    final controller = EditorController();
    addTearDown(controller.dispose);

    expect(controller.playbackDanceMode, PlaybackDanceMode.fourKey);
    expect(
      controller.simulationSoundForKey(LogicalKeyboardKey.numpad2),
      PlaybackSoundId.f1_2,
    );
    expect(
      controller.simulationSoundForKey(LogicalKeyboardKey.numpad1),
      isNull,
    );
    expect(
      controller.simulationSoundForKey(LogicalKeyboardKey.numpad3),
      isNull,
    );
    expect(
      controller.simulationSoundForKey(LogicalKeyboardKey.numpad5),
      PlaybackSoundId.f1Yeah,
    );
    expect(
      controller.simulationSoundForKey(LogicalKeyboardKey.numpad0),
      PlaybackSoundId.f1Yeah,
    );
    expect(
      controller.simulationSoundForKey(LogicalKeyboardKey.space),
      PlaybackSoundId.f1Go,
    );
    expect(
      controller.simulationSoundForKey(LogicalKeyboardKey.arrowUp),
      PlaybackSoundId.f1_8,
    );
    expect(
      controller.simulationSoundForKey(LogicalKeyboardKey.arrowLeft),
      PlaybackSoundId.f1_4,
    );

    controller.setPlaybackDanceMode(PlaybackDanceMode.eightKey);
    expect(
      controller.simulationSoundForKey(LogicalKeyboardKey.numpad1),
      PlaybackSoundId.f1_1,
    );
    expect(
      controller.simulationSoundForKey(LogicalKeyboardKey.numpad3),
      PlaybackSoundId.f1_3,
    );
    expect(
      controller.simulationSoundForKey(LogicalKeyboardKey.numpad9),
      PlaybackSoundId.f1_9,
    );

    controller.setPlaybackDanceMode(PlaybackDanceMode.fourKey);
    expect(
      controller.simulationSoundForKey(LogicalKeyboardKey.numpad1),
      isNull,
    );
  });

  test('simulation settings keep the arrows layout out of 8K', () {
    final controller = EditorController();
    addTearDown(controller.dispose);

    controller.setSimulationEnabled(true);
    expect(controller.simulationEnabled, isTrue);
    controller.setScreenKeypadLayout(ScreenKeypadLayout.arrows);
    expect(controller.screenKeypadLayout, ScreenKeypadLayout.arrows);

    controller.openScreenKeypad();
    expect(controller.screenKeypadOpen, isTrue);

    controller.setScreenKeypadLayout(ScreenKeypadLayout.arrows);
    controller.setPlaybackDanceMode(PlaybackDanceMode.eightKey);
    expect(controller.screenKeypadLayout, ScreenKeypadLayout.numeric);

    controller.setScreenKeypadLayout(ScreenKeypadLayout.arrows);
    expect(controller.screenKeypadLayout, ScreenKeypadLayout.numeric);

    controller.setSimulationEnabled(false);
    expect(controller.simulationEnabled, isFalse);
    expect(controller.screenKeypadOpen, isFalse);
  });

  testWidgets('numeric overlay renders fixed keypad and judgement colors', (
    tester,
  ) async {
    final controller = EditorController();
    addTearDown(controller.dispose);
    controller.setSimulationEnabled(true);
    controller.openScreenKeypad();

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: ScreenKeypadOverlay(controller: controller)),
      ),
    );

    expect(find.text('7'), findsOneWidget);
    expect(find.text('8'), findsOneWidget);
    expect(find.text('9'), findsOneWidget);
    expect(find.text('0'), findsOneWidget);
    expect(find.text('5'), findsOneWidget);
    expect(find.byIcon(Icons.play_arrow_rounded), findsOneWidget);
    expect(find.byIcon(Icons.close), findsOneWidget);
  });

  testWidgets('numeric overlay uses real-keyboard sized keys', (tester) async {
    await tester.binding.setSurfaceSize(const Size(390, 844));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    final controller = EditorController();
    addTearDown(controller.dispose);
    controller.setSimulationEnabled(true);
    controller.openScreenKeypad();

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: ScreenKeypadOverlay(controller: controller)),
      ),
    );

    // 18 mm keycap with a 1 mm gap: about 108 dp at normal phone density.
    expect(
      tester.getSize(find.byKey(const ValueKey('screen-keypad-key-7'))),
      const Size(108, 108),
    );
    expect(
      tester.getSize(find.byKey(const ValueKey('screen-keypad-key-0'))),
      const Size(338, 108),
    );
  });

  testWidgets('screen keys fire on pointer down and honor the 4K grayout', (
    tester,
  ) async {
    final controller = EditorController();
    addTearDown(controller.dispose);
    controller.setSimulationEnabled(true);
    controller.openScreenKeypad();
    final triggered = <PlaybackSoundId>[];

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: ScreenKeypadOverlay(
            controller: controller,
            onTrigger: triggered.add,
          ),
        ),
      ),
    );

    final gesture = await tester.startGesture(
      tester.getCenter(find.byKey(const ValueKey('screen-keypad-key-2'))),
    );
    await tester.pump();
    expect(triggered, [PlaybackSoundId.f1_2]);
    await gesture.up();

    await tester.tap(find.byKey(const ValueKey('screen-keypad-key-1')));
    await tester.pump();
    expect(triggered, [PlaybackSoundId.f1_2]);
  });

  testWidgets('arrow overlay renders Ctrl, arrows and 0', (tester) async {
    await tester.binding.setSurfaceSize(const Size(390, 844));
    addTearDown(() => tester.binding.setSurfaceSize(null));
    final controller = EditorController();
    addTearDown(controller.dispose);
    controller.setSimulationEnabled(true);
    controller.setScreenKeypadLayout(ScreenKeypadLayout.arrows);
    controller.openScreenKeypad();

    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: ScreenKeypadOverlay(controller: controller)),
      ),
    );

    expect(find.text('Ctrl'), findsOneWidget);
    expect(find.text('↑'), findsOneWidget);
    expect(find.text('↓'), findsOneWidget);
    expect(find.text('←'), findsOneWidget);
    expect(find.text('→'), findsOneWidget);
    expect(find.text('0'), findsOneWidget);
    expect(
      tester.getSize(find.byKey(const ValueKey('screen-keypad-key-↑'))),
      const Size(108, 108),
    );
  });
}
