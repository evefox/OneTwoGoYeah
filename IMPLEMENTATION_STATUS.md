# 实现状态

更新时间：2026-08-30

## 当前阶段

阶段 1：规范 PCM 数据模型已完成。下一切片固定并接入受控的 MP3/Ogg 解码依赖。

## 本次范围

- 增加稳定实施计划和可跨会话读取的状态文件。
- 增加 Git 忽略与 SLK 二进制属性规则。
- 建立可直接调用的 C++ 核心、CLI 和最小测试。
- 定义双声道平面 PCM16 的采样率、帧数、字节尺寸、所有权和稳定错误契约。
- 使用纯内存 C++ 测试覆盖有效值、空 PCM、深拷贝/移动所有权、空指针、尺寸不匹配和溢出边界。
- 不接入 MP3/Ogg 解码器，也不实现波形、时序、SLK 或谱面领域行为。

## 工具链

- CMake：`3.27.7`
- C++ 编译器：Apple Clang `17.0.0`，arm64 macOS
- Ninja：未安装，阶段 0 使用 CMake 默认生成器
- Flutter：未安装，原生阶段暂不需要
- Git：已初始化，默认分支为 `main`

## 修改文件

- `.gitignore`
- `.gitattributes`
- `LICENSE`
- `IMPLEMENTATION_PLAN.md`
- `IMPLEMENTATION_STATUS.md`
- `native/CMakeLists.txt`
- `native/av_core/CMakeLists.txt`
- `native/av_core/include/av_core/audio/pcm16.hpp`
- `native/av_core/include/av_core/version.hpp`
- `native/av_core/src/audio/pcm16.cpp`
- `native/av_core/src/version.cpp`
- `native/av_core/tests/CMakeLists.txt`
- `native/av_core/tests/pcm16_test.cpp`
- `native/av_core/tests/version_test.cpp`
- `native/tools/avtool/CMakeLists.txt`
- `native/tools/avtool/main.cpp`

## 最后成功验证

2026-08-30 在 arm64 macOS 上依次运行：

```text
cmake -S native -B build/native -DBUILD_TESTING=ON
cmake --build build/native
ctest --test-dir build/native --output-on-failure
```

结果：配置和构建成功；`av_core.version`、`av_core.pcm16`、`avtool.version` 共 3 项测试全部通过。

## 已知问题

- 完整 GPLv3 正文和第三方声明必须在分发前离线纳入仓库；当前 `LICENSE` 先固定 `GPL-3.0-or-later` 声明。
- 示例 MP3/Ogg 的再分发许可尚未确认，已通过 `.gitignore` 排除；两个 SLK 继续保留为本地规范与测试参考。
- Flutter 工具链尚未安装，不阻塞当前原生核心阶段。

## 唯一下一步

固定 libmad、libogg、libvorbis 的来源、版本、编译定义与许可证，建立只包含解码所需代码的可复现构建骨架；本切片暂不改变 PCM 模型或接入平台音频 API。
