# 实现状态

更新时间：2026-11-15

## 当前阶段

阶段 8 已完成：Flutter 三列编辑器、Dart FFI 原生资产、真实音频/谱面工作流、私有草稿与异常恢复、四平台播放和文件适配、桌面关闭保护、响应式输入与无障碍均已落地。阶段 8.5 的 macOS 体验微调已完成并通过整体验收，当前进入阶段 9 发布前兼容验证。

## 阶段 9：发布前兼容验证（进行中）

已有发布核查覆盖当前 Apple Silicon 主机，并在真实 Windows x86-64 主机上补齐原生构建、App Release 构建与实机冒烟；Windows ARM64、真实 iOS 设备以及 Android 真机/模拟器的 Release 冒烟仍需在对应环境补齐。以下发布产物与核查数据来自既有阶段 9 记录，未包含本轮多选与延迟剪切改动；本轮实测进度见文末“当前工作与待验证项”。

### 已完成

- 发布资料已纳入仓库：`LICENSES/GPL-3.0.txt` 为完整 GPLv3 正文，
`LICENSES/FLUTTER-BSD-3-CLAUSE.txt` 为 Flutter 相关 BSD 3-Clause 文本，
`THIRD_PARTY_NOTICES.md` 汇总固定的 libmad/libogg/libVorbis、Flutter 和
Dart 依赖声明；示例音频、参考音效和本地导出文件仍由 Git 忽略。
- 原生 CMake/CTest 黄金与一致性套件 `50/50` 通过；Dart FFI 分析和测试
`7/7` 通过；Flutter 分析无问题，Flutter 回归 `92/92` 通过。
- ARM64 与 x86-64 原生构建均通过完整 `50/50` 黄金与一致性套件。为消除固定
libVorbis 在两种 ISA 上的 libm 低位、浮点收缩/向量化和 x86 专用
`vorbis_ftoi` 差异，第三方边界现在使用统一 binary64 量化、严格浮点编译选项
和标量参考转换；Ogg 解码/编码以及依赖其 PCM 的 BPM/联合分析共用一套更新后
黄金，未放宽任何比较容差。
- Android Release APK 已使用 Flutter `3.47.2`、Dart `3.13.2`、SDK `36`
和 NDK `28.2.13676358` 构建，只包含 `arm64-v8a`。
`scripts/build_android_release.py` 固定 `--target-platform android-arm64`，构建后
校验产物 ABI，再生成 `build/releases/OneTwoGoYeah-1.0.0-arm64.apk`；该包的
`libotgy_native.so` 导出 `otgy_chart_create_empty`，依赖 `libm.so`，不依赖或
打包 `libc++_shared.so`。包版本为 `1.0.0 (versionCode 1)`，APK 大小为
`22,445,828` 字节，SHA-256 为
`70b8631e9ee2101a893b02fc352f5e9a3cd0d134c851d2a2567110cd5cd408e9`。当前
使用本地 Debug signing key，仅适合个人真机测试，不适合商店发布；32 位与
模拟器 ABI 不再进入发布流程。
- macOS Release App 已使用 Xcode `26.6`（macOS SDK `26.5`）构建；主程序为
`x86_64 + arm64` Universal，版本为 `1.0.0 (1)`。`codesign --verify` 通过，
但当前为本机 Ad Hoc 签名，保留 `com.apple.security.files.user-selected.read-write`
和开发调试 entitlement；对外分发仍需 Developer ID 签名和 notarization。
- iOS 真机目标 Release 无签名构建通过，产物为 `arm64`、版本 `1.0.0 (1)`；
Flutter 不支持 simulator 的 Release 模式，因此该命令不作为失败项。当前尚未
在真实 iOS 设备上完成签名安装和运行冒烟。
- 已在 `build/releases/` 生成带离线许可证资料的本地归档（该目录被 Git 忽略）：
`OneTwoGoYeah-1.0.0+1-android-bundle.zip`、
`OneTwoGoYeah-1.0.0+1-macos-universal-release.zip` 和
`OneTwoGoYeah-1.0.0+1-macos-universal-bundle.zip`。当前 SHA-256 分别为：
    ```text
    79418516971a076c253f8831e82f83f95a78551113ef28adf3d3a991a9a0e978  OneTwoGoYeah-1.0.0+1-android-bundle.zip
    3bfbbf0f2c3f8c63386353e5275d76dec0fb8c2fa14a0545ff54affea20ce1aa  OneTwoGoYeah-1.0.0+1-macos-universal-bundle.zip
    9ab041eacc50b8a8a4d58182c175b3ffc394428033f5a5bae1eff392d2f43f4a  OneTwoGoYeah-1.0.0+1-macos-universal-release.zip
    ```

    三个 ZIP 均通过 `unzip -t`；macOS 归档中的主程序再次确认同时包含
    `x86_64` 和 `arm64`。

## 阶段 8.5：macOS 体验微调验收

- 使用 macOS Debug App 进行连续体验，优先处理影响操作手感、视觉层级和反馈清晰度的细小问题。
- 每次只处理一个最小颗粒的调整，修改、验证和结果保持可独立识别，不把多个无关体验变化合并为一批。
- 每项调整先在本地验证；确认符合预期后单独提交，未通过验收则回退该项，不影响已确认的其他修改。
- 阶段 8.5 期间不改变已确认的领域数据、算法、文件兼容性或跨平台契约；发现需要扩大范围的行为变化时，先单独记录再决定是否进入实现。
- 由用户确认 macOS 整体体验没有问题后，阶段 8.5 才算完成并转入阶段 9。

阶段 8.5 验收结果：用户已确认当前体验微调基本完成，macOS 整体体验通过验收；本阶段关闭，不再保留未验收的体验调整项。

本阶段已确认纳入的体验微调，按以下顺序分别实现和验收：

1. 播放时隐藏 1/16 音符线和拍线，只保留小节线；暂停后恢复完整时序网格。该调整已完成并通过 macOS 体验验收；只影响播放态视觉，不改变吸附、编辑、BPM、Offset、谱面数据或导出。
2. 播放跟随改为屏幕帧同步的连续视觉插值；音频设备实际播放帧仍是暂停、结束和中断位置的唯一权威来源。已实现屏幕 `Ticker`、`50ms` 预测上限、重复设备帧防漂移和暂停精确回归，并通过完整自动化验证与 macOS 体验验收。该调整只影响播放态画面，不改变音频输出和核心时序。
3. 编辑态以 `46px` 目标 1/16 行距自动选择初始 `0.25` Zoom，用户手动 Zoom 优先；播放态使用最高 `170px/s` 的独立时间尺度，5 秒范围最多 `850px`。播放受限时改用核心按同一 `170px/s` 密度生成的波形缓存，消除从高密度编辑波形进行非整数逐帧抽样造成的闪烁；暂停后恢复编辑 Zoom 和编辑波形。Flutter 48 项、原生 50 项、静态分析和 macOS Debug 构建已通过，并完成 macOS 体验验收。

三项调整各自测试、各自提交或回退。三态“开/关/节奏提取”波形切换继续搁置，直到另行确认阈值定义和实现方案。

## 本次范围

