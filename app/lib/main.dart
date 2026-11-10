import 'dart:ui';

import 'package:flutter/material.dart';

import 'src/application/editor_controller.dart';
import 'src/infrastructure/local_diagnostics.dart';
import 'src/infrastructure/local_ui_preferences.dart';
import 'src/ui/editor_screen.dart';
import 'src/ui/editor_theme.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await initializeAppDiagnostics();
  final preferences = await LocalUiPreferences.load();
  runApp(OneTwoGoYeahApp(preferences: preferences));
}

Future<LocalDiagnostics?> initializeAppDiagnostics() async {
  final diagnostics = await LocalDiagnostics.initialize();
  if (diagnostics != null) {
    installDiagnosticErrorHandlers(diagnostics);
    diagnostics.record('startup', LocalDiagnostics.buildIdentity);
  }
  return diagnostics;
}

void installDiagnosticErrorHandlers(LocalDiagnostics logger) {
  final previousFlutterError = FlutterError.onError;
  FlutterError.onError = (details) {
    logger.recordException(
      'flutter',
      details.exception,
      details.stack ?? StackTrace.empty,
    );
    if (previousFlutterError != null) {
      previousFlutterError(details);
    } else {
      FlutterError.presentError(details);
    }
  };
  final previousPlatformError = PlatformDispatcher.instance.onError;
  PlatformDispatcher.instance.onError = (error, stack) {
    logger.recordException('platform', error, stack);
    return previousPlatformError?.call(error, stack) ?? false;
  };
}

class OneTwoGoYeahApp extends StatelessWidget {
  const OneTwoGoYeahApp({
    super.key,
    this.controller,
    this.storeFactory,
    this.preferences,
  });

  final EditorController? controller;
  final ProjectStoreFactory? storeFactory;
  final LocalUiPreferences? preferences;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'OneTwoGoYeah',
      debugShowCheckedModeBanner: false,
      theme: buildEditorTheme(),
      home: EditorScreen(
        controller: controller,
        storeFactory: storeFactory,
        preferences: preferences,
      ),
    );
  }
}
