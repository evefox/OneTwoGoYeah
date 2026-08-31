import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

void main(List<String> args) async {
  await link(args, (input, output) async {
    await CLinker.library(
      name: 'otgy_native',
      assetName: 'otgy_native_bindings_generated.dart',
      language: Language.cpp,
      std: 'c++17',
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