- 建立 `app/` Flutter App 和私有 `otgy_native` Dart FFI 包；构建钩子直接编译唯一 `av_core` 与轻量 C ABI，不保留模板原生实现副本。
- 实现两行工具栏、NPC/玩家/Showtime 三列画布、波形、时序网格、标记和 Showtime 范围、悬浮导航、状态栏、响应式窄屏布局、键鼠/触控、快捷键和无障碍语义。
- 接通 MP3/Ogg 选择与事务替换、六阶段 BPM/Offset 联合分析、三位小数显示但高精度提交、共享 Ogg 导出进度/取消，以及逐字节规范 SLK 导入校验导出。
- 实现正式草稿列表、保存/另存为/覆盖/删除、500 毫秒恢复快照、异常会话恢复、未保存操作与退出保护；Windows/macOS 窗口关闭通过生命周期通道等待 Flutter 确认。
- 新增共享 `render_playback_pcm` 与按设备帧推进，四平台分别用 Android `AudioTrack`、Apple `AVAudioEngine` 和 Windows `waveOut` 输出；暂停、自然结束、焦点或设备中断保留最后确认帧且不自动续播。
- 新增线程安全 C ABI 音频导出取消句柄和跨 isolate 协作取消；保存阶段继续全局门控，临时文件优先通过 Android/POSIX/Windows 原子不替换硬链接发布，macOS 另有 `renameatx_np(RENAME_EXCL)` 和用户选定路径 `O_EXCL` 写入回退，同名或并发冲突选择最小 `_2/_3` 后缀。
- 新增阶段 8 多平台 CI，覆盖原生 Ubuntu/macOS/Windows、Flutter Android/Windows/macOS/iOS 构建，以及 Dart FFI/Flutter 分析和测试。
- 修复 Flutter 后台联合分析闭包捕获不可发送 `ReceivePort` 导致 BPM/Offset 分析未启动的问题；后台 isolate 回归现直接取得真实候选和六阶段终态。
- 修复 Apple 播放暂停只挂起节点以及旧 buffer/旧设备查询回调污染下一次播放的问题；暂停现保留实际帧后停止输出，Flutter 与 Apple 适配层都以播放代次丢弃过期结果，macOS Runner 测试锁定替代播放状态。
- 修复 Dart 原生资产构建钩子在非代码资产阶段读取 `HookConfig.code` 的失败，macOS 调试运行与 Xcode Runner 测试均可完成构建。
- 增加 macOS 原生触控板映射：以纵向为主的双指滑动平滑移动时间线，以水平方向为主的滑动和旋转不占用功能；捏合围绕固定光标线连续预览 Zoom，结束时吸附到 `0.25` 级别，并增加 `Command +/-/0` Zoom 快捷键。
- 帮助页“快捷键与手势”按平台拆分：桌面只列键盘、鼠标和滚轮操作，移动端只列触控操作；桌面六项快捷键可在页内改绑并写入 `ui-preferences.json`，Windows 默认键位对齐 ArrowVortex（`Space`、`Ctrl+Z`、`Ctrl+Y`、`Numpad +/-`，Zoom 重置未绑定）。
- 修复 BPM 直接输入被中间值刷新截断的问题；输入期间保留完整局部文本，按回车或失焦后才校验并提交一次，非法、空白和越界输入恢复当前权威值。
- Offset 直接输入采用相同的编辑结束提交规则；画布当前时间光标/播放判定线移到高度 `25%`，移除三列内部纵向分割线并增强小节线、拍线；悬浮控件脱离画布手势命中层以稳定单击。`SHOW-D` 常规范围与最后半小节改为不重叠的深、浅填充，像素回归锁定由深到浅的视觉层级。
- 播放画布使用屏幕帧同步的独立绘制位置，在设备帧确认之间连续插值且最多预测 `50ms`；重复确认不会延长预测窗口，暂停、结束或中断立即恢复设备权威行，逐帧更新限制在画布 `RepaintBoundary` 内。
- 增加谱面播放音效：右侧波形按钮下方提供中文/英文、4K/8K 和切换音效设置；`av_core` 唯一实现 WAV PCM16 解码、固定种子随机、NPC 使用 `m1`、玩家使用关联 `f1` 的 N/S 音效配对、SHOW 音效、确定性重采样和饱和混音，暂停续播保持同一音效计划。两套 46 个 WAV 已随 App 离线打包，macOS Debug 构建与完整核心/C ABI/Flutter 回归通过。
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
- 增加 `calculate_pcm_offset_plan` 与 `apply_pcm_offset`，按当前采样率执行正 Offset 双声道前置静音、负 Offset 开头裁剪和零 Offset 深复制；小数帧正好一半时远离零，且不覆盖权威 Offset。
- Offset 测试覆盖正负/零、正负半帧、删除全部帧拒绝、有限范围、尺寸溢出、输入不变和深复制/移动所有权。
- 增加共享音频导出基础名和最终建议名纯函数，保留合法 Unicode，统一处理控制字符、跨平台非法字符、首尾空白、末尾句点、保留名称、时间戳及 `_2` 起最小冲突后缀。
- MP3/Ogg 解码成功结果增加自有 `AudioSourceInfo`；MP3 按解码样本数加权测得平均码率且排除 Xing 帧，Ogg 保留有效标称码率并以测得/容器时长码率回退。
- 增加 `calculate_audio_export_quality`，直接查询固定 libvorbis 在当前采样率下品质 `1..5` 的标称码率；未知来源上限为 `5`，默认值为 `min(3, ceiling)`，明确拒绝越界或超过来源上限的请求。
- 把固定 libVorbis `vorbisenc.c`、公开编码头和 23 份模式/码本表纳入离线快照；第三方源码与确定性数学头总摘要经复核为 `2cc53e72e738fad21c8346146dbf976e088ea327adcf653fd83ea7b374214011`。
- 增加进程内 `encode_ogg_vorbis`，固定 Ogg serial、1024 帧提交块、PCM float 转换、品质映射和 TITLE/ARTIST/ALBUM 顺序；有效 UTF-8 元数据确定写入，无效字段省略。
- 增加品质 `1`、`3`、`5` 三份 CC0 确定性 Ogg 黄金输出，分别固定 SHA-256 并在普通测试中完整逐字节比较；重复编码、解码有效性、进度、取消、错误和所有权均通过。
- 增加 `AudioExportSnapshot` 与高层编码任务，在确认时深复制 PCM/元数据并捕获 PCM 标识、原始 Offset、品质、来源信息、规范基础名和变换计划；在途任务只读取快照，取消不返回部分字节。
- 后台 Ogg 编码任务移至不捕获 `EditorController` 的顶层 isolate helper，避免播放音效资源加载 Future 进入 isolate 消息而导致导出在准备阶段失败；导出异常弹窗保留类型、平台详情和堆栈，并支持复制完整诊断。
- 增加纯 `coordinate_application_operation` 门控；`Playing` 只允许暂停和切换波形，`ExportingAudio` 只允许取消导出，其余分别返回稳定 `PlaybackActive`/`AudioExportActive`。
- 实现 `avtool export-audio <audio> --offset <seconds> --quality <q> --output <file.ogg>`，严格独立于区域设置解析参数，直接复用解码、来源品质、快照、Offset 和 Ogg 核心；同目录临时写入后以不覆盖式硬链接原子发布。
- 增加直接核心导出探针与真实 CLI 逐字节一致性测试，覆盖 MP3/Ogg、零/正/负半帧 Offset 和品质 `1/3/5`；稳定错误测试覆盖参数、来源上限、删除全部帧、读取/解码、目标格式、写入、已有目标及临时文件清理。
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
- 增加 `generate_signed_uniform_waveform_tile_4x`，按 ArrowVortex 抗锯齿级别 `3` 以四倍宽高执行有符号均匀投影，再把每个 4×4 Alpha 块用非负整数平均降采样到原图块尺寸；既有 1x/2x/3x 路径保持原样，没有开放通用形状、亮度或抗锯齿配置。
- 增加 `generate_rectified_uniform_waveform_tile_4x`，复用同一 4x 超采样与整数降采样路径，只把投影切换为 ArrowVortex 整流形状；没有开放通用形状、亮度或抗锯齿配置。
- 增加 `generate_signed_amplitude_waveform_tile_4x`，继续复用同一 4x 路径，只按四倍渲染宽度为每个超采样行计算 ArrowVortex 振幅亮度，再执行 4×4 非负整数平均；没有开放通用形状、亮度或抗锯齿配置。
- 增加 `generate_rectified_amplitude_waveform_tile_4x`，复用相同 4x 振幅亮度与整数降采样路径，只把投影切换为 ArrowVortex 整流形状，完成四种 4x 组合；没有开放通用形状、亮度或抗锯齿配置。
- 在 `av_core` 私有固定 ArrowVortex Aubio/Ooura FFT/FindOnsets 最小源码切片，公开 `detect_onsets(Pcm16)` 直接 C++ 入口；固定 256 帧窗口、1024 缓冲、`complex` 方法和实际联合分析使用的单线程分支，不公开线程或算法配置。
- 规范 PCM 按 `(float)((int)left + (int)right) / 65536.0f` 精确混音，不重采样；起音结果拥有 `uint64` PCM 帧索引和未舍入 binary64 strength，空/短 PCM 返回空序列，参考 `int` 范围及分配失败返回稳定错误且不暴露部分序列。
- 增加原始双声道 PCM 的单起音和重复片段派生 PCM 的六起音独立黄金；逐项精确比较位置和 strength 位模式，并覆盖空、短、静音、左右交换、保持左右和的混音变换、范围错误、错误文本及深复制/移动所有权。
- 增加 `detect_bpm_candidates(Pcm16)` 直接 C++ 入口，调用唯一的起音实现并按参考只重算前 100 个起音 strength；固定 `89..205` BPM、interval 粗扫/精扫、多项式归一化、fitness 稳定排序、重复/倍频去除、整数吸附和前三名截断，不计算 Offset。
- 把规范 PCM 左右整数和除以 `65536.0f` 的兼容混音提取为起音与 BPM 共用的单一私有函数；独立参考确认 BPM 粗扫使用 1/2/8/16 线程时结果逐字节相同，核心固定串行项目顺序以消除硬件并发数输入。
- 增加单起音默认 `120/1.0` 与六起音三候选独立黄金，逐位比较未舍入 BPM、fitness 和顺序；覆盖最多三名、降序、空/短/静音、共享混音、低采样率环形窗口保护、参考整数上限、稳定错误及深复制/移动所有权。
- 增加 `detect_offsets(Pcm16, BpmCandidate[])` 直接 C++ 入口，以同一候选顺序返回检测器非负相位和显式一元取负后的 App 权威 Offset；BPM、fitness 不舍入也不重新排序。
- BPM 与 Offset 共用唯一的 2048 点 Hamming gap 窗和环形置信度实现；Offset 按起音位置固定权重建立基础相位，再以 `sampleRate / 20` 单边窗口的幅值上升斜率比较半拍 offbeat，平局保留基础相位。
- 增加一候选、三候选 Offset 黄金和真实 offbeat 胜出黄金，逐位覆盖正检测相位、负 App 值及 `+0.0 -> -0.0`；覆盖候选配对、共享混音、空/短/静音、空候选、非法候选、采样率/整数边界、稳定错误和所有权。
- 增加同步 `analyze_sync(Pcm16)` 联合入口，从 PCM 零帧开始把实际分析范围限制到 `min(frameCount, sampleRate * 600)`；起音只执行一次，BPM strength、候选和 Offset/offbeat 复用同一起音序列及帧边界。
- 联合入口按真实转换依次报告 DetectingOnsets、ScanningIntervals、RefiningCandidates、SelectingBpm、CalculatingOffsets、FinalizingResults 六阶段，并返回分析帧数及未舍入的完整 `OffsetCandidate` 配对。
- 增加三份联合候选独立黄金，逐位覆盖 BPM、检测/App Offset、fitness、顺序和 `-0.0`；覆盖三个独立入口结果一致性、六阶段、短/空 PCM、600 秒运行时前缀、最低采样率、最大乘法、参考整数边界、稳定错误及所有权。
- 增加 `TimingParameters` 及秒、连续拍、连续 1/16 行之间的六个纯换算入口；固定 `seconds = beat * (60 / bpm) - offset` 的 App Offset 符号与 binary64 运算顺序，不执行显示舍入、吸附或输入覆盖。
- 高精度换算覆盖默认 `64 行 = 16 拍 = 9.6 秒`、真实未舍入检测值、正负/小数坐标、BPM 零无网格但抽象拍/行仍可用、BPM/Offset 包含式范围、非有限输入、有限溢出和稳定错误。
- 增加 `generate_timing_grid`，在显式半开秒范围内只枚举非负整数 1/16 行，并复用高精度换算逐行生成未舍入秒数；固定 4/4 的 Measure/Beat/Sixteenth 分类和严格顺序。
- 网格覆盖默认 `[0, 9.6)` 的 64 行、4 条小节线/12 条其他拍线/48 条细分线、半开边界、真实检测 Offset、非负行、空范围、BPM 零空网格、非法输入、投影溢出、一百万条上限及深复制/移动所有权。
- 增加 `calculate_timeline_range`，在连续谱面行中统一取固定 `0..64` 四小节基础范围、可选音频结尾、全部事件行和全部 `SHOW-D.rangeEnd` 的最晚位置，同时返回未舍入起止秒数。
- 时间线范围覆盖四类最大项、多事件/多 Showtime 乱序扫描、音频秒位模式保留、Offset 起点投影、无音频空谱面、BPM 零局部 `100.0` 投影标记且不改权威输入、非法音频/时序/整数行和投影溢出。
- 增加 `calculate_timeline_navigation`，直接消费共享范围，在连续 1/16 行中一次返回到开头、上翻页、下翻页、到末尾目标及四个可用状态；页距固定为 `(visibleBeats - 1) * 4`。
- 导航覆盖小数当前位置/可见拍数、非吸附结果、首尾限制、精确边界状态、可见一拍的零页距禁用、无法继续移动、非法范围/位置/拍数和有限运算溢出。
- 增加纯 `start_playback`、`advance_playback`、`pause_playback` 位置状态；支持音频前负秒静音时间线、从非空音频末尾开始先回到零、到末尾自动暂停和普通暂停逐位保留位置，不启动设备或读取 PCM。
- 播放位置覆盖未舍入起点、有限增量、达到/越过末尾、暂停后不推进、零时长、负秒推进到零、无音频、非法时长/光标/增量/状态和稳定错误。
- 增加 `compose_slk_position`、`decompose_slk_position` 和 `validate_slk_position`，用唯一无符号整数实现生成四列、从总 1/16 行反向分解及严格导入一致性校验。
- SLK 位置覆盖 beat/sixteenth `0..3` 全部组合、多个 measure、小节边界、`uint64` 最大值分解/重组、分量越界、乘加溢出、第四列前后差一和稳定错误。
- 增加 `parse_slk_records(bytes)` 二进制入口，不解码标题字节；导入严格扫描结构合法的 ID、CRLF、六列类型 schema、受支持前导记录、Y3 起连续 sheet row、相邻 X1..X6、零个或一个 `[r & 0]` 和容器 `E`，允许不同 Excel/SYLK 工具产生的合法前导变体。
- 底层解析返回有序自有 `SlkParsedRow`，强制四列一致、位置递增及 `n/s/c/d/r` 值组合；规范/兼容 SLK 分别解析 428/404 行，并覆盖未知记录、schema/列/行号/整数/类型/边界错误、来源位置和所有权。
- 增加 `analyze_slk_drill_groups(records, beginRowIndex)`，在单个普通序列中配对紧邻的 NPC/玩家块，并在未消费的 Showtime 或文件边界前停止。
- `DrillGroup` 结果以 NPC 起始 sheet row 作为同文档稳定标识，逐条传播显式角色、来源行和块内索引；动作相对位置以各自首个 `n` 为原点，兼容两个 `c` 到首个 `n` 间隔不同的合法块。
- 规范/兼容 SLK 的两个普通序列分别识别 `14 + 20` 与 `16 + 18` 个组；合成测试覆盖合法 ID 前导变体、可选 `s`、后续阶段游标、最终空序列、深复制/移动，以及结构、内容、动作位置和结束边界的稳定来源错误。
- 增加 `analyze_slk_dance_move_drills(groupAnalysis)`，统一解码直接 `1..11` 与拆分 `segmentNCount * 100 + moveNCount`，按段拼接 NPC/玩家完整舞步并透传普通序列边界。
- `DanceMoveDrill` 以首组标识作为同文档稳定标识，保留来源组映射、原始 `c`、段长、显式角色及连续舞步行索引；规范/兼容两阶段分别还原 `6 + 11` 与 `8 + 9` 个演练。
- 舞步测试覆盖直接值全范围、`3+2+3=8`、两位数段长 `10+1=11`、真实 `moveNCount` 顺序、深复制/移动，以及无效值、数量、编码、总和、提前/缺失结尾和伪拆分的稳定来源错误。
- 增加 `analyze_slk_showtimes(records)`，从完整底层行逐阶段复用唯一分组/舞步入口，把连续 `d` 与有序 `DanceMoveDrill` 一一关联并在存在时消费末行 `[r & 0]`。
- Showtime/`SHOW-D` 分别以自身 sheet row 作为同文档稳定标识，每个 `SHOW-D` 自有保存来源行、阶段内索引、演练引用和已校验 `showNCount`；规范/兼容文件分别关联 `6+11` 与 `8+9` 个引用。
- 完整语义测试覆盖多阶段、稳定映射、可选边界与所有权，以及空阶段、末尾无 Showtime 阶段、下层失败、`d` 数量/值、错误边界和空输入的结构化原子错误。
- 增加 `import_finished_slk(bytes)` 原子入口，只编排现有记录解析与完整语义分析；全部成功才发布自有 `FinishedSlkDocument`，失败明确区分解析/语义层且不暴露部分值。
- 严格导入测试覆盖两份真实文件、输入清除、深复制/移动、空/空指针/损坏头，以及语法合法的空谱面和缺失 `d`，并校验未使用下层错误保持 `None`。
- 增加不依赖 SLK 的原生 `ChartDraft` 事件与关系容器，并由 `project_finished_slk_to_chart_draft(document)` 从已验证语义一次生成显式角色、稳定标识和 `showNCount`。
- 规范/兼容投影分别得到 `408/384` 个角色事件，且各有 `2` 个 Showtime 起点、`17` 个 `SHOW-D`、`34` 个演练组、`17` 个舞步演练和 `2` 个 Showtime；全部引用、顺序、唯一 ID、所有权及无 `r` 事件均通过。
- 增加 `validate_finished_chart(draft)`，只读校验事件唯一性、角色块、三层关系、完整数量和 `showNCount`；零诊断才深复制生成 `FinishedChart`，不修改或修复草稿。
- 校验测试覆盖两份真实导入草稿，以及空草稿、重复 ID/时间行、孤立内容、块不匹配、舞步数量和 Showtime 值错误，验证诊断、快照所有权与输入不变。
- 收紧 `FinishedChart` 边界：调用方不能直接构造快照，校验器进一步要求 `SHOW-C/SHOW-D` 恰好被关系拥有、舞步演练最多被一个 Showtime 引用、无归属舞步演练必须位于全部 Showtime 事件之后，并按 Showtime/舞步演练/演练组/NPC/玩家/Showtime/末尾无归属舞步展平后的事件位置严格递增，并拒绝展示范围整数溢出。
- 增加 `export_finished_chart_to_slk(FinishedChart)`，在 `av_core` 内编译固定 `z3116_lv5.slk` 的 2686 字节规范头，不在运行时读取 `example/`，只替换 `B`/`NN` 行数范围并按原始字节保留乱码标题、格式记录、列宽、筛选、CRLF 和结尾。
- 增加 `project_chart_markers` 三列统一投影及前一个、前一个本列、后一个标记查询；底层容器乱序不改变结果，当前位置严格排除。
- 增加 `project_showtime_moves` 和 `chart_note_ordinal`，从权威 `showNCount`/事件类型派生展示范围、连续序列首尾状态及 NPC/玩家独立 N 编号，不持久化派生值。
- 增加 `ChartEditor`，统一执行三列单行占用、NPC-C/N/S 放置转换删除、玩家相对间隔自动生成、Showtime 来源收集/位置/排布/范围冲突和所有失效事件引用清理。
- 每次成功上下文点击、整批生成、整段删除或清空只提交一份完整 `ChartDraft` 历史；失败不修改草稿或历史，撤销/重做逐值恢复全部事件与关系，新编辑清空重做分支。
- 玩家与 Showtime 自动生成结果在提交后脱离来源同步；NPC 后续修改只更新当前原生舞步的 N 总数，既有玩家位置/类型和 `SHOW-D.showNCount` 均保持生成时快照。
- 增加 `encode_formal_draft`/`decode_formal_draft`，用内部 `OTGYPRJ\0` version 1 小端信封、显式类型/长度和 CRC32 保存完整 `ChartDraft` 及 BPM/Offset 原始 binary64 位模式。
- 增加 `encode_recovery_snapshot`/`decode_recovery_snapshot`，复用相同容器安全与完整性校验但只保存 `ChartDraft`；不完整草稿无需通过成品校验即可精确往返，历史、音频和视图状态不进入负载。
- 原生负载测试固定空正式/恢复负载的规范头、长度和 CRC 字节，并覆盖两份真实 SLK 完整关系、不完整编辑草稿、隐藏精度、类型错配、版本/长度/CRC、非法枚举、超大计数、截断和尾随数据。
- 导出器从显式角色和三层关系唯一生成 NPC 后玩家的 `c/n/s`、Showtime `c0/d`、直接及 `segmentNCount * 100 + moveNCount` 拆分值，并通过共享位置换算写六列；末尾无 Showtime 舞步按行顺序生成普通阶段，且不生成 `[r & 0]`；最后一段为 Showtime 阶段时边界为 `max(1920, lastShowD.rangeEnd)`。
- 导出测试覆盖规范文件导入、投影、校验再导出的 30340 字节逐字节相同，兼容文件使用规范外壳后的完整原生语义往返，直接/拆分 `c` 值、超长边界、CRLF/BOM/结尾，以及工作表末行 `161/162/313/314/>314` 时两个格式锚点和 `B`/`NN` 范围字段。
- 把 `.onsets`、`.bpm_candidates`、`.offsets`、`.sync_candidates` 明确标记为 Git binary，禁止跨平台检出时的换行或文本规范化。
- 对末尾图块的四舍五入索引显式限制到最后一个有效 PCM 帧，消除参考代码的越界未定义行为；负数幅值缩放显式匹配算术右移结果。
- 增加四份 1x、四份 2x、四份 3x 与四份 4x、共十六份 65536 字节 CC0 Alpha 黄金夹具，固定 UI Zoom `11`、Scale `4`、block `1` 的左右声道逐字节结果及 SHA-256；每个新组合都重新逐字节验证全部既有输出，并覆盖均匀亮度 2x 的 `0/63/127/191/255`、均匀亮度 3x 的 `0/28/56/85/113/141/170/198/226/255`、均匀亮度 4x 的 `0/15/31/47/63/79/95/111/127/143/159/175/191/207/223/239/255`、振幅亮度 2x/3x/4x 的超采样行亮度与整数平均、1x 每行单一振幅 Alpha、中心对称、形状/亮度/采样差异、末帧限制和最小 Scale 下的 `247/239` 亮度边界。
- 增加 `calculate_chart_timeline_range` 模型入口，在 `av_core` 内统一投影并扫描全部谱面事件与 `SHOW-D.rangeEnd`；C ABI 只传入 `ChartDraft` 句柄，不重复领域扫描。
- 增加 `native/av_c_api` 动态库目标 `otgy_c_api`，ABI 与共享库主版本为 `2`；公共头只使用 C 类型，Windows 与其他平台均显式控制导出可见性。
- C ABI 使用统一状态码、稳定领域/领域内错误、拥有型 `otgy_error`、显式 `uint64_t` 长度、不透明拥有句柄和匹配销毁函数；全部有状态入口封闭 C++ 异常，失败不发布部分拥有结果。
- ABI 覆盖 PCM、MP3/Ogg 解码、哈希、Offset、文件名、品质、不可变 Ogg 导出、十六种波形、起音/BPM/Offset/联合分析、六个时序换算、网格、谱面模型时间线范围、导航、播放、操作门控、谱面编辑与历史、校验、项目负载、SLK 导入/导出和位置换算。
- 增加拥有型扁平谱面语义快照，逐项保存显式角色事件、演练组、舞步演练、Showtime、`showNCount` 和引用切片；不从 SLK 行、列位置或简称重建语义。
- 增加纯 C 头链接测试、音频一致性测试和谱面一致性测试；ABI v2 公共头预处理后的 191 个函数声明与本机动态库 191 个 `otgy_*` 导出符号逐项一致，其中包含宏展开的 16 个波形入口、播放 PCM/按帧推进、四个音频导出取消句柄入口及多选与粘贴预览拥有句柄入口。
- 波形过滤和未固定契约的额外 CLI 命令不属于阶段 8；当前产品工作流、播放 PCM/设备输出、平台草稿索引、原子文件发布及 Flutter 四平台适配均已实现。

