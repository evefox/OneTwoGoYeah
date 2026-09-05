# 实现状态

更新时间：2026-09-05

## 当前阶段

阶段 8 已完成：Flutter 三列编辑器、Dart FFI 原生资产、真实音频/谱面工作流、私有草稿与异常恢复、四平台播放和文件适配、桌面关闭保护、响应式输入与无障碍均已落地。阶段 8.5 的 macOS 体验微调已完成并通过整体验收，当前进入阶段 9 发布前兼容验证。

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
- 把固定 libVorbis `vorbisenc.c`、公开编码头和 23 份模式/码本表纳入离线快照；第三方源码总摘要更新为 `d4e1715267b23503c51bd73e42f4ccfd0523aa070d980c780c692f1f2a75a422`。
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
- 增加 `parse_slk_records(bytes)` 二进制入口，不解码标题字节；严格扫描 ID、CRLF、六列类型 schema、受支持前导记录、Y3 起连续 sheet row、相邻 X1..X6、最终 `[r & 0]` 和容器 `E`。
- 底层解析返回有序自有 `SlkParsedRow`，强制四列一致、位置递增及 `n/s/c/d/r` 值组合；规范/兼容 SLK 分别解析 428/404 行，并覆盖未知记录、schema/列/行号/整数/类型/边界错误、来源位置和所有权。
- 增加 `analyze_slk_drill_groups(records, beginRowIndex)`，在单个普通序列中配对紧邻的 NPC/玩家块，并在未消费的 Showtime 或文件边界前停止。
- `DrillGroup` 结果以 NPC 起始 sheet row 作为同文档稳定标识，逐条传播显式角色、来源行和块内索引；动作相对位置以各自首个 `n` 为原点，兼容两个 `c` 到首个 `n` 间隔不同的合法块。
- 规范/兼容 SLK 的两个普通序列分别识别 `14 + 20` 与 `16 + 18` 个组；合成测试覆盖可选 `s`、后续阶段游标、最终空序列、深复制/移动，以及结构、内容、动作位置和结束边界的稳定来源错误。
- 增加 `analyze_slk_dance_move_drills(groupAnalysis)`，统一解码直接 `1..11` 与拆分 `segmentNCount * 100 + moveNCount`，按段拼接 NPC/玩家完整舞步并透传普通序列边界。
- `DanceMoveDrill` 以首组标识作为同文档稳定标识，保留来源组映射、原始 `c`、段长、显式角色及连续舞步行索引；规范/兼容两阶段分别还原 `6 + 11` 与 `8 + 9` 个演练。
- 舞步测试覆盖直接值全范围、`3+2+3=8`、两位数段长 `10+1=11`、真实 `moveNCount` 顺序、深复制/移动，以及无效值、数量、编码、总和、提前/缺失结尾和伪拆分的稳定来源错误。
- 增加 `analyze_slk_showtimes(records)`，从完整底层行逐阶段复用唯一分组/舞步入口，把连续 `d` 与有序 `DanceMoveDrill` 一一关联并最终消费末行 `[r & 0]`。
- Showtime/`SHOW-D` 分别以自身 sheet row 作为同文档稳定标识，每个 `SHOW-D` 自有保存来源行、阶段内索引、演练引用和已校验 `showNCount`；规范/兼容文件分别关联 `6+11` 与 `8+9` 个引用。
- 完整语义测试覆盖多阶段、稳定映射、边界与所有权，以及空阶段、缺少 Showtime、下层失败、`d` 数量/值、缺失/错误边界和空输入的结构化原子错误。
- 增加 `import_finished_slk(bytes)` 原子入口，只编排现有记录解析与完整语义分析；全部成功才发布自有 `FinishedSlkDocument`，失败明确区分解析/语义层且不暴露部分值。
- 严格导入测试覆盖两份真实文件、输入清除、深复制/移动、空/空指针/损坏头，以及语法合法的空谱面和缺失 `d`，并校验未使用下层错误保持 `None`。
- 增加不依赖 SLK 的原生 `ChartDraft` 事件与关系容器，并由 `project_finished_slk_to_chart_draft(document)` 从已验证语义一次生成显式角色、稳定标识和 `showNCount`。
- 规范/兼容投影分别得到 `408/384` 个角色事件，且各有 `2` 个 Showtime 起点、`17` 个 `SHOW-D`、`34` 个演练组、`17` 个舞步演练和 `2` 个 Showtime；全部引用、顺序、唯一 ID、所有权及无 `r` 事件均通过。
- 增加 `validate_finished_chart(draft)`，只读校验事件唯一性、角色块、三层关系、完整数量和 `showNCount`；零诊断才深复制生成 `FinishedChart`，不修改或修复草稿。
- 校验测试覆盖两份真实导入草稿，以及空草稿、重复 ID/时间行、孤立内容、块不匹配、舞步数量和 Showtime 值错误，验证诊断、快照所有权与输入不变。
- 收紧 `FinishedChart` 边界：调用方不能直接构造快照，校验器进一步要求 `SHOW-C/SHOW-D` 恰好被关系拥有、按 Showtime/舞步演练/演练组/NPC/玩家/Showtime 展平后的事件位置严格递增，并拒绝展示范围整数溢出。
- 增加 `export_finished_chart_to_slk(FinishedChart)`，在 `av_core` 内编译固定 `z3116_lv5.slk` 的 2686 字节规范头，不在运行时读取 `example/`，只替换 `B`/`NN` 行数范围并按原始字节保留乱码标题、格式记录、列宽、筛选、CRLF 和结尾。
- 增加 `project_chart_markers` 三列统一投影及前一个、前一个本列、后一个标记查询；底层容器乱序不改变结果，当前位置严格排除。
- 增加 `project_showtime_moves` 和 `chart_note_ordinal`，从权威 `showNCount`/事件类型派生展示范围、连续序列首尾状态及 NPC/玩家独立 N 编号，不持久化派生值。
- 增加 `ChartEditor`，统一执行三列单行占用、NPC-C/N/S 放置转换删除、玩家相对间隔自动生成、Showtime 来源收集/位置/排布/范围冲突和所有失效事件引用清理。
- 每次成功上下文点击、整批生成、整段删除或清空只提交一份完整 `ChartDraft` 历史；失败不修改草稿或历史，撤销/重做逐值恢复全部事件与关系，新编辑清空重做分支。
- 玩家与 Showtime 自动生成结果在提交后脱离来源同步；NPC 后续修改只更新当前原生舞步的 N 总数，既有玩家位置/类型和 `SHOW-D.showNCount` 均保持生成时快照。
- 增加 `encode_formal_draft`/`decode_formal_draft`，用内部 `OTGYPRJ\0` version 1 小端信封、显式类型/长度和 CRC32 保存完整 `ChartDraft` 及 BPM/Offset 原始 binary64 位模式。
- 增加 `encode_recovery_snapshot`/`decode_recovery_snapshot`，复用相同容器安全与完整性校验但只保存 `ChartDraft`；不完整草稿无需通过成品校验即可精确往返，历史、音频和视图状态不进入负载。
- 原生负载测试固定空正式/恢复负载的规范头、长度和 CRC 字节，并覆盖两份真实 SLK 完整关系、不完整编辑草稿、隐藏精度、类型错配、版本/长度/CRC、非法枚举、超大计数、截断和尾随数据。
- 导出器从显式角色和三层关系唯一生成 NPC 后玩家的 `c/n/s`、Showtime `c0/d`、直接及 `segmentNCount * 100 + moveNCount` 拆分值，并通过共享位置换算写六列；最终边界固定为 `max(1920, lastShowD.rangeEnd)`。
- 导出测试覆盖规范文件导入、投影、校验再导出的 30340 字节逐字节相同，兼容文件使用规范外壳后的完整原生语义往返，直接/拆分 `c` 值、超长边界、CRLF/BOM/结尾，以及工作表末行 `161/162/313/314/>314` 时两个格式锚点和 `B`/`NN` 范围字段。
- 把 `.onsets`、`.bpm_candidates`、`.offsets`、`.sync_candidates` 明确标记为 Git binary，禁止跨平台检出时的换行或文本规范化。
- 对末尾图块的四舍五入索引显式限制到最后一个有效 PCM 帧，消除参考代码的越界未定义行为；负数幅值缩放显式匹配算术右移结果。
- 增加四份 1x、四份 2x、四份 3x 与四份 4x、共十六份 65536 字节 CC0 Alpha 黄金夹具，固定 UI Zoom `11`、Scale `4`、block `1` 的左右声道逐字节结果及 SHA-256；每个新组合都重新逐字节验证全部既有输出，并覆盖均匀亮度 2x 的 `0/63/127/191/255`、均匀亮度 3x 的 `0/28/56/85/113/141/170/198/226/255`、均匀亮度 4x 的 `0/15/31/47/63/79/95/111/127/143/159/175/191/207/223/239/255`、振幅亮度 2x/3x/4x 的超采样行亮度与整数平均、1x 每行单一振幅 Alpha、中心对称、形状/亮度/采样差异、末帧限制和最小 Scale 下的 `247/239` 亮度边界。
- 增加 `calculate_chart_timeline_range` 模型入口，在 `av_core` 内统一投影并扫描全部谱面事件与 `SHOW-D.rangeEnd`；C ABI 只传入 `ChartDraft` 句柄，不重复领域扫描。
- 增加 `native/av_c_api` 动态库目标 `otgy_c_api`，ABI 与共享库主版本固定为 `1`；公共头只使用 C 类型，Windows 与其他平台均显式控制导出可见性。
- C ABI 使用统一状态码、稳定领域/领域内错误、拥有型 `otgy_error`、显式 `uint64_t` 长度、不透明拥有句柄和匹配销毁函数；全部有状态入口封闭 C++ 异常，失败不发布部分拥有结果。
- ABI 覆盖 PCM、MP3/Ogg 解码、哈希、Offset、文件名、品质、不可变 Ogg 导出、十六种波形、起音/BPM/Offset/联合分析、六个时序换算、网格、谱面模型时间线范围、导航、播放、操作门控、谱面编辑与历史、校验、项目负载、SLK 导入/导出和位置换算。
- 增加拥有型扁平谱面语义快照，逐项保存显式角色事件、演练组、舞步演练、Showtime、`showNCount` 和引用切片；不从 SLK 行、列位置或简称重建语义。
- 增加纯 C 头链接测试、音频一致性测试和谱面一致性测试；公共头预处理后的 162 个函数声明与动态库 162 个 `otgy_*` 导出符号完全一致，其中包含宏展开的 16 个波形入口、播放 PCM/按帧推进和四个音频导出取消句柄入口。
- 波形过滤和未固定契约的额外 CLI 命令不属于阶段 8；当前产品工作流、播放 PCM/设备输出、平台草稿索引、原子文件发布及 Flutter 四平台适配均已实现。

