# 实现状态

更新时间：2026-08-30

## 当前阶段

阶段 0：工程基线已完成。下一切片进入阶段 1，先定义规范 PCM 数据模型和不变量。

## 本次范围

- 增加稳定实施计划和可跨会话读取的状态文件。
- 增加 Git 忽略与 SLK 二进制属性规则。
- 建立可直接调用的 C++ 核心、CLI 和最小测试。
- 不实现音频、波形、时序、SLK 或谱面领域行为。

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
- `native/av_core/include/av_core/version.hpp`
- `native/av_core/src/version.cpp`
- `native/av_core/tests/CMakeLists.txt`
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

结果：配置和构建成功；`av_core.version`、`avtool.version` 共 2 项测试全部通过。

## 已知问题

- 完整 GPLv3 正文和第三方声明必须在分发前离线纳入仓库；当前 `LICENSE` 先固定 `GPL-3.0-or-later` 声明。
- 示例 MP3/Ogg 的再分发许可尚未确认，已通过 `.gitignore` 排除；两个 SLK 继续保留为本地规范与测试参考。
- Flutter 工具链尚未安装，不阻塞当前原生核心阶段。

## 唯一下一步

定义 `av_core` 的规范双声道平面 PCM16 数据模型、尺寸与溢出校验，并使用纯内存 C++ 单元测试固定其所有权和错误契约；本切片暂不接入解码器。