## 工具链

- CMake：`3.27.7`
- C/C++ 编译器：Xcode Apple Clang `21.0.0`，arm64 macOS
- Ninja：未安装，CMake 使用默认生成器
- Flutter：`3.47.2`，Dart `3.13.2`
- Android SDK：`35/36`，NDK `28.2.13676358`；本机 Debug APK 已完成构建
- Apple SDK：Xcode `26.6`（macOS SDK `26.5`），CocoaPods `1.17.0`
- Windows：Windows 11 x86-64，Visual Studio `2026`（MSVC `19.52`，工具集 `v145`），Windows SDK `10.0.26100`，CMake `4.3.2`（`Visual Studio 18 2026` 生成器）
- Git：已初始化，默认分支为 `main`

## 修改文件

- `.github/workflows/stage8-platforms.yml`
- `app/`（Flutter UI、应用协调、私有存储、Dart FFI 构建钩子与绑定、四平台 Runner、测试）
- `.gitignore`
- `.gitattributes`
- `LICENSE`
- `ARCHITECTURE.md`
- `TESTING.md`
- `IMPLEMENTATION_PLAN.md`
- `IMPLEMENTATION_STATUS.md`
- `native/CMakeLists.txt`
- `native/av_c_api/`
- `native/third_party/CMakeLists.txt`
- `native/third_party/README.md`
- `native/third_party/libmad/`
- `native/third_party/libogg/`
- `native/third_party/libvorbis/`
- `native/third_party/tests/`
- `native/av_core/CMakeLists.txt`
- `native/av_core/include/av_core/audio/mp3_decoder.hpp`
- `native/av_core/include/av_core/audio/ogg_vorbis_decoder.hpp`
- `native/av_core/include/av_core/audio/audio_export.hpp`
- `native/av_core/include/av_core/audio/audio_export_name.hpp`
- `native/av_core/include/av_core/audio/audio_export_quality.hpp`
- `native/av_core/include/av_core/audio/audio_source_info.hpp`
- `native/av_core/include/av_core/audio/ogg_vorbis_encoder.hpp`
- `native/av_core/include/av_core/audio/pcm16.hpp`
- `native/av_core/include/av_core/audio/pcm16_hash.hpp`
- `native/av_core/include/av_core/audio/pcm_offset.hpp`
- `native/av_core/include/av_core/coordination/operation_gate.hpp`
- `native/av_core/include/av_core/timing/bpm_detector.hpp`
- `native/av_core/include/av_core/playback/playback_position.hpp`
- `native/av_core/include/av_core/playback/playback_pcm.hpp`
- `native/av_core/include/av_core/slk/slk_position.hpp`
- `native/av_core/include/av_core/slk/slk_parser.hpp`
- `native/av_core/include/av_core/slk/slk_drill_group_analyzer.hpp`
- `native/av_core/include/av_core/slk/slk_dance_move_drill_analyzer.hpp`
- `native/av_core/include/av_core/slk/slk_showtime_analyzer.hpp`
- `native/av_core/include/av_core/slk/slk_import.hpp`
- `native/av_core/include/av_core/slk/slk_export.hpp`
- `native/av_core/include/av_core/slk/slk_chart_draft_projection.hpp`
- `native/av_core/include/av_core/chart/chart_draft.hpp`
- `native/av_core/include/av_core/chart/chart_editor.hpp`
- `native/av_core/include/av_core/chart/chart_projection.hpp`
- `native/av_core/include/av_core/chart/chart_validator.hpp`
- `native/av_core/include/av_core/project/project_serialization.hpp`
- `native/av_core/include/av_core/timing/onset_detector.hpp`
- `native/av_core/include/av_core/timing/offset_detector.hpp`
- `native/av_core/include/av_core/timing/sync_analyzer.hpp`
- `native/av_core/include/av_core/timing/timing_conversion.hpp`
- `native/av_core/include/av_core/timing/timing_grid.hpp`
- `native/av_core/include/av_core/timing/timeline_range.hpp`
- `native/av_core/include/av_core/timing/timeline_navigation.hpp`
- `native/av_core/include/av_core/waveform/waveform_tile.hpp`
- `native/av_core/include/av_core/version.hpp`
- `native/av_core/src/audio/mp3_decoder.cpp`
- `native/av_core/src/audio/ogg_vorbis_decoder.cpp`
- `native/av_core/src/audio/audio_export.cpp`
- `native/av_core/src/audio/audio_export_name.cpp`
- `native/av_core/src/audio/audio_export_quality.cpp`
- `native/av_core/src/audio/ogg_vorbis_encoder.cpp`
- `native/av_core/src/audio/pcm16.cpp`
- `native/av_core/src/audio/pcm16_hash.cpp`
- `native/av_core/src/audio/pcm_offset.cpp`
- `native/av_core/src/coordination/operation_gate.cpp`
- `native/av_core/src/playback/playback_position.cpp`
- `native/av_core/src/playback/playback_pcm.cpp`
- `native/av_core/src/slk/slk_position.cpp`
- `native/av_core/src/slk/slk_parser.cpp`
- `native/av_core/src/slk/slk_drill_group_analyzer.cpp`
- `native/av_core/src/slk/slk_dance_move_drill_analyzer.cpp`
- `native/av_core/src/slk/slk_showtime_analyzer.cpp`
- `native/av_core/src/slk/slk_import.cpp`
- `native/av_core/src/slk/slk_export.cpp`
- `native/av_core/src/slk/slk_chart_draft_projection.cpp`
- `native/av_core/src/chart/chart_draft.cpp`
- `native/av_core/src/chart/chart_editor.cpp`
- `native/av_core/src/chart/chart_projection.cpp`
- `native/av_core/src/chart/chart_validator.cpp`
- `native/av_core/src/project/project_serialization.cpp`
- `native/av_core/src/timing/arrowvortex/`
- `native/av_core/src/timing/bpm_detector.cpp`
- `native/av_core/src/timing/bpm_detector_internal.hpp`
- `native/av_core/src/timing/onset_detector.cpp`
- `native/av_core/src/timing/onset_detector_internal.hpp`
- `native/av_core/src/timing/offset_detector.cpp`
- `native/av_core/src/timing/offset_detector_internal.hpp`
- `native/av_core/src/timing/pcm_mix.hpp`
- `native/av_core/src/timing/sync_analyzer.cpp`
- `native/av_core/src/timing/tempo_constants.hpp`
- `native/av_core/src/timing/tempo_gap.hpp`
- `native/av_core/src/timing/timing_conversion.cpp`
- `native/av_core/src/timing/timing_grid.cpp`
- `native/av_core/src/timing/timeline_range.cpp`
- `native/av_core/src/timing/timeline_navigation.cpp`
- `native/av_core/src/waveform/waveform_tile.cpp`
- `native/av_core/src/version.cpp`
- `native/av_core/tests/CMakeLists.txt`
- `native/av_core/tests/audio_export_name_test.cpp`
- `native/av_core/tests/audio_export_quality_test.cpp`
- `native/av_core/tests/audio_export_test.cpp`
- `native/av_core/tests/bpm_detector_test.cpp`
- `native/av_core/tests/fixtures/bpm/`
- `native/av_core/tests/fixtures/mp3/`
- `native/av_core/tests/fixtures/ogg_vorbis/`
- `native/av_core/tests/fixtures/ogg_export/`
- `native/av_core/tests/fixtures/onset/`
- `native/av_core/tests/fixtures/offset/`
- `native/av_core/tests/fixtures/sync/`
- `native/av_core/tests/fixtures/waveform/`
- `native/av_core/tests/mp3_decoder_test.cpp`
- `native/av_core/tests/ogg_vorbis_decoder_test.cpp`
- `native/av_core/tests/ogg_vorbis_encoder_test.cpp`
- `native/av_core/tests/operation_gate_test.cpp`
- `native/av_core/tests/pcm_offset_test.cpp`
- `native/av_core/tests/onset_detector_test.cpp`
- `native/av_core/tests/offset_detector_test.cpp`
- `native/av_core/tests/pcm16_test.cpp`
- `native/av_core/tests/playback_position_test.cpp`
- `native/av_core/tests/playback_pcm_test.cpp`
- `native/av_core/tests/slk_position_test.cpp`
- `native/av_core/tests/slk_parser_test.cpp`
- `native/av_core/tests/slk_drill_group_analyzer_test.cpp`
- `native/av_core/tests/slk_dance_move_drill_analyzer_test.cpp`
- `native/av_core/tests/slk_showtime_analyzer_test.cpp`
- `native/av_core/tests/slk_import_test.cpp`
- `native/av_core/tests/slk_export_test.cpp`
- `native/av_core/tests/slk_chart_draft_projection_test.cpp`
- `native/av_core/tests/chart_validator_test.cpp`
- `native/av_core/tests/chart_projection_test.cpp`
- `native/av_core/tests/chart_editor_test.cpp`
- `native/av_core/tests/project_serialization_test.cpp`
- `native/av_core/tests/sync_analyzer_test.cpp`
- `native/av_core/tests/timing_conversion_test.cpp`
- `native/av_core/tests/timing_grid_test.cpp`
- `native/av_core/tests/timeline_range_test.cpp`
- `native/av_core/tests/timeline_navigation_test.cpp`
- `native/av_core/tests/version_test.cpp`
- `native/av_core/tests/waveform_tile_test.cpp`
- `native/tools/avtool/CMakeLists.txt`
- `native/tools/avtool/main.cpp`
- `native/tools/avtool/tests/`

