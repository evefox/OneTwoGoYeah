# 实现状态

更新时间：2026-08-30

## 当前阶段

阶段 2：ArrowVortex 兼容波形进行中；有符号/整流形状与均匀/振幅亮度四种 1x、四种 2x、四种 3x，以及有符号均匀亮度 4x 的 CPU Alpha 图块及 Zoom/Scale 几何已完成。

## 本次范围

- 增加稳定实施计划和可跨会话读取的状态文件。
- 增加 Git 忽略与 SLK 二进制属性规则。
- 建立可直接调用的 C++ 核心、CLI 和最小测试。
- 定义双声道平面 PCM16 的采样率、帧数、字节尺寸、所有权和稳定错误契约。
- 使用纯内存 C++ 测试覆盖有效值、空 PCM、深拷贝/移动所有权、空指针、尺寸不匹配和溢出边界。
- 固定 ArrowVortex 参考快照中的 libmad `0.15.1 beta`、随附 libogg 快照和 libVorbis `1.3.5` 源码与许可证。
- 建立离线静态目标和版本/链接测试，集中固定 libmad 定点配置并禁止 `fast-math`。
- 在 `av_core` 增加只接收内存字节的同步 MP3 解码入口，libmad 保持私有依赖。
- 按 ArrowVortex 的定点舍入/削波、ID3、Xing、坏帧处理规则输出规范 PCM16。
- 单声道逐样本复制到 Left/Right，双声道保留左右顺序，不重采样；中途格式变化失败。
- 增加稳定 `Mp3DecodeError` 和无部分 PCM 的失败契约，覆盖空、空指针、尺寸、损坏、截断及已解码后失败。
- 增加 CC0-1.0 的小型生成单声道 CBR、双声道 Xing/VBR MP3 及完整平面 PCM16LE 黄金字节，并在配置阶段固定四份 SHA-256。
- 在 `av_core` 增加内存游标 Ogg Vorbis 解码入口，libogg/libVorbis 保持私有依赖。
- 以 libogg 完整物理页/EOS 校验拒绝损坏与截断容器，再使用与 ArrowVortex 相同的 `ov_open_callbacks`/`ov_read` PCM16 路径。
- 单声道复制到 Left/Right，双声道和多声道只保留编码通道 0/1；串联流改变采样率或声道数时失败且不返回部分 PCM。
- 增加稳定 `OggVorbisDecodeError`，覆盖空、空指针、尺寸、非 Ogg、页损坏、截断和串联格式变化。
- 增加 CC0-1.0 的单、双、三声道生成 Ogg 及完整平面 PCM16LE 黄金字节，在配置阶段固定六份 SHA-256；三份 PCM 均与实际编译的 ArrowVortex `LoadOgg.cpp` 逐字节相同。
- 在 `av_core` 增加规范 PCM16LE SHA-256 查询，固定 Left 后 Right、每样本小端字节和小写十六进制契约；空 PCM 与五份黄金 PCM 均直接验证。
- 实现 `avtool decode <audio> --pcm-hash`，CLI 只读取文件、选择现有 MP3/Ogg 权威入口并输出独立于区域设置的 schema v1 JSON。
- 增加直接 C++ 探针与真实 CLI 的运行时集成比较，覆盖五份夹具的格式、采样率、帧数和哈希，以及大小写扩展名、参数、格式、读取和解码错误。
- 从固定 ArrowVortex `Waveform.cpp`/`View.cpp` 参考提取 CPU 波形采样边缘、负幅值整数缩放、有符号/整流形状投影和均匀/振幅亮度算术，核心不依赖全局视图、GPU 或纹理。
- 增加拥有 Left/Right 平面的 `WaveformTile` 和稳定结果/错误类型；支持 UI Zoom `1..19`、Scale `1..10`、128 行 block、空音频和音频末尾透明图块。
- 增加 `generate_rectified_uniform_waveform_tile_1x`，与既有有符号入口共享全部几何、采样边缘、限制、错误和所有权路径，只按 ArrowVortex 规则把每行最大绝对幅值对称投影到声道中心两侧。
- 增加 `generate_signed_amplitude_waveform_tile_1x`，保持既有有符号覆盖范围，只按 ArrowVortex 的声道宽度相关整数标量把每行最大绝对幅值转换为 Alpha。
- 增加 `generate_rectified_amplitude_waveform_tile_1x`，复用相同采样边缘、整流投影和振幅整数算术完成第四种 1x 组合；没有开放通用形状、亮度或抗锯齿配置。
- 增加 `generate_signed_uniform_waveform_tile_2x`，按 ArrowVortex 抗锯齿级别 `1` 以两倍宽高执行有符号均匀投影，再把每个 2×2 Alpha 块用非负整数平均降采样到原图块尺寸；没有开放通用抗锯齿参数或其他 2x 组合。
- 增加 `generate_rectified_uniform_waveform_tile_2x`，复用同一 2x 超采样与整数降采样路径，只把投影切换为 ArrowVortex 整流形状；没有开放通用形状、亮度或抗锯齿配置。
- 增加 `generate_signed_amplitude_waveform_tile_2x`，继续复用同一 2x 路径，只按两倍渲染宽度为每个超采样行计算 ArrowVortex 振幅亮度，再执行 2×2 非负整数平均；没有开放通用形状、亮度或抗锯齿配置。
- 增加 `generate_rectified_amplitude_waveform_tile_2x`，复用相同 2x 振幅亮度与整数降采样路径，只把投影切换为 ArrowVortex 整流形状，完成四种 2x 组合；没有开放通用形状、亮度或抗锯齿配置。
- 增加 `generate_signed_uniform_waveform_tile_3x`，按 ArrowVortex 抗锯齿级别 `2` 以三倍宽高执行有符号均匀投影，再把每个 3×3 Alpha 块用非负整数平均降采样到原图块尺寸；既有 2x 路径保持原样，没有开放通用抗锯齿参数或其他 3x 组合。
- 增加 `generate_rectified_uniform_waveform_tile_3x`，复用同一 3x 超采样与整数降采样路径，只把投影切换为 ArrowVortex 整流形状；没有开放通用形状、亮度或抗锯齿配置。
- 增加 `generate_signed_amplitude_waveform_tile_3x`，继续复用同一 3x 路径，只按三倍渲染宽度为每个超采样行计算 ArrowVortex 振幅亮度，再执行 3×3 非负整数平均；没有开放通用形状、亮度或抗锯齿配置。
- 增加 `generate_rectified_amplitude_waveform_tile_3x`，复用相同 3x 振幅亮度与整数降采样路径，只把投影切换为 ArrowVortex 整流形状，完成四种 3x 组合；没有开放通用形状、亮度或抗锯齿配置。
- 增加 `generate_signed_uniform_waveform_tile_4x`，按 ArrowVortex 抗锯齿级别 `3` 以四倍宽高执行有符号均匀投影，再把每个 4×4 Alpha 块用非负整数平均降采样到原图块尺寸；既有 1x/2x/3x 路径保持原样，没有开放通用抗锯齿参数或其他 4x 组合。
- 对末尾图块的四舍五入索引显式限制到最后一个有效 PCM 帧，消除参考代码的越界未定义行为；负数幅值缩放显式匹配算术右移结果。
- 增加四份 1x、四份 2x、四份 3x 与一份 4x、共十三份 65536 字节 CC0 Alpha 黄金夹具，固定 UI Zoom `11`、Scale `4`、block `1` 的左右声道逐字节结果及 SHA-256；每个新组合都重新逐字节验证全部既有输出，并覆盖均匀亮度 2x 的 `0/63/127/191/255`、均匀亮度 3x 的 `0/28/56/85/113/141/170/198/226/255`、均匀亮度 4x 的 `0/15/31/47/63/79/95/111/127/143/159/175/191/207/223/239/255`、振幅亮度 2x/3x 的超采样行亮度与整数平均、1x 每行单一振幅 Alpha、中心对称、形状/亮度/采样差异、末帧限制和最小 Scale 下的 `247/239` 亮度边界。
- 暂不实现其他 4x 组合、波形过滤、CLI 波形命令、时序、SLK、C ABI 或 Flutter。

