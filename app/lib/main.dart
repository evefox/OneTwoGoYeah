import 'package:flutter/material.dart';

import 'src/application/editor_controller.dart';
import 'src/ui/editor_screen.dart';
import 'src/ui/editor_theme.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const OneTwoGoYeahApp());
}

class OneTwoGoYeahApp extends StatelessWidget {
  const OneTwoGoYeahApp({super.key, this.controller, this.storeFactory});

  final EditorController? controller;
  final ProjectStoreFactory? storeFactory;

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'OneTwoGoYeah',
      debugShowCheckedModeBanner: false,
      theme: buildEditorTheme(),
      home: EditorScreen(controller: controller, storeFactory: storeFactory),
    );
  }
}