## 最后成功验证

### 2026-11-15：Windows 原生构建、实机冒烟与两处编辑缺陷修复

Windows 版本此前从未在真实硬件上构建或运行。本轮在独立分支上补齐 MSVC 可移植性并在实机完成验证。可移植性改动只影响构建与测试入口，不改变领域行为：

- `native/av_core/src/timing/arrowvortex/FindOnsets.cpp`：`static char*` 字符串字面量改为 `char[]`，满足 `/permissive-`。
- `native/av_c_api/src/audio.cpp`：补 `<cmath>`；MSVC 的 `std::isfinite` 由该头提供。
- `native/CMakeLists.txt`：MSVC 追加 `/utf-8`，窄字符串字面量与源文件按 UTF-8 解释。
- 8 个 SLK 测试：源路径改用 `std::filesystem::u8path`，Windows 不再按 ANSI 代码页解释窄字符路径。
- `native/tools/avtool/tests/*.cmake`：`execute_process` 追加 `ENCODING UTF8`；`decode_errors.cmake` 的正则按 CMake 4 规则转义 `[`。
- `native/av_c_api/tests/CMakeLists.txt`：Windows 上把 `otgy_c_api.dll` 复制到三个 C ABI 测试的可执行文件目录；此前它们以 `0xC0000135` 退出。
- `app/packages/otgy_native/hook/link.dart`：Windows 不再追加 `-x none` 与 `std=c++17`，并避免 `native_toolchain_c` 给原生产物加 `/TP`；否则 `.lib` 归档会被当作 C++ 源文件。
- `native/av_core/tests/chart_editor_allocation_failure_test.cpp`：MSVC Debug 的 STL 容器代理会在 `noexcept` 构造内分配两个指针大小的块，注入失败会直接 `std::terminate`；该尺寸不再注入。