## 工具链

- CMake：`3.27.7`
- C/C++ 编译器：Apple Clang `17.0.0`，arm64 macOS
- Ninja：未安装，阶段 0 使用 CMake 默认生成器
- Flutter：未安装，原生阶段暂不需要
- Git：已初始化，默认分支为 `main`

## 修改文件

- `.gitignore`
- `.gitattributes`
- `LICENSE`
- `ARCHITECTURE.md`
- `TESTING.md`
- `IMPLEMENTATION_PLAN.md`
- `IMPLEMENTATION_STATUS.md`
- `native/CMakeLists.txt`
- `native/third_party/CMakeLists.txt`
- `native/third_party/README.md`
- `native/third_party/libmad/`
- `native/third_party/libogg/`
- `native/third_party/libvorbis/`
- `native/third_party/tests/`
- `native/av_core/CMakeLists.txt`
- `native/av_core/include/av_core/audio/mp3_decoder.hpp`
- `native/av_core/include/av_core/audio/ogg_vorbis_decoder.hpp`
- `native/av_core/include/av_core/audio/pcm16.hpp`
- `native/av_core/include/av_core/audio/pcm16_hash.hpp`
- `native/av_core/include/av_core/waveform/waveform_tile.hpp`
- `native/av_core/include/av_core/version.hpp`
- `native/av_core/src/audio/mp3_decoder.cpp`
- `native/av_core/src/audio/ogg_vorbis_decoder.cpp`
- `native/av_core/src/audio/pcm16.cpp`
- `native/av_core/src/audio/pcm16_hash.cpp`
- `native/av_core/src/waveform/waveform_tile.cpp`
- `native/av_core/src/version.cpp`
- `native/av_core/tests/CMakeLists.txt`
- `native/av_core/tests/fixtures/mp3/`
- `native/av_core/tests/fixtures/ogg_vorbis/`
- `native/av_core/tests/fixtures/waveform/`
- `native/av_core/tests/mp3_decoder_test.cpp`
- `native/av_core/tests/ogg_vorbis_decoder_test.cpp`
- `native/av_core/tests/pcm16_test.cpp`
- `native/av_core/tests/version_test.cpp`
- `native/av_core/tests/waveform_tile_test.cpp`
- `native/tools/avtool/CMakeLists.txt`
- `native/tools/avtool/main.cpp`
- `native/tools/avtool/tests/`

