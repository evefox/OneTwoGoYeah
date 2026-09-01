import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

void main(List<String> args) async {
  await link(args, (input, output) async {
    final cppLinkStdLib = input.config.code.targetOS == OS.android
        ? 'c++_static'
        : null;
    await CLinker.library(
      name: 'otgy_native',
      assetName: 'otgy_native_bindings_generated.dart',
      // Android keeps libm separate from libc; retain it when using the
      // statically linked libc++ runtime.
      libraries: input.config.code.targetOS == OS.android
          ? const ['m']
          : const [],
      language: Language.cpp,
      std: 'c++17',
      cppLinkStdLib: cppLinkStdLib,
      // The link hook receives static archives as its source files. Reset
      // Clang's language mode so -x c++ does not parse those archives as text.
      flags: input.config.code.targetOS == OS.windows
          ? const []
          : const ['-x', 'none'],
      linkerOptions: LinkerOptions.manual(
        gcSections: false,
        stripDebug: false,
        symbolsToKeep: null,
      ),
    ).run(
      input: input,
      output: output,
      sources: input.assets.code
          .map((asset) => asset.file!.toFilePath())
          .toList(growable: false),
    );
  });
}