实机结果（Windows 11、MSVC `19.52`、Windows SDK `10.0.26100`）：

```text
cmake --build build --parallel
ctest --test-dir build --output-on-failure
flutter build windows --release
```

原生完整套件 `57/59` 通过。未通过的两项是 Ogg 黄金测试：32768 个字节中 26 个相差 1 个最低有效位。是否为此引入平台容差尚未决定，本轮不放宽断言。成品 SLK 导入→导出在 Windows 上对真实成品谱面逐字节一致。

App Release 在实机通过启动、音频与 SLK 导入（含中文路径）、播放/暂停/末尾重播、编辑、选择、撤销/重做、保存与另存为、同名覆盖提示和未保存关闭确认。

本轮修复并实机验证的两处缺陷：

- 音乐末尾无法重新播放：`playbackStartEnabled` 在光标到达音乐结尾时禁用播放按钮，前两处回卷修复因此不可达。现在末尾保持可用，播放时回卷到开头（实测 `00:10.031 → 00:00.947`）。
- 同名草稿不提示覆盖：检查把当前草稿排除在外。现在任何同名草稿都提示，并以已有草稿标识为目标（只保留一份同名草稿）。

键盘结论：点击画布后焦点落在画布的 `FocusNode` 上，快捷键绑定在 widget 测试中通过；实机注入的 SPACE 事件携带扫描码但 `logicalKey` 与 `physicalKey` 为 `null`，因此扫描码注入无法触发 `Shortcuts`。真实键盘路径本轮没有在实机按下，键盘仍不记为已实机验证；`Ctrl+Z` 曾以虚拟键注入但没有探针日志，同样不作为已验证结论。

测试侧同步纠正两处与既有规格不符的断言：`otgy_native_test` 的 ABI 版本断言从 `2` 更新到当前 `5`；`chart_selection_test` 的“首个已记录舞步之前新增完整舞步”用例改为断言 Showtime 随该次编辑失效并可一次撤销恢复，与 `FUNCTIONAL_SPEC.md` 的“Showtime 内容失效”判据和 `chart_editor_test.cpp` 用例 (e) 一致。

本轮在主机上执行：原生 CMake/CTest 完整 `59/59`、Dart FFI 完整 `29/29`、Flutter 完整 `256/256`。

### 2026-10-15：Showtime 失效判定的引用归属

`showtime_is_stale` 原先按位置把登记舞步与登记 `SHOW-D` 一一配对比较，并在登记舞步数量之外的 `SHOW-D` 上直接跳过。这与规格不符：`SHOW-D` 自身保存舞步引用，`showNCount` 只能与它自己指向的舞步的 `moveNCount` 比较；已登记的 `SHOW-D` 事件缺失同样应判为失效。现在分两轮独立检查——先按登记舞步逐项要求存在且仍是完整舞步（导入的 `SHOW-D` 少于舞步仍合法），再按登记 `SHOW-D` 逐项要求事件存在、指向舞步存在且完整、`showNCount` 等于该舞步的 `moveNCount`。核心、C ABI、Dart FFI 与 Flutter 仍调用同一处实现，未新增第二处判定。当前剪切准备不触发删除或清理；首次成功落点时，被引用舞步的删除、失效 Showtime 清理与目标插入共用一条历史，撤销一次同时恢复。

本次实际执行：

```text
cmake --build build/selection-native --parallel 4
ctest --test-dir build/selection-native --output-on-failure

cd app/packages/otgy_native
../../../build/flutter-sdk/bin/dart analyze
../../../build/flutter-sdk/bin/dart test test/chart_selection_test.dart -r expanded

cd ../..
../build/flutter-sdk/bin/flutter analyze --no-pub
../build/flutter-sdk/bin/flutter test --no-pub --reporter expanded

cd ..
git diff --check
```

结果：原生完整黄金与一致性套件 `50/50` 通过；Dart FFI 分析无问题、舞步选择与剪贴板测试 `9/9` 通过；Flutter 分析无问题、默认配置完整回归 `164/164` 通过；`git diff --check` 无空白问题。Dart 原生资产构建钩子输出了 clang 的未使用链接参数警告（`-lc++`、`-Wl,-encryptable`），未导致测试失败，不将这次验证描述为全部构建无警告。

两个新用例是真实回归测试：把 `native/av_core/src/chart/chart_editor.cpp` 还原到 `HEAD` 版本后，用例 (g) 断言失败于 `chart_editor_test.cpp:1488`，只保留用例 (h) 时失败于 `chart_editor_test.cpp:1489`；恢复修复后两者通过。用例 (g) 构造“`SHOW-D` 指向第二个舞步、计数仍取自第一个舞步”的草稿，用例 (h) 构造“登记的 `SHOW-D` 已不存在且落在登记舞步数量之外”的草稿。`test_paste_failures_preserve_state` 同时补上三项失败保护断言：非空重做分支在失败后仍可重放并还原同一草稿、失败后剪贴板内容仍能粘出与未失败对照编辑器逐值相同的结果、在已分配部分新标识后才发现标识耗尽时草稿/历史/剪贴板/标识游标全不变且下一条成功命令分配到的标识与对照编辑器逐值相同。