## 最后成功验证

2026-08-30 在 arm64 macOS 上依次运行：

```text
cmake -S native -B build/native-waveform-signed-uniform-4x-clean -DBUILD_TESTING=ON
cmake --build build/native-waveform-signed-uniform-4x-clean --parallel 2
ctest --test-dir build/native-waveform-signed-uniform-4x-clean --output-on-failure

cmake -S native -B build/native-waveform-signed-uniform-4x-asan -DBUILD_TESTING=ON -DCMAKE_C_FLAGS='-fsanitize=address -fno-omit-frame-pointer' -DCMAKE_CXX_FLAGS='-fsanitize=address -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address'
cmake --build build/native-waveform-signed-uniform-4x-asan --parallel 2
ctest --test-dir build/native-waveform-signed-uniform-4x-asan --output-on-failure

cmake -S native -B build/native-waveform-signed-uniform-4x-asan-ubsan -DBUILD_TESTING=ON -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/native-waveform-signed-uniform-4x-asan-ubsan --parallel 2 --target av_core_waveform_tile_test
ctest --test-dir build/native-waveform-signed-uniform-4x-asan-ubsan -R '^av_core.waveform_tile$' --output-on-failure

git diff --check
```

结果：全新普通配置和构建成功，14/14 测试通过；全新 AddressSanitizer 构建后 14/14 通过；AddressSanitizer/UndefinedBehaviorSanitizer 组合下单独运行 `av_core.waveform_tile` 1/1 通过。固定双声道 PCM 在 UI Zoom `11`、Scale `4`、block `1` 下继续得到 `594` pixels/s、`256` 声道宽度、`24` 声道间距；新增有符号均匀亮度 4x Left/Right Alpha 与独立 ArrowVortex 捕获的 65536 字节黄金逐字节相同，SHA-256 为 `0b732ac7f11a5ea7dd923d46e6ada4befa9f6333af19ed9d07344e70bb09e2e6`，Alpha 精确包含 4×4 二值平均产生的 `0/15/31/47/63/79/95/111/127/143/159/175/191/207/223/239/255`。既有四份 3x 黄金保持 `c5a9044914fa046e9d05884e35f450b0192d172882f6225cb6eccfaea050485f`、`b624da9bc4692a14cba56b625ce021df1ad6a7b3a9c084d715c5b2f3b15a523f`、`a8960ca342b30cb1a20353290dd006c416228ff300bb2255a6924bf3401caa7f` 和 `295b3071d96e86e7fa27244989387ec57a549de0ac1412171ef0eb88f27bb646`，四份 2x 黄金保持 `2243a44abad0fcec5ec1114895c16068bcc8248f3560a02027fcf3dc1daff9e5`、`f912279c24fa66d0c508c455d970b7640b074a2dc8ce64c9e3220947a476dc88`、`1218049f713c135283487d9553ab5e3e0d28cdc800062f3c02d7ffc2591ef7b9` 和 `44eb3412dfb110695eb0cfa87635283e7eee360b904c76858ae79830d6c47f03`，四份 1x 黄金仍分别保持 `aa2bf6026ef8ca04de51f89e79eb8fefc4de73f58cbbfd02924d82473c5d5fe6`、`9ba7cb0c431b92bcbbed2c0b9d332c51a0c0ded9ce4b520c4d8e82c37a9d7161`、`f085dceeff95f50a60f97b8b9875489ad51e662874dc68cd80c01ee71f98e8c7` 和 `5272c1d45caf622592a74dc5129d290c9c36eb6b715107e39621996e4c5c6065`；既有末帧限制与音频结束后的全透明 block 行为保持不变。

