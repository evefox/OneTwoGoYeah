# 实现状态

更新时间：2026-08-30

## 当前阶段

阶段 2：ArrowVortex 兼容波形进行中；有符号与整流形状、均匀亮度、1x 采样的 CPU Alpha 图块及 Zoom/Scale 几何已完成。

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
- 从固定 ArrowVortex `Waveform.cpp`/`View.cpp` 参考提取 CPU 波形采样边缘、负幅值整数缩放以及有符号/整流均匀 Alpha 填充，核心不依赖全局视图、GPU 或纹理。
- 增加拥有 Left/Right 平面的 `WaveformTile` 和稳定结果/错误类型；支持 UI Zoom `1..19`、Scale `1..10`、128 行 block、空音频和音频末尾透明图块。
- 增加 `generate_rectified_uniform_waveform_tile_1x`，与既有有符号入口共享全部几何、采样边缘、限制、错误和所有权路径，只按 ArrowVortex 规则把每行最大绝对幅值对称投影到声道中心两侧。
- 对末尾图块的四舍五入索引显式限制到最后一个有效 PCM 帧，消除参考代码的越界未定义行为；负数幅值缩放显式匹配算术右移结果。
- 增加有符号与整流两份 65536 字节 CC0 Alpha 黄金夹具，固定 UI Zoom `11`、Scale `4`、block `1` 的左右声道逐字节结果及 SHA-256；整流切片继续逐字节重验既有有符号输出，并覆盖中心对称性、形状差异、几何边界、错误、所有权和末帧安全性。
- 暂不实现振幅亮度、2x/3x/4x 降采样、波形过滤、CLI 波形命令、时序、SLK、C ABI 或 Flutter。

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
cmake -S native -B build/native-waveform-rectified-clean -DBUILD_TESTING=ON
cmake --build build/native-waveform-rectified-clean --parallel 2
ctest --test-dir build/native-waveform-rectified-clean --output-on-failure

cmake -S native -B build/native-waveform-rectified-asan -DBUILD_TESTING=ON -DCMAKE_C_FLAGS='-fsanitize=address -fno-omit-frame-pointer' -DCMAKE_CXX_FLAGS='-fsanitize=address -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address'
cmake --build build/native-waveform-rectified-asan --parallel 2
ctest --test-dir build/native-waveform-rectified-asan --output-on-failure

cmake -S native -B build/native-waveform-rectified-asan-ubsan -DBUILD_TESTING=ON -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/native-waveform-rectified-asan-ubsan --parallel 2 --target av_core_waveform_tile_test
ctest --test-dir build/native-waveform-rectified-asan-ubsan -R '^av_core.waveform_tile$' --output-on-failure

git diff --check
```

结果：全新普通配置和构建成功，14/14 测试通过；全新 AddressSanitizer 构建后 14/14 通过；AddressSanitizer/UndefinedBehaviorSanitizer 组合下单独运行 `av_core.waveform_tile` 1/1 通过。固定双声道 PCM 在 UI Zoom `11`、Scale `4`、block `1` 下继续得到 `594` pixels/s、`256` 声道宽度、`24` 声道间距；新增整流 Left/Right Alpha 与独立 ArrowVortex 捕获的 65536 字节黄金逐字节相同，SHA-256 为 `9ba7cb0c431b92bcbbed2c0b9d332c51a0c0ded9ce4b520c4d8e82c37a9d7161`。既有有符号黄金仍逐字节相同，SHA-256 保持 `aa2bf6026ef8ca04de51f89e79eb8fefc4de73f58cbbfd02924d82473c5d5fe6`。

## 已知问题

- 完整 GPLv3 正文和第三方声明必须在分发前离线纳入仓库；当前 `LICENSE` 先固定 `GPL-3.0-or-later` 声明。
- 示例 MP3/Ogg 的再分发许可尚未确认，已通过 `.gitignore` 排除；两个 SLK 继续保留为本地规范与测试参考。
- Flutter 工具链尚未安装，不阻塞当前原生核心阶段。
- 对固定的 libmad/libogg/libVorbis 旧版 C 快照执行全量 UndefinedBehaviorSanitizer 时，会在现有解码路径报告负下标式指针访问、空指针零偏移和有符号左移诊断；普通构建与全量 AddressSanitizer 仍精确通过全部黄金结果。本切片未修改第三方兼容代码，也未用测试过滤这些诊断；本次新增的不经解码器 PCM 哈希路径已单独通过 UBSan。

## 唯一下一步

在现有共享采样边缘和有符号 1x 投影上增加 ArrowVortex 振幅亮度，并为同一固定 PCM、UI Zoom `11`、Scale `4`、block `1` 捕获独立逐字节黄金 Alpha；保持有符号/整流两份均匀亮度输出完全不变。本切片不实现整流振幅亮度、2x/3x/4x、过滤、CLI、C ABI 或 Flutter。