## 工具链

- CMake：`3.27.7`
- C/C++ 编译器：Xcode Apple Clang `21.0.0`，arm64 macOS
- Ninja：未安装，CMake 使用默认生成器
- Flutter：`3.47.2`，Dart `3.13.2`
- Android SDK：`35/36`，NDK `28.2.13676358`；本机 Debug APK 已完成构建
- Apple SDK：Xcode `26.6`（macOS SDK `26.5`），CocoaPods `1.17.0`
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

2026-08-31 在 arm64 macOS 上运行：

```text
cmake --build build/stage8-native --parallel
ctest --test-dir build/stage8-native --output-on-failure

cd app/packages/otgy_native
../../../build/flutter-sdk/bin/dart analyze
../../../build/flutter-sdk/bin/dart test -r expanded

cd ../..
../build/flutter-sdk/bin/flutter analyze
../build/flutter-sdk/bin/flutter test -r expanded
../build/flutter-sdk/bin/flutter build macos --debug
codesign -d --entitlements :- build/macos/Build/Products/Debug/OneTwoGoYeah.app

swiftc -parse ios/Runner/AppDelegate.swift
swiftc -parse macos/Runner/MainFlutterWindow.swift
```

结果：原生核心、CLI 和 C ABI 49/49 通过；Dart FFI 分析通过且 6/6 测试通过；Flutter 分析通过且 25/25 测试通过。公共头 162 个函数声明与动态库 162 个 `otgy_*` 导出逐项一致。播放 PCM、设备帧推进、导出取消、六阶段分析回调、原子不覆盖发布、未保存退出和既有全部解码/波形/BPM/Offset/草稿/SLK 黄金均继续通过。Android Debug APK 已使用 SDK 35/36 与 NDK 28.2 完整构建，原生资产覆盖 `armeabi-v7a`、`arm64-v8a` 和 `x86_64`；macOS Debug App 已使用 Xcode 26.6 完整构建并启动，最终签名确认包含 `com.apple.security.files.user-selected.read-write`；iOS/macOS Swift 源码语法解析通过。Windows 与 iOS 真实 SDK 构建继续由阶段 8 CI 验证。