## 已知问题

- 完整 GPLv3 正文和第三方声明必须在分发前离线纳入仓库；当前 `LICENSE` 先固定 `GPL-3.0-or-later` 声明。
- 示例 MP3/Ogg 的再分发许可尚未确认，已通过 `.gitignore` 排除；两个 SLK 继续保留为本地规范与测试参考。
- Flutter 工具链尚未安装，不阻塞当前原生核心阶段。
- 对固定的 libmad/libogg/libVorbis 旧版 C 快照执行全量 UndefinedBehaviorSanitizer 时，会在现有解码路径报告负下标式指针访问、空指针零偏移和有符号左移诊断；普通构建与全量 AddressSanitizer 仍精确通过全部黄金结果。本切片未修改第三方兼容代码，也未用测试过滤这些诊断；本次新增的有符号均匀亮度 4x 波形路径已单独通过 AddressSanitizer/UndefinedBehaviorSanitizer。

## 唯一下一步

在现有 4x 超采样与参考整数降采样路径上增加 ArrowVortex 整流均匀亮度 4x，并为同一固定 PCM、UI Zoom `11`、Scale `4`、block `1` 捕获独立逐字节黄金 Alpha；保持当前十三份输出完全不变。本切片只增加整流均匀亮度 4x，不实现其他 4x、过滤、CLI、C ABI 或 Flutter。