验证边界：本轮没有找到能由正常界面操作生成这两个畸形状态的复现序列；两个新用例按既有先例直接构造草稿，用于验证提交边界的防御性清理，不据此断言已穷尽所有可达路径。C ABI 层只转发同一条核心命令，这一轮失效判定修复未新增 ABI 专用用例，由核心用例与 `av_c_api.chart_consistency` 的既有逐值对照覆盖。本轮为本地主机单元与 FFI/Widget 回归，没有构建发布包、安装或操作设备，也没有改动已保存的设备草稿。

### 2026-10-14：离线本地诊断与验收构建入口

已接通 `OTGY_DIAGNOSTICS` 编译期开关、本地有界 JSONL、构建身份、编辑命令与结果、保存恢复及异常日志，以及普通/验收构建共有的“导出诊断包（含当前谱面）”。主动导出使用共享核心恢复编码的原始字节，不调用成品校验或 SLK 导出；日志不包含谱面负载、音频、文件/草稿名或剪贴板内容。异常不读取 message/details，不 stringify 未知对象；历史异常状态按类型与字段白名单再次过滤。移动端在 finish 返回后与 commit 前检查取消，失败先尝试未提交文档清理，再显示原有可复制错误弹窗。

已通过的实际命令：

```text
cd app
../build/flutter-sdk/bin/dart format test/diagnostics_export_test.dart
../build/flutter-sdk/bin/flutter --suppress-analytics --no-version-check analyze --no-pub
../build/flutter-sdk/bin/flutter --suppress-analytics --no-version-check test --no-pub --reporter expanded
../build/flutter-sdk/bin/flutter --suppress-analytics --no-version-check test --no-pub --dart-define=OTGY_DIAGNOSTICS=true --reporter expanded test/local_diagnostics_test.dart test/main_diagnostics_test.dart test/editor_diagnostics_test.dart test/diagnostics_export_test.dart test/diagnostics_lifecycle_test.dart test/audio_export_publisher_test.dart

cd ..
python3 -B scripts/test_build_acceptance.py
python3 -B -c "from pathlib import Path; paths = sorted(Path('scripts').glob('*.py')); [compile(p.read_bytes(), str(p), 'exec') for p in paths]; print(f'Python syntax OK: {len(paths)} files')"
git diff --check
```

结果：最终格式检查 `1` 文件、`0` 变化（此前全部 `12` 个诊断相关 Dart 文件格式检查也为 `0` 变化）；Flutter 静态检查无问题；默认配置完整回归 `164/164`、开启诊断的六文件定向回归 `76/76` 均通过。成功、取消、捕获失败、选择器失败、写入失败、提交失败与 finish 等待期间 detached 均验证工作区状态保护；剪贴板不只检查可粘贴，还在卸载界面隔离恢复监听后，与独立同源会话实际粘贴，并逐字节比较正式草稿及恢复编码。JSON 冲突不覆盖原文件且保持 `.json` 扩展名的普通发布与直接写入回退测试均通过。历史首轮三项导出等待失败在最终完整回归中未再出现，未确认其根因，不将其归为已证实的生产缺陷。

验收脚本测试 `5/5` 通过，包含 APK/macOS/Windows/iOS 的真实 CLI dry-run、版本读取、构建身份和参数覆盖拒绝；两个 Python 文件内存语法编译及 `git diff --check` 通过。dry-run 使用不存在的 Flutter 路径，未启动打包或安装。

验证边界：本轮为本地主机单元/Widget、原生 FFI 与模拟平台通道回归，不代替四平台实际发布验收。本轮没有构建新的诊断 Release APK/App，没有安装或操作设备，没有修改已保存的设备草稿；下面的既有发布包及设备结果不是本轮诊断验收结果。

### 2026-10-14：空演练组清理与 Showtime 再生成

核心在候选草稿与提交边界按角色事件的 `drill_group_id` 删除零绑定事件的演练组，并移除舞步中的组引用，再判断 Showtime 是否失效。删除一组 NPC 后仍保留该组玩家内容；删除其最后一个角色事件后，剩余完整组可以再次生成 `SHOW-D`。存活事件的组即使关系向量为空仍保留，加载、只读查询和失败命令不修改旧快照，撤销/重做精确恢复完整关系。

本次实际执行：

```text
cmake --build build/selection-native --parallel 4
ctest --test-dir build/selection-native -R 'av_core.chart_editor|av_c_api.chart_consistency' --output-on-failure
ctest --test-dir build/selection-native --output-on-failure

cd app/packages/otgy_native
../../../build/flutter-sdk/bin/dart format test/chart_selection_test.dart
../../../build/flutter-sdk/bin/dart analyze
../../../build/flutter-sdk/bin/dart test -r expanded

cd ../..
../build/flutter-sdk/bin/flutter analyze
../build/flutter-sdk/bin/flutter test test/chart_selection_test.dart --reporter expanded

git diff --check
```

结果：原生定向 `2/2`、完整黄金与一致性套件 `50/50` 通过；Dart FFI 分析无问题、完整测试 `16/16` 通过；Flutter 分析无问题、定向交互测试 `7/7` 通过。新增 C ABI 用例逐值比较删除最后组事件、单步撤销/重做与再生成后的完整核心语义；新增 Dart FFI 用例精确比较恢复快照字节，并确认再生成 `SHOW-D` 位于行 `17`、`showNCount` 为 `1`。

独立 Android 集成回归任务 `bmsj7dmuj` 于 2026-10-14 02:09:58.936 UTC 在 `emulator-5556` 启动，三项测试 `3/3` 通过并以退出码 `0` 完成，完成通知时间为 02:10:27.237 UTC；测试覆盖移动端长按与舞步剪贴板、删除最后组事件后的 Showtime 再生成，以及剪切时同一撤销步骤清理失效 Showtime。任务 `brcciq43y` 从仓库根目录误运行 `flutter test`，因找不到 `pubspec.yaml` 以退出码 `1` 结束且测试未开始，不计入验证结果。历史界面的 `engine-7`/`engine-8` 只是描述匹配，缺少直接 alias 字段；因此以原始任务 ID 记录，不声称短标签存在正式映射。

现场数据保留：`build/showtime-user-state.tar` 为 `4608` 字节，SHA-256 为 `86f89d6e5c87f19bba916658b5d4ad2a98e5680c17fda538eb05f7f70047f365`。归档中的恢复记录为 `1266` 字节，时间戳 `2026-10-14T01:37:09.891137Z`，SHA-256 为 `e53dbd3386d6932f4c695d9666d0d4828469f05c1d151be0ba7487de0e992cac`。2026-10-14 实际通过 `adb -s emulator-5554 shell run-as ... ls -l files/OneTwoGoYeah` 与 `exec-out run-as ... dd if=... status=none` 只读核对，当前目录仍只有该恢复记录与 `active-session`，两个文件与归档成员均逐字节一致。本次未安装、卸载、清空、恢复或改写原模拟器数据。独立的 `build/showtime-user-recovery.otgydata` 尾部含 `dd` 统计文本，现场分析只使用归档内干净成员。

归档内恢复 payload 经现有 `decode_recovery_snapshot` 解码后确认：舞步 `6` 登记组 `[5,13]`，其中组 `5` 没有任何绑定角色事件，组 `13` 是完整的 `moveNCount=1` 舞步；另一舞步 `46` 仍是不完整草稿。旧提交 `73064496e27bca7a9abd9357c925d258f424a9ff` 的编辑器对同一快照在 Showtime 行 `17` 和 `32` 均返回 `NoRemainingDanceMoves(9)`，保留 SHOW-C 且不生成 SHOW-D。当前核心对每次独立加载的同一快照，在合法位置删除空组 `5`、把舞步 `6` 的组列表改为 `[13]`，并生成 `showNCount=1` 的 SHOW-D；全部角色事件保持不变，清理与生成共用一条历史，撤销后草稿及 `884` 字节 payload 与原始输入逐字节相同，重做也精确恢复。非法行 `18` 仍返回 `InvalidShowtimePosition(8)`，不清理、不产生历史。这证明保存现场确实包含本修复处理的阻塞状态，且修复可在不丢失用户内容的前提下解除阻塞；恢复记录没有编辑历史或 APK 构建标识，因此不推断空组由哪次具体操作产生，也不声称完整重建了现场 APK。

### 既有舞步选择验证记录

以下是 2026-10-13 多选变更前的验证证据，不代表当前多选、延迟剪切和点击落点流程已经通过设备验收。当前交互契约以 `FUNCTIONAL_SPEC.md` 的“选择模式与舞步剪贴板”为准。

当时在 arm64 macOS 和 Android API 34 模拟器 `emulator-5554` 上运行：

```text
cmake -S native -B build/selection-native -DBUILD_TESTING=ON
cmake --build build/selection-native --parallel
ctest --test-dir build/selection-native --output-on-failure

cd app/packages/otgy_native
../../../build/flutter-sdk/bin/dart analyze
../../../build/flutter-sdk/bin/dart test -r expanded

cd ../..
../build/flutter-sdk/bin/flutter analyze
../build/flutter-sdk/bin/flutter test --reporter expanded
../build/flutter-sdk/bin/flutter test integration_test/chart_selection_test.dart -d emulator-5554 --reporter expanded
```

结果：原生核心、CLI 和 C ABI `50/50` 通过；Dart FFI 分析通过且 `15/15` 测试通过；Flutter 分析通过且 `99/99` 测试通过；Android 真实 App 与原生 FFI 集成测试 `2/2` 通过，覆盖长按选择、模式内点击、复制、重复粘贴、新标识、剪切、单步撤销、占用失败、退出清空剪贴板和 Showtime 同步清理及恢复。最终 Flutter 回归还覆盖工具栏 Tab/Enter 焦点路径、文本剪贴板及无障碍标签。