## 已知问题

- 完整 GPLv3 正文和第三方声明必须在分发前离线纳入仓库；当前 `LICENSE` 先固定 `GPL-3.0-or-later` 声明。
- 示例 MP3/Ogg 的再分发许可尚未确认，已通过 `.gitignore` 排除；两个 SLK 继续保留为本地规范与测试参考。
- 本机已完成 Android Debug APK 与 macOS Debug App 真实构建；当前不是 Windows 环境，也尚未在真实 iOS 设备上验证，工作流继续覆盖其余平台，但远端首次运行结果不属于本地验证证据。
- 对固定的 libmad/libogg/libVorbis 旧版 C 快照执行全量 UndefinedBehaviorSanitizer 时，会在解码/编码路径报告负下标式指针访问、空指针零偏移和有符号左移诊断，包括 `libogg/framing.c`、`bitwise.c` 以及 libVorbis `sharedbook.c`、`psy.c`、`floor1.c`。普通构建与全量 AddressSanitizer 仍精确通过全部黄金结果；未用测试过滤或更宽容断言掩盖这些诊断。阶段 7 定向组合验证只对新增/修改的自有 C++ 路径启用 UndefinedBehaviorSanitizer，对固定第三方 C 快照保留 AddressSanitizer，并以 `halt_on_error=1` 通过 4/4。

## 唯一下一步

阶段 8.5 已完成验收。下一步进入阶段 9：在实际 x86-64 与 ARM64 的 Windows、macOS、iOS 和 Android 环境运行完整黄金与平台集成套件，补齐完整 GPLv3 正文、第三方声明、离线分发说明和源码安装文档，再形成可发布构建。
