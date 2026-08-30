# 实现状态

更新时间：2026-08-30

## 当前阶段

阶段 1：规范 PCM 与音频解码进行中；纯内存 MP3/Ogg Vorbis 解码已完成，下一切片连接 `avtool decode` 诊断入口。

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
- 暂不实现 CLI 音频命令、波形、时序、SLK 或 Ogg 编码行为。

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
- `native/av_core/include/av_core/version.hpp`
- `native/av_core/src/audio/mp3_decoder.cpp`
- `native/av_core/src/audio/ogg_vorbis_decoder.cpp`
- `native/av_core/src/audio/pcm16.cpp`
- `native/av_core/src/version.cpp`
- `native/av_core/tests/CMakeLists.txt`
- `native/av_core/tests/fixtures/mp3/`
- `native/av_core/tests/fixtures/ogg_vorbis/`
- `native/av_core/tests/mp3_decoder_test.cpp`
- `native/av_core/tests/ogg_vorbis_decoder_test.cpp`
- `native/av_core/tests/pcm16_test.cpp`
- `native/av_core/tests/version_test.cpp`
- `native/tools/avtool/CMakeLists.txt`
- `native/tools/avtool/main.cpp`

## 最后成功验证

2026-08-30 在 arm64 macOS 上依次运行：

```text
cmake -S native -B build/native-ogg-clean -DBUILD_TESTING=ON
cmake --build build/native-ogg-clean --parallel 2
ctest --test-dir build/native-ogg-clean --output-on-failure
git diff --check
```

结果：配置和构建成功；`third_party.audio_codec_snapshot`、`av_core.version`、`av_core.pcm16`、`av_core.mp3_decoder`、`av_core.ogg_vorbis_decoder`、`avtool.version` 共 6 项测试全部通过。
另以 AddressSanitizer 和 UndefinedBehaviorSanitizer 全量重建并运行相同 6 项测试，全部通过。MP3 两份、Ogg Vorbis 三份 PCM16LE 黄金文件均与实际编译的 ArrowVortex 对应加载器输出逐字节相同。

## 已知问题

- 完整 GPLv3 正文和第三方声明必须在分发前离线纳入仓库；当前 `LICENSE` 先固定 `GPL-3.0-or-later` 声明。
- 示例 MP3/Ogg 的再分发许可尚未确认，已通过 `.gitignore` 排除；两个 SLK 继续保留为本地规范与测试参考。
- Flutter 工具链尚未安装，不阻塞当前原生核心阶段。

## 唯一下一步

实现计划中的 `avtool decode <audio> --pcm-hash`：CLI 只负责读取文件和展示稳定、独立于区域设置的机器可读结果，MP3/Ogg 解码及规范 PCM 必须调用现有 `av_core` 权威入口；集成测试对同一夹具比较 CLI 与直接 C++ API 的格式、采样率、帧数和完整平面 PCM16 哈希。本切片不实现波形、检测器、C ABI 或 Ogg 编码。