正常入口 `lib/main.dart` 的 Android Debug APK 已重新构建、安装并启动，产物为 `app/build/app/outputs/flutter-apk/app-debug.apk`。实际执行 `flutter build apk --debug -t lib/main.dart`、`adb -s emulator-5554 install -r build/app/outputs/flutter-apk/app-debug.apk`、`adb -s emulator-5554 shell am force-stop io.onetwogoyeah.one_two_go_yeah` 和 `adb -s emulator-5554 shell am start -n io.onetwogoyeah.one_two_go_yeah/.MainActivity`，均成功。

本轮 Android 三 ABI 原生资产检查未完成：首次执行未找到 `readelf`，后续检查命令未获执行许可；设备集成测试通过不能代替该资产检查。Windows、macOS 和 iOS 的本轮安装运行验证未执行。

### 既有发布验证记录

以下是 2026-09-05 的结果，未在本轮重新验证：

```text
cmake -S native -B build/stage9-native -DBUILD_TESTING=ON
cmake --build build/stage9-native --parallel
ctest --test-dir build/stage9-native --output-on-failure

cd app/packages/otgy_native
../../../build/flutter-sdk/bin/dart analyze
../../../build/flutter-sdk/bin/dart test -r expanded

cd ../..
../build/flutter-sdk/bin/flutter analyze
../build/flutter-sdk/bin/flutter test -r expanded
../build/flutter-sdk/bin/flutter build apk --release
../build/flutter-sdk/bin/flutter build macos --release
../build/flutter-sdk/bin/flutter build ios --release --no-codesign
tool/verify_android_native_asset.sh app/build/app/outputs/flutter-apk/app-release.apk
codesign --verify --deep --strict build/macos/Build/Products/Release/OneTwoGoYeah.app
unzip -t build/releases/OneTwoGoYeah-1.0.0+1-android-bundle.zip
unzip -t build/releases/OneTwoGoYeah-1.0.0+1-macos-universal-bundle.zip

swiftc -parse ios/Runner/AppDelegate.swift
swiftc -parse macos/Runner/MainFlutterWindow.swift
```

结果：原生核心、CLI 和 C ABI `50/50` 通过；Dart FFI 分析通过且 `7/7`
测试通过；Flutter 分析通过且 `92/92` 测试通过。Android Release APK、macOS
Release Universal App 和 iOS 真机目标 Release 无签名 App 均构建成功；Android
三 ABI 原生资产检查、macOS Ad Hoc 签名校验和两个发布 ZIP 完整性检查通过。
`flutter build ios --simulator --release` 按 Flutter 约束拒绝执行（simulator
Release 不受支持），不作为产品构建失败。当前机器没有 Windows SDK、Android 真机
或已签名 iOS 真机，因此这些环境仍待阶段 9 后续验证。

## 已知问题

- 示例 MP3/Ogg 的再分发许可尚未确认，已通过 `.gitignore` 排除；两个 SLK 继续保留为本地规范与测试参考。
- Android Release 当前使用本地 Debug signing key；macOS Release 为本机 Ad Hoc 签名。对外分发前必须由发布者在目标环境配置自己的 Android release keystore、Developer ID 签名和 Apple notarization，且不能把密钥或密码提交到仓库。
- 主机不是 Windows 环境，也没有已连接的 Android 真机或已签名 iOS 真机。Windows x86-64 已在一台真实主机完成原生与 App 构建、运行冒烟；Windows ARM64、Android 真机/模拟器 Release 冒烟和 iOS 真机安装运行仍需在对应设备或 CI 完成。
- Windows 上两项 Ogg 黄金测试未通过：32768 个字节中 26 个相差 1 个最低有效位。成因尚未定位，当前不放宽断言；是否为此引入明确的平台容差、或调整固定 libVorbis 在该编译器的浮点路径，待决定。
- Flutter 不支持 iOS simulator 的 Release 构建；模拟器验证应使用 Debug/Profile 构建，不能把 `flutter build ios --simulator --release` 作为发布产物。
- 对固定的 libmad/libogg/libVorbis 旧版 C 快照执行全量 UndefinedBehaviorSanitizer 时，会在解码/编码路径报告负下标式指针访问、空指针零偏移和有符号左移诊断，包括 `libogg/framing.c`、`bitwise.c` 以及 libVorbis `sharedbook.c`、`psy.c`、`floor1.c`。普通构建与全量 AddressSanitizer 仍精确通过全部黄金结果；未用测试过滤或更宽容断言掩盖这些诊断。阶段 7 定向组合验证只对新增/修改的自有 C++ 路径启用 UndefinedBehaviorSanitizer，对固定第三方 C 快照保留 AddressSanitizer，并以 `halt_on_error=1` 通过 4/4。
- 移动端固定判定线锚点受时间线末尾限制。`calculate_timeline_range()` 始终以四小节基础范围为基础取最晚者（`native/av_core/src/timing/timeline_range.cpp:13`），时间光标被限制在 `endRow` 以内（`app/lib/src/application/editor_controller.dart:1348`），而目标闭区间 `[锚点行, 锚点行 + 最大偏移]` 必须三列全空。因此在没有音频、末尾已有内容的项目里，把批次粘贴到时间线末尾后末尾行被该批次占用，判定线无法再移到末尾之后，无法在同一末尾继续追加；需要加载音频或存在更晚标记/`SHOW-D` 才能继续。本轮用一次性 Dart 诊断（已删除）在真实 `NativeChartEditor` 上测得：无音频空项目 `endRow = 64.0`（四小节基础范围）；在锚点 `64` 首次粘贴成功并提交一次修订，`endRow` 增长到 `78.0`；此后 `moveToRow(1000)` 被限制为 `78.0`；对可达末尾区间 `72/74/75/76/77/78` 逐行调用 `updatePastePreview()` 全部返回 `PasteRangeOccupied`（`19`，“粘贴范围内存在其他标记”），在末尾再调用 `pasteAtRow()` 修订数不变、返回同一稳定错误。该限制来自既有 `FUNCTIONAL_SPEC.md` 条款，本轮未改动这些契约，是否允许锚点越过末尾待产品决定。

## 当前工作与待验证项

2026-11-15 macOS 桌面快捷键实机验证：在 macOS Release 构建上用真实窗口逐项回放 `F1`、`Home/End`、`Page Down`、`Shift+Page Down`、`1/2/3`（含自动前进）、`5`、`Command+A/C/V`、撤销/重做、`Command 0`、`Command+小键盘 +/-`、`Command+Shift+S`，并通过诊断日志核对 `placeMarker`、`click`、`selectAll`、`copy`、`paste`、`undo`、`redo` 的真实结果。验证中发现并修复 macOS 默认键位冲突：`Command+小键盘 +/-` 原先同时绑定 Zoom 和 Scale，按一次会同时改变两个值；现改为 macOS Zoom 只使用 `Command + / -`，`Command+小键盘 +/-` 只调整 Scale。新增默认键位互斥测试，遍历两端默认组合并断言 `conflictingAction` 为空。未改动 Windows 绑定。Flutter `311/311` 通过，`flutter analyze` 无问题。

2026-11-15 桌面判定层：Windows/macOS 模拟播放且未打开屏幕键盘时，编辑画布覆盖与屏幕键盘同款半透明底色，并把同一判定文字显示在编辑画布高度约 `25%`、判定线上方；该层绘制在右侧悬浮条之前且不接收指针，播放期间仍可切换波形显隐；暂停、停止、普通播放、谱面预览与节奏测试都不显示，判定开关关闭时只留底色。判定文字的颜色与字号已抽成共享 `judgement_display.dart`，屏幕键盘与桌面渲染同一组件，控制器仍独占 `320 ms` 显示窗口。新增 `desktop_judgement_layer_test.dart`，覆盖显示/清除、预览与节奏测试不显示、判定关闭只剩底色、悬浮条可点击、屏幕键盘打开时由键盘独占显示和移动端不渲染。Flutter `310/310` 通过，`flutter analyze` 无问题；本轮未改原生代码，未执行真机或平台构建验证。

2026-11-15 Windows 验收：main 已 rebase 到 `worktree-msvc-portability` 的 f5aec4e 并包含桌面快捷键提交 2989bab。在 otgy-win 上干净 Release 构建成功；Debug CTest `57/59`，仅 `av_core.ogg_vorbis_decoder/encoder` 两个已知 1 LSB 差异，`av_core.chart_editor`、分配失败注入和 `av_c_api.chart_consistency` 全部通过。诊断版 Release 注入按键确认 `placeMarker`（0/1/2）、`selectAll`、`copy`、`End`、`paste`（判定线 64）、右键清除和 `Esc` 退出真正到达核心。该机出现已知 DXGI 白屏，应用窗口内容全白，因此 F1、Ctrl+O/S、Scale 的界面结果未做视觉验收；真实键盘仍需用户亲手按一次。Visual Studio 多配置必须用 `ctest -C Debug`，`-C Release` 会因 `NDEBUG` 关闭断言而 SEGFAULT，不能作为测试结果（已记录在 TESTING.md）。

2026-11-15 桌面入口快捷键与全选已接通：会话内右键清除选择/退出，`F1` 打开使用指南，`Ctrl/Command+O` 打开草稿，`Ctrl/Command+S` 与 `Ctrl/Command+Shift+S` 保存/另存为，`Ctrl/Command+小键盘 +/-` 调整 Scale；`Ctrl/Command+A` 通过新增核心 `select_all` 全选所有可选舞步并从普通模式进入选择模式，`Ctrl/Command+Home/End` 跳到选区首尾。文件、帮助和 Scale 键与对应按钮/菜单共用门控，右键只在会话内响应。C ABI 追加 `otgy_chart_editor_select_all`，`ffigen` 已重新生成绑定。按约定，`Tab`、`Ctrl+Shift+E`、`Delete` 删除选中舞步、拖框与 `Shift/Alt` 加减选、`Shift+Home/End` 仍待确认后再实现。本轮原生 `59/59`、Flutter `300/300` 通过，`flutter analyze` 无问题；未执行真机或平台构建验证。

2026-11-15 桌面光标导航与粘贴快捷键已接通：`↑/↓` 吸附 1/16 网格、`Page Up/Down` 吸附小节、`Shift+Page Up/Down` 吸附拍、`Home/End` 跳到首尾、`Ctrl/Command+↑/↓` 跳到相邻标记；全部经现有 `moveToRow`，播放和音频导出不响应，选择/待粘贴会话内仍可导航，移动端不注册。待粘贴模式新增 `Ctrl/Command+V` 在判定线提交，成功后保持待粘贴且不移动判定线，点击目标行继续可用。帮助页扩展到二十二项可改绑操作，并在复制粘贴指南中补充快捷键落点；标记键和粘贴键忽略按键重复，方向键保留连续移动。本轮原生代码未改动；Flutter `289/289` 通过，`flutter analyze` 无问题；未执行真机或平台构建验证。

2026-11-14 桌面显式标记键已贯通核心、C ABI、Dart FFI、控制器和画布：`ChartEditor::place_marker` / `remove_marker` 提供三种 NPC 类型的显式放置、同类型单标记删除、11 个 N 与空组结束拒绝，以及无起始组的接管；`4` / `5` 复用既有 Showtime 与玩家上下文点击。C ABI 追加 `otgy_chart_editor_place_marker` / `otgy_chart_editor_remove_marker` 与结果码 `23/24`，并像点击一样输出 `player_actions_cleared`；`ffigen` 已重新生成绑定。帮助页改用用户可见名称列出五个标记键，桌面端十一项操作全部可改绑；放置成功后判定线前进一行，删除、拒绝和失败不移动。本轮原生 `59/59`、Flutter `278/278` 全部通过，`flutter analyze` 无问题；未执行真机或平台构建验证。

全平台长按进入多选、点击切换舞步、复制/剪切准备、点击画布落点和持续待粘贴流程已贯通各层。C ABI 已迁移至 v2，拥有型选择集合与只读预览经 Dart 复制为不可变值；Flutter 使用普通、选择、待粘贴三态。首次剪切落点才删除源并插入目标，成功后快照继续用于复制。Flutter 门控、键盘焦点、诊断脱敏和应用自动化交互验收已完成。

2026-10-16 本轮已执行并通过：原生 CMake 配置与完整构建；核心、黄金、CLI 和 C ABI 全套 `50/50`；Dart FFI 分析与完整测试 `19/19`；`ffigen` 重新生成绑定；Flutter 分析无问题；Flutter 全量测试 `178/178`；启用诊断的定向测试 `63/63`（`flutter test --dart-define=OTGY_DIAGNOSTICS=true` 加五个 `*diagnostic*` 测试文件）；最终源码 Android Debug 集成测试 `5/5`（连续两次运行）；最终源码 macOS Debug 集成测试 `5/5`。Android 普通入口 Debug APK 已重新构建，三 ABI 原生资产检查通过，最终 APK SHA-256 为 `c64dee169df732fe96648a47d5f6b95fa650ee744348877c1f7f68870ac5786e`。

选择模式使用精确标记行跨列命中：`ChartEditor::select(column, row)` 保留合法列检查及 `marker_at()` 的精确行查询，六类 NPC/玩家标记和 `SHOW-D` 均可从任意一列切换所属完整舞步。三列全空行及高亮跨度内无标记行不命中，`SHOW-C` 从三列输入均不可选；普通编辑仍按列返回占用冲突。行为只在 `av_core` 实现，C ABI、FFI 和 Flutter 原样透传，无公开接口、ABI 版本或生成绑定变更。

2026-10-17 跨列选择验证：原生选择及 C ABI 定向 `2/2`，完整核心/黄金/CLI/C ABI `50/50`；Dart FFI 选择定向 `11/11`、完整 `20/20`，分析无问题；Flutter 选择定向 `22/22`、完整 `180/180`，分析无问题；macOS 真实 App 集成测试 `6/6`。新增回归覆盖三列选中/取消、玩家实际标记、`SHOW-D`、`SHOW-C`、跨度内空白行、非法列、普通编辑错列占用及草稿/历史不变性；Android 与 macOS widget 布局均验证真实画布坐标路径。普通入口 macOS Debug 已使用当前脏工作树、离线诊断及构建身份 `cf460bf499386a3ccd6c9e41ab2f711928cc5f43-dirty-20261017T085409.776260Z` 构建并通过 `open -n` 打开，新进程 `39638`，原 Debug 进程 `53982` 保留。本次未运行 Android/iOS/Windows 设备验证，未提交或推送。

音频导出测试使用 `tester.runAsync` 与 `pump` 交替等待真实后台 isolate，保持进度、取消及失败传播的精确断言。会话内音频导出被拒绝，退出会话后才能开始导出；导出期间阻塞界面的实时区域与“取消导出”可访问，编辑器下层输入不可达，长按、选择和剪贴板命令均不改变草稿与历史。导出结束后可重新进入会话。

选择/待粘贴会话统一门控已接通：核心新增 `ChartInteraction` 模式、选择命令/导航视图/历史导航分类与 `ChartInteractionActive` 拒绝码，C ABI 与 Dart FFI 只转发；同宽度追加常量不改变 ABI 版本 `2`。Flutter 会话内禁用音频、项目、保存/导出、播放与音效入口，保留导航、波形设置、历史导航及会话自身操作；已打开的谱面菜单与音效面板也按同一规则禁用。分析、SLK 导出及音效控制器直调有对应守卫。状态栏时间、Zoom 与 Scale 使用有界弹性布局，覆盖窄 Mac 窗口缩放时的溢出回归。

2026-10-17 门控最终验证：原生定向 `3/3`、完整核心/黄金/CLI/C ABI `50/50`、Dart FFI 完整 `20/20`、Flutter 完整 `189/189`、启用诊断的五文件定向测试 `63/63` 全部通过；Flutter 与 Dart FFI 静态分析无问题，`git diff --check` 通过。新增白名单、控制器拒绝不变性、键盘、已打开菜单禁用与窄窗口布局回归。用户已确认 macOS Debug 门控体验验收通过，并授权提交；分析任务复用、音效已打开菜单和窄窗口布局另由自动回归覆盖。未重新运行本次门控的 macOS/Android 设备集成，避免干扰用户的验收窗口与模拟器；前述 `6/6` 是跨列选择阶段的设备结果，不作为本次门控设备验证。

`MouseRegion.onEnter` 与 `onHover` 继续复用悬停投影入口。会话内音频导出被门控、音频导出期间不能进入会话，因此原来“待粘贴中触发导出 busy 遮罩”的用例已不再属于合法 UI 路径。对应测试改为断言会话内导出被拒绝、保存回调不执行且预览保持；静止鼠标下导航和缩放后的预览重投影回归继续保留。

Android 和 macOS 集成测试均按测试根 `RepaintBoundary` 把焦点、多选高亮和待粘贴预览三种状态截图写到系统临时目录。Android 包在集成测试结束时被卸载，设备内文件随之消失，因此本轮在测试运行期间用 `adb exec-out run-as … cat` 轮询 `code_cache/`，以 PNG 魔数校验后把三张 `1080x2337` 设备截图复制到主机临时目录保留；macOS 三张 `800x600` 截图保留在沙盒容器 `Data/tmp/`。本会话的图像工具无法呈现图像（`read_image` 返回 "cannot get property \"attachments\" without inject"），因此不能声明视觉布局、ghost、焦点环、提示文本或遮挡情况已通过人工视觉审查，该缺口继续保留。已取得的替代证据是程序化解码与几何测量：

- 平台路由：移动端提交按钮的填充色 `#82D3E0` 只出现在 Android 待粘贴截图，边界框 `x=[207,659] y=[2001,2126]`，即 `172.6x48.0` 逻辑像素（与 `ValueKey('paste-at-current-cursor')` 的 `minimumSize: Size(0, 48)` 一致）；该颜色在 Android 焦点截图与多选截图中为 `0` 像素，可确认归属；在 macOS 三张截图中同样为 `0` 像素，桌面端未绘制该移动端按钮。
- 固定判定线：`judgement` `#E6B75A` 在 Android 待粘贴与多选截图中为 `x=[0,1079] y=[866,880]`（整宽 `411.4` 逻辑像素、高 `5.7`），macOS 对应截图为 `x=[0,799] y=[208,212]`（整宽、高 `5.0`）；Android 焦点截图中该色带扩展到 `y=[816,930]`，与 `app/lib/src/ui/editor_canvas.dart:1005` 的标记焦点绘制一致。
- 待粘贴预览：两个平台的待粘贴截图都出现同一合成色 `#162A27`，与 `markerPlayer` `#3BAA73` 叠加在画布 `#10151A` 上得到的绿色带一致，且横跨整个画布宽度（Android `x=[3,1076] y=[814,2273]`，macOS `x=[1,798] y=[284,575]`），在多选与焦点截图中为 `0` 像素；三张截图中 `danger` `#E07B84` 均为 `0` 像素，说明捕获的是合法预览而非冲突预览。
- 其余主色与 `app/lib/src/ui/editor_theme.dart` 一致：画布 `#10151A`、面板 `#171D23`、抬升面 `#202831`、边框 `#34404A`；多选跨度上的主导色 `#19252B`（Android）与 `#18252B`（macOS）等于 `accent` `#65B8C5` 以 `0.10` 叠加画布。

上述测量只能排除截图全黑并确认颜色、平台路由与几何量级，不能代替对排版、遮挡、ghost 轮廓、焦点环形状和文本内容的视觉审查。Windows、iOS、Release 运行时和真实 Android 设备未在本轮执行。既有 `NPC-S` 拆分、边界删除合并和 Showtime 清理改动继续保留；既有 Release 进程与原模拟器数据保持原状。本轮未提交或推送。


