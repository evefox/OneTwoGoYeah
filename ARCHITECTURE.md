# 架构设计

状态：已接受的基线。架构决策发生变化时必须更新本文档。

## 目标

- 在 Windows、macOS、iOS 和 Android 上完全离线运行。
- 保持与 ArrowVortex 兼容的音频解码、波形行为、BPM 检测和 Offset 检测。
- 保存比 ArrowVortex 界面显示或导出结果更高的项目精度。
- 让 App、C++ 测试、命令行工具和未来集成都调用同一份实现。
- 保持代码易于理解和测试，并确保领域核心不依赖 UI 或操作系统。

## 许可证与分发

- 项目使用 GPLv3 或更高版本许可证。
- 保留 ArrowVortex 以及所有复用依赖的版权声明和许可证文本。
- 分发源代码，也可以提供用户自行安装的构建产物；不要求上架应用商店。
- 本地 `../ArrowVortex-release` 目录仅作为开发期间的行为和源码参考，不是运行时或构建时依赖。

## 依赖方向

```text
Flutter App
        |
        v
轻量 Dart FFI 绑定
        |
        v
稳定 C ABI 包装层  <----- C ABI 测试
        |
        v
C++ av_core  <-------------- C++ 单元测试和黄金测试
   ^       ^
   |       |
CLI 工具   平台播放与文件适配层
```

所有依赖都指向 `av_core`。`av_core` 不得导入或调用 Flutter App、C ABI 包装层、CLI 或平台适配层。

## 仓库结构

```text
OneTwoGoYeah/
├── app/
│   ├── lib/src/{application,infrastructure,ui}/
│   ├── packages/otgy_native/{hook,lib,test}/
│   └── {android,ios,macos,windows}/
├── native/
│   ├── av_core/
│   │   ├── include/
│   │   ├── src/
│   │   │   ├── audio/
│   │   │   ├── waveform/
│   │   │   ├── timing/
│   │   │   ├── slk/
│   │   │   └── project/
│   │   └── tests/
│   ├── av_c_api/
│   └── tools/
│       └── avtool/
├── fixtures/
│   ├── audio/
│   ├── waveform/
│   ├── sync/
│   └── slk/
├── AGENTS.md
├── ARCHITECTURE.md
├── PRODUCT.md
└── TESTING.md
```

Flutter App、Dart 原生资产包和四个平台 Runner 均已按此边界建立。目录名称可以演进，但不得改变层级边界和依赖方向。

## 核心边界

所有必须跨平台保持一致的行为都由 `av_core` 负责：

- MP3 和 Ogg Vorbis 解码
- 规范 PCM 表示
- 波形采样与 Alpha 图块生成
- 播放位置推进，以及从时间线位置生成规范设备播放 PCM
- 起音检测
- BPM 候选检测与排序
- Offset 检测
- 时序转换与谱面数据模型
- 项目序列化
- 严格 SLK 解析、语义导入、校验和兼容导出
- 开始编辑后使用的可撤销领域命令

`av_core` 不得依赖：

- Flutter 或 Dart
- 窗口、组件、画布或 GPU
- 音频输出设备
- 文件选择器、剪贴板、菜单或通知 API
- Win32、CoreAudio、AVFoundation、Android 媒体 API 或其他平台专用 API
- `gMusic`、`gTempo`、`gView` 等 ArrowVortex 全局对象

核心 API 必须显式接收依赖，并返回拥有明确所有权的值或显式结果对象。可同步调用的核心入口是测试所依据的权威实现。App 层后台任务可以在线程池中调用这些入口。

## 规范 PCM16 数据模型

`av_core` 使用唯一的规范音频内存表示：拥有所有权的双声道、平面、有符号
16 位 PCM。`Pcm16` 保存一个大于零的 `uint32_t` 采样率、一个 `size_t`
帧数，以及长度始终相等的左、右 `int16_t` 通道。零帧是合法的空 PCM
值；采样率为零不是合法值。通道顺序固定为 Left、Right，通道数据由
`Pcm16` 自身拥有，`allocate` 产生零初始化通道，`copy_from_channels`
复制调用方缓冲区而不保留别名。复制 `Pcm16` 是深复制，移动转移两个通道的
所有权；可变通道指针只能修改既有样本，不能改变帧数或调整容量。

每通道字节数为 `frameCount * sizeof(int16_t)`，总字节数为两倍；构造前由
共享 `validate_pcm_dimensions` 检查采样率、`size_t` 乘法溢出和标准库容器的
最大尺寸，任何失败都不会访问输入缓冲区或留下部分对象。PCM 工厂返回带有
稳定 `PcmError` 的显式 `PcmResult`：无效采样率、通道帧数不匹配、非零帧数的
空通道指针、尺寸溢出和分配失败分别可区分。该模型只使用标准 C++ 内存，不
依赖解码器、平台音频 API 或文件；MP3/Ogg 解码器后续必须把结果提交为此
表示。

## SLK 位置契约

SLK 位置转换属于 `av_core`，不得在 Flutter、C ABI、平台适配层或命令行展示代码中重复实现。每个 SLK 数据行的前四列，是同一个从零开始的谱面位置的两种等价整数表示：

```text
totalSixteenth = measure * 16 + beat * 4 + sixteenth
measure        = totalSixteenth / 16
beat           = (totalSixteenth % 16) / 4
sixteenth      = totalSixteenth % 4
```

这里 `/` 表示非负整数除法；`beat` 和 `sixteenth` 的范围都是 `0..3`。导入必须解析并校验该关系；导出必须通过共享转换生成一致的四列数据。如果导入行中的两种表示不一致，则属于解析错误。核心必须返回明确错误，不得修复该行，也不得静默选择其中一种表示。

当前整数权威入口为 `compose_slk_position`、`decompose_slk_position` 和
`validate_slk_position`。三者都返回完整 `SlkPosition { measure, beat, sixteenth, total_sixteenth }`，导出器不能自行拼装第四列。输入与结果使用非负 `uint64`；负号、
空字段和非十进制文本属于后续解析器错误。组合入口先校验两个分量 `0..3`，再用
显式上界检查避免 `measure * 16 + ...` 溢出。反向入口对任意 `uint64` 总位置都能
唯一分解，包括最大值。严格校验入口先执行同一组合逻辑，再逐整数比较第四列；
不一致返回 `InconsistentTotal`，不返回修复值。

当前底层记录入口为 `parse_slk_records(bytes, byteCount)`。它把输入视为不透明字节，
只按 ASCII 控制记录扫描，不把标题或格式表解码为 Unicode。文件必须全部使用 CRLF，并以
唯一末行 `E\r\n` 结束；导入首行必须是结构合法的 SYLK `ID` 记录（以 `ID;` 开始、
包含名称标志 `N` 且以 `E` 字段结束），不要求固定为 `ID;PWXL;N;E`。解析器接受不同
Excel/SYLK 工具实际使用的 `P/F/B/O/NN/C` 前导记录族，但要求六列类型声明逐记录匹配
`int/int/int/int/enum(n,s,c,d,r)/int`。未知记录、裸 LF、截断行或
不完整单元格组都返回稳定来源行错误。

谱面数据必须从 sheet row `3` 连续递增，每个底层行严格由 X1..X6 六条相邻 `C`
记录组成；数据行之间只允许参考文件实际使用的 `F` 格式记录，最终数据行恰好位于 sheet row `162/314` 时也允许紧随对应固定 `F` 记录。四个位置整数调用
`validate_slk_position`，位置必须严格递增。类型只接受带双引号的小写 `n/s/c/d/r`；
`n/s/r` 只允许值 `0`，`c` 接受非负整数，`d` 要求非零。导入允许零个或一个 `[r & 0]`；
如果存在，必须是最后一个谱面数据行；除上述最终固定 `F` 记录外，之后只能出现容器 `E`。成功结果拥有包含边界行（如果存在）在内的有序
`SlkParsedRow` 数组，不引用输入字节；复制为深复制，移动转移所有权。失败返回稳定
错误、物理行号、sheet row 和列号且不暴露部分数组。此层只保证记录与底层行合法，
不推导 NPC/玩家角色、演练组、舞步或 Showtime 关联。

SLK 只保存有序的底层行值，不显式保存 NPC/玩家角色、`DrillGroup` 标识、`DanceMoveDrill` 标识或 Showtime 关联。因此语义模型包含的信息严格多于交换格式。

SLK 块识别和角色分配属于 `av_core` 的语义分析，不属于底层行解析器。在成品谱面中，共享谱面模型把聚合对象命名为 `DrillGroup`（演练组）。一个成品 `DrillGroup` 由两个紧邻且完全相同的块组成；每个块包含一个 `[c & 非0]`、一个或多个 `[n & 0]`，以及零个或一个结尾 `[s & 0]`。整份谱面的分析结果显式表示以下有序角色：

- 第一个块：NPC 演示
- 第二个块：玩家练习

成功解释成品谱面后，已识别块中的每个条目都继承该块的角色和 `DrillGroup` 标识。同一种局部 `[c & 非0] + [n & 0]...` 结构会因整份谱面的上下文不同而属于不同角色，因此严格 SLK 导入器只有在整份谱面通过校验后才推导角色。Flutter 和其他适配层必须使用核心返回的分组和显式角色，不得根据行顺序自行推断。兼容导出必须保持 NPC 在前、玩家在后的顺序。原生 `ChartDraft` 仍可表示不完整或不匹配的演练组，因为所有已经存在的角色敏感对象都会保留显式角色，与草稿当前能否导出无关。

当前第一层入口为 `analyze_slk_drill_groups(records, beginRowIndex)`。它只分析一个普通演示/练习序列，并在第一个 `[c & 0]`、`[r & 0]` 或记录末尾之前停止，返回未消费的结束行索引和 `Showtime`、`FileBoundary` 或 `EndOfRecords` 结束类型，供后续 Showtime 层继续解释。块的类型和值逐条相同；动作位置则以每块首个 `n` 为原点比较全部 `n/s` 相对位置。`c` 只作为块锚点，NPC/玩家各自从 `c` 到首个 `n` 的间隔不参与内容匹配，这是兼容 `谎话成品.slk` 所必需的现有格式行为。

分析成功时，第一个块固定为 NPC、第二个块固定为玩家；`DrillGroup` 标识确定为 NPC 起始 `c` 的 sheet row，在同一已解析文档中唯一且可复现。每个自有结果条目同时保存来源底层行索引、块内索引、角色和组标识，不引用输入数组。孤立块、缺少 `n`、块长度/类型/值/动作相对位置不一致都原子失败，只返回稳定错误和来源 sheet row，不暴露部分分组。此层不解释 `c` 数量编码、不拼接 `DanceMove`、不关联 Showtime，也不构造 `ChartDraft`。

共享谱面模型区分 `DanceMove`（舞步）与 `DanceMoveDrill`（舞步演练）。在成品谱面中，一个 `DanceMove` 是单个角色的一至十一个 `[n & 0]`，后接恰好一个结尾 `[s & 0]`。一个成品 `DanceMoveDrill` 是用于演练该舞步的有序、非空 `DrillGroup` 列表。按演练组顺序拼接 NPC 块或玩家块，都必须还原出相同的完整 `DanceMove`。只有最后一个演练组的每个角色块带有结尾 `[s & 0]`。

SLK `c` 值的编码、解码和校验都由共享核心负责：

```text
如果 DanceMoveDrill 未拆分：
    cValue        = moveNCount
    segmentNCount = moveNCount

如果 DanceMoveDrill 已拆分：
    cValue        = segmentNCount * 100 + moveNCount
    segmentNCount = cValue / 100
    moveNCount    = cValue % 100
```

这里 `/` 表示非负整数除法。未拆分的 `cValue` 直接位于 `1..11`。拆分表示使用末两位十进制数字保存 `moveNCount`，其商可能超过一位，所以实现不得假定所有拆分值都恰好是三位数。对于成品谱面，一个拆分 `DanceMoveDrill` 内的所有演练组必须编码相同的 `moveNCount`；有序 `segmentNCount` 之和必须恰好等于该值；每段数量还必须等于从任一角色块实际解析出的 `[n & 0]` 数量。适配层只接收解码后的数量和模型标识，不得自行解析 `cValue` 的十进制数字。

当前第二层入口为 `analyze_slk_dance_move_drills(groupAnalysis)`。未拆分 `cValue` 必须位于 `1..11`，恰好消费一个带结尾 `s` 的 `DrillGroup`。拆分值必须消费至少两个连续组；每组的整数商等于实际 `n` 数量、余数位于 `1..11` 且全程相同，段长累计不得超过并必须最终等于该余数。累计完成前出现 `s`、累计完成时缺少 `s`、混用拆分/未拆分编码或普通序列提前结束都原子失败。

成功结果以首个 `DrillGroup` 标识作为同文档唯一且可复现的 `DanceMoveDrill` 标识，并保留每个来源组的原序列索引、组内索引、NPC/玩家两个原始 `c` 来源行、`cValue` 和解码段长。NPC 与玩家各自按组顺序拼接去掉 `c` 后的 `n/s` 行，得到带显式角色、演练标识和连续舞步内索引的自有 `DanceMove`；两者的 `n` 数量等于权威 `moveNCount` 且最后一行是唯一的 `s`。结果同时透传普通序列的底层起止行和 Showtime/文件边界类型，不扫描或关联边界后的 `d`。

`Showtime` 识别和舞步关联同样属于 `av_core`。在成品谱面中，`[c & 0]` 标记结束紧邻其前的有序演示/练习阶段，并开始对应的 Showtime。随后至少一个、最多覆盖全部前置舞步演练的连续非零 `d` 条目，按顺序与该阶段 `DanceMoveDrill` 的有序前缀一一对应；不能跳过前置舞步或只引用后缀。每个 `d` 值必须等于所引用 `DanceMove` 的 `moveNCount`。共享模型保留该阶段的全部 `DanceMoveDrill` 列表，并为每个已展示的 `SHOW-D` 显式保留关联和自身的 `showNCount`：严格导入时它来自已校验的 `d` 值，自动补全时它是生成当下所引用 `DanceMove.moveNCount` 的快照。关联只用于来源追踪和成品校验，不是实时数据绑定；Flutter、C ABI 和其他适配层不得通过自行扫描 SLK 行重新建立关联。兼容导出按行序逐阶段写入：阶段从上一个 Showtime 最后一个 `SHOW-D` 事件行之后开始，展示范围只是视觉提示、不决定归属；先写该阶段被引用的舞步演练和排在其后的无归属舞步演练，再写 `[c & 0]` 与有序 `d` 条目。`[c & 0]` 是可选的：最后一个普通阶段之后没有 Showtime 标记时，该阶段的 `DanceMoveDrill` 作为无归属演练保留；只要存在 Showtime，其规则不变。一个 Showtime 可以只展示其前置普通阶段按行序最前面的一段舞步，未被 `d` 引用的完整舞步演练继续留在该阶段并排在已引用舞步之后，不能夹在已引用舞步之间；每个 `[c & 0]` 之后至少有一个 `d` 条目。

当前完整语义入口为 `analyze_slk_showtimes(records)`。它从底层第一个谱面行开始，对每个普通阶段调用上述唯一分组和舞步演练入口；除最后一个普通阶段外，每个普通阶段必须非空且必须紧随 `[c & 0]`。最后一个普通阶段可以不带 Showtime，并以 `[r & 0]` 或记录末尾结束。其后连续 `d` 的数量必须在 `1..该阶段 DanceMoveDrill 数量` 内，且逐项值等于有序前缀中对应舞步的 `moveNCount`。Showtime 标识取 `c0` 的 sheet row，`SHOW-D` 标识取自身 sheet row；每个自有 `SHOW-D` 同时保存来源底层行、阶段内索引、所引用演练标识和来自已校验 `d` 的 `showNCount`。末尾无 Showtime 阶段的自有 `DanceMoveDrill` 列表单独保存在分析结果中，不生成 Showtime 或 `SHOW-D`。

分析器继续从 `d` 后第一行解释下一普通阶段，接受零个或一个末行 `[r & 0]`，并在存在时把它作为已消费的格式边界返回，不创建领域事件。空谱面（既无 Showtime 也无普通阶段）、空普通阶段、分组或舞步分析失败、`d` 数量/值不匹配或非末行边界均原子失败。失败携带绝对来源行、sheet row、Showtime/阶段组/`SHOW-D` 索引及下层稳定错误，不暴露任何部分 Showtime。

严格字节事务入口为 `import_finished_slk(bytes, byteCount)`。它不增加第三套格式逻辑，只顺序调用 `parse_slk_records` 和 `analyze_slk_showtimes`；底层记录仅作为事务内临时值存在。两层全部成功后才发布自有 `FinishedSlkDocument`，其中完整语义不引用输入字节或临时记录。失败明确区分 `RecordParseFailed` 与 `SemanticAnalysisFailed`，只填充对应下层结构化错误，另一层错误保持 `None`，且结果不包含部分文档。

每个 Showtime 舞步引用的派生时长由该 `SHOW-D` 自身的 `showNCount` 计算：`1..6` 对应一小节，`7` 及以上对应两小节。在固定 4/4、每拍四个 1/16 的谱面坐标中分别为 16 和 32 个 1/16 位置。该计算只在 `av_core` 实现，UI、C ABI 和导出器只消费结果；原生项目不得再保存一个可能与 `showNCount` 冲突的可编辑时长副本。修改所关联舞步的当前 `moveNCount` 不得回写已有 `SHOW-D.showNCount`，因此也不得自动改变其位置或展示范围。生成时无法取得舞步数量则不能生成对应的 `SHOW-D`；生成后的不匹配由成品校验报告。

`SHOW-D1` 和 `SHOW-D+` 是从 `SHOW-C` 后连续 `SHOW-D` 序列派生的查询角色，不进入持久化事件类型。核心根据 `dRow` 所在小节起点与派生一/二小节时长返回左闭右开的展示范围 `[rangeStart, rangeEnd)`。位置验证只允许 `SHOW-D1` 位于小节线或小节线 +1；所属 `SHOW-C` 不在小节线时进一步限制为小节线。`SHOW-D+` 始终位于小节线，并且不能落入前一个 `SHOW-D` 的展示范围。后续位置统一等于前一展示范围的 `rangeEnd`。

核心格式适配层把最后一个普通阶段之后可选的 `[r & 0]` 作为兼容 SLK 文件边界，并在兼容导出的最后一段是 Showtime 阶段时自动写出该行；它与 SYLK 容器结束行 `E` 不同。严格 SLK 导入会在存在时验证并消费该边界，缺失时也可完成导入，但不得为它创建 `ChartDraft` 事件。领域模型、C ABI 编辑接口和 Flutter 投影均不暴露对应类型，编辑器的相邻查询、上下文点击和撤销历史都把它视为不存在。

导出器不保存或复用导入文档的边界位置。规范模板的 `1920` 是最小边界；最后一段是 Showtime 阶段时，导出器计算最后一个 `SHOW-D` 的 `[rangeStart, rangeEnd)`，使用 `max(1920, rangeEnd)` 作为 `r` 的总 1/16 行，再调用共享 `decompose_slk_position` 生成四列。最后一段是无 Showtime 的普通舞步阶段时，导出器不写边界行，文件在该阶段最后一个事件后直接结束；末尾舞步行不参与边界位置计算。

## SLK 规范字节导出契约

成品 SLK 导出只有一个规范容器模板：`example/z3116_lv5.slk`，其当前 SHA-256 为 `8d203601373b3be426d220f002d8d945c7d94b8c44670cbee4ceab82dd723a12`。`example/谎话成品.slk` 只用于导入和语义测试，不能作为另一种导出外壳或按来源选择的模板。生产实现不得在运行时依赖 `example` 目录；脚手架建立时应把经哈希校验的模板字节或等价生成资源固定在 `av_core` 中，并由同一核心序列化器使用。

模板固定部分按不透明字节处理，不能先按 UTF-8、CP949、系统 ANSI 或其他字符集解码后再编码。以下字节级属性固定：

- 导入接受合法但与模板不同的 `ID` 记录及 Excel/SYLK 前导、格式表、列宽、筛选定义和标题记录；不带 BOM，且固定结构记录仍须满足 SYLK 语法。
- 导出文件以 `ID;PWXL;N;E\r\n` 开始；其 Excel/SYLK 前导、格式表、列宽、筛选定义、标题行、类型行及固定记录的字节、顺序和引号形式来自规范模板。
- 第一行标题保留模板中的原始韩文编码字节；即使调用方把它显示为乱码，也不得替换或修复。
- 第二行依次定义六列为 `int`、`int`、`int`、`int`、`enum(n,s,c,d,r)`、`int`。
- 每条记录使用 CRLF；文件以单独的 `E\r\n` 结束，末尾不能缺少 CRLF，也不能追加空行。

令 `encodedRowCount` 为语义编码后全部 SLK 谱面行数量，包括导出器自动生成的最终 `[r & 0]`。工作表中的前两行固定用于标题和类型，因此：

```text
lastSheetRow = encodedRowCount + 2

B record  = B;Y{lastSheetRow};X7;D0 0 {lastSheetRow - 1} 6
NN record = NN;N_FilterDatabase;ER1C1:R{lastSheetRow}C6
```

这两个范围记录是固定模板中仅因谱面行数而变化的非数据字段。谱面行从工作表 `Y3` 开始连续编号，每个语义行严格输出六条单元格记录：

```text
C;Y{sheetRow};X1;K{measure}
C;X2;K{beat}
C;X3;K{sixteenth}
C;X4;K{totalSixteenth}
C;X5;K"{type}"
C;X6;K{value}
```

整数使用无区域设置的十进制 ASCII，不写前导加号、分组符或无意义前导零；类型只写小写 ASCII `n/s/c/d/r`。规范模板中两个 `F;P0;FG0C;SM24;X7` 固定格式记录分别锚定在工作表第 162、314 行之后；输出数据覆盖到对应行时按相同位置和原始字节写出，数据未覆盖到该行时不在数据末尾虚构额外工作表行。除此之外，序列化器不得新增、省略、重排或平台化模板记录。

`av_core` 返回最终字节缓冲区。C ABI、CLI、Flutter 和平台文件层只能按二进制原样保存，禁止文本模式写入、换行转换、BOM 插入、Unicode 规范化或表格库重新序列化。相同 `FinishedChart` 在 Windows、macOS、iOS 和 Android 上必须得到逐字节相同的 SLK。

## 草稿与成品谱面边界

谱面数据管线必须明确：

```text
SLK 字节
   └──> 解析 ──> 整份谱面校验 ──> 语义解码 ──> ChartDraft
                                                    │
原生项目保存/加载 <─────────────────────────────────┤
                                                    │
                                                    └──> 校验 ──> FinishedChart
                                                                         │
                                                                         └──> 语义编码 ──> SLK 字节
```

底层 SLK 文档只在导入事务内存在，保留可解析的行顺序和值，但不虚构语义角色；它绝不会被提交为可编辑项目状态。`ChartDraft` 是权威的原生可编辑模型，支持部分块、不匹配但带角色的块、不完整分组、缺失关联，以及编辑过程中自然出现的其他状态。每个已有的角色敏感对象都必须通过自身字段或强制存在的带角色父对象，明确得到 NPC 或玩家角色。草稿领域命令必须保留角色和用户意图，不能强制每个中间状态都通过成品谱面构造器。

语义分析器和校验器是纯 `av_core` 操作。它们返回稳定的诊断代码、对象标识或来源位置，以及足以让任意 UI 解释问题的结构化上下文。分析绝不修改、修复、重排或丢弃项目数据。SLK 语义解码只有在整份谱面的所有必需约束都通过后才分配 NPC/玩家角色。原生草稿校验不会推断或重标角色；它校验显式带角色的模型，并且只有在所有派生 SLK 值和关联内部一致时才创建 `FinishedChart` 快照。

在 SLK 导入事务中，整份谱面解释成功后才构造带有显式角色、稳定原生标识和语义关系的新 `ChartDraft`。如果解析、成品谱面校验或语义解码失败，则不构造也不提交草稿，当前项目保持不变。创建角色敏感内容的编辑命令必须从显式编辑上下文接收或取得角色，绝不能从原始 `c`、`n` 或 `s` 值推断角色。

兼容成品 SLK 序列化只接受已经成功校验的 `FinishedChart` 快照。角色和关系通过规定的成品顺序隐式编码，不添加非标准 SLK 角色字段。正式草稿序列化只接受 `ChartDraft`，并且必须在不要求成品有效性的情况下，精确往返不完整谱面内容、对象标识、显式角色和语义关系。它不保存音频、BPM、Offset、缓存、播放位置、撤销历史或视图状态。因此正式草稿仍能保存 SLK 不包含的必要语义，但其负载范围严格限于谱面数据。App 可以持续校验以提供反馈，但校验结果不得阻止继续编辑或保存正式草稿。

当前工作区来源使用显式状态表示，不能根据名称或文件扩展名反推：

```text
WorkspaceSource = Untitled
                | ImportedSlk { sourceName }
                | SavedDraft { draftId, draftName }

WorkspaceState  = { source, dirty, chartDraft, audioTiming, viewState }
```

App 启动时直接创建 BPM 为精确 `100`、Offset 为精确 `0` 的 `Untitled` 空草稿。`SavedDraft` 的主保存操作使用稳定 `draftId` 原子覆盖对应草稿；`Untitled`、`ImportedSlk` 以及任何 `另存为` 操作都会创建或明确覆盖一个草稿，并在成功后切换为对应 `SavedDraft`。`dirty` 只表示当前 `ChartDraft` 与当前基准谱面不同；BPM 和 Offset 属于当前音频会话，不参与 `dirty`，也不进入撤销历史。成功导入 SLK 或加载正式草稿后，以导入或加载的谱面建立干净基准。导出 SLK 不改变 `source` 或 `dirty`。清空只修改内存中的谱面数据并设置 `dirty`，不得直接删除或覆盖正式草稿。

加载正式草稿时，只以事务方式替换 `ChartDraft` 和工作区来源；当前音频会话的权威 BPM、权威 Offset、规范 PCM、波形、分析缓存、来源元数据、品质上限及其他音频状态保持不变。严格导入 SLK 时，文件不提供 BPM/Offset，因此成功事务只替换 `ChartDraft` 和工作区来源，当前音频、BPM、Offset 及音频派生状态全部保留。两种事务失败时都保持完整当前状态不变。

`[c & value]`、`[n & 0]`、`[s & 0]` 等原始 SLK 组合只是序列化边界表示，不足以作为原生语义标识。原生模型使用带角色的块和事件；查询任何角色敏感对象时，无需重新扫描类似 SLK 的相邻数据就必须能得到 NPC 或玩家角色。即使两个对象会序列化为相同原始组合，只要角色不同，它们仍是不同对象。拆分 `cValue` 和 Showtime `[c & 0]` 在导入/导出边界与显式语义结构相互转换或由其派生；`[r & 0]` 仅由格式适配层消费或生成，不进入原生草稿模型。

当前原生只读导入投影为 `project_finished_slk_to_chart_draft(document)`。`ChartDraft` 分开拥有带 `ChartRole` 的 `BlockStart/Note/MoveEnd` 事件、Showtime 起点事件、带 `showNCount` 和演练引用的 Showtime 舞步事件，以及 `DrillGroup`、`DanceMoveDrill`、Showtime 三层关系。导入生成的各类初始稳定标识取对应来源 sheet row，但投影后所有查询只使用原生字段和关系，不再读取 SLK 类型字母、相邻顺序或来源记录。

每个角色事件直接保存角色、连续 1/16 行、演练组标识和舞步演练标识；每个演练组分别列出 NPC/玩家事件标识；每个舞步演练列出有序演练组；每个 Showtime 列出有序舞步演练和 `SHOW-D` 事件。投影保持全局时间顺序并深复制全部数据，格式边界 `[r & 0]` 不生成任何事件、标识或关系；末尾无 Showtime 普通阶段的舞步演练同样投影为无归属 `DanceMoveDrill`，不生成 Showtime 事件。当前 `ChartDraft` 容器不在构造时施加成品约束，因此后续编辑仍可保存不完整状态；只有这个导入投影的输入因为来自 `FinishedSlkDocument` 而必然完整。

当前成品边界为 `validate_finished_chart(draft)`。它只读校验事件 ID 与全局时间行唯一性、角色块结构与匹配、演练组/舞步演练/Showtime 引用、完整 `moveNCount`、结尾位置和 `SHOW-D.showNCount`，并报告带对象、关联对象和时间行上下文的稳定诊断。草稿不会被修复、重排或修改；只有零诊断时才深复制生成 `FinishedChart`，分配失败与领域诊断分开返回。

`FinishedChart` 不能由调用方直接构造，只能由上述零诊断校验边界创建。校验还要求每个 `SHOW-C/SHOW-D` 事件恰好被一个 Showtime 关系拥有；每个 `DanceMoveDrill` 可以没有 Showtime 归属，但不能被多个 Showtime 引用。普通阶段从上一个 Showtime 最后一个 `SHOW-D` 事件行之后开始，舞步按首个 N 行排序；无归属舞步演练可以留在任意普通阶段，只要其事件行保持内部递增并排在所属阶段已引用舞步之后；不存在 Showtime 时全部舞步演练都属于同一个普通阶段。校验要求按兼容导出的写入顺序展平全部事件：每个阶段先写记录舞步演练的组与事件，再写该阶段其余无归属舞步演练，然后写 `SHOW-C` 与有序 `SHOW-D`，最后写末尾普通阶段；展平后的全部事件位置必须严格递增，且每个 Showtime 的 `d` 条目必须对应其阶段按行序最前面的一批舞步；最后一个 `SHOW-D` 的展示范围计算不能溢出 `uint64`。因此兼容导出只消费快照和关系顺序，不重新修复或猜测草稿结构。

规格与 UI 使用固定语义简称 `NPC-C`、`NPC-N`、`NPC-S`、`PLY-C`、`PLY-N`、`PLY-S`、`SHOW-C` 和 `SHOW-D`。这些简称是一对一的展示标签，不是持久化枚举字符串，也不能替代原生事件类型和显式角色。核心 API 应返回稳定类型和角色字段，由适配层映射展示简称；不得通过解析简称字符串执行领域操作。SLK 文件边界没有编辑简称或事件类型。

上下文放置规则需要由核心提供确定性的相邻事件查询。“上一个标记”在全部三列中查询位置严格小于点击位置的最近事件；“上一个本列标记”先按点击列过滤，再执行相同查询；“下一个标记”在全部三列中查询位置严格大于点击位置的最近事件。查询基于规范谱面时间位置，不依赖屏幕坐标、滚动方向或 UI 绘制顺序。当前位置不计入相邻查询；没有候选时返回显式空结果。

当前只读入口 `project_chart_markers(draft)` 把三个底层事件容器统一投影为按行和稳定事件 ID 排序的 `ChartMarker` 自有数组；列和 `NPC-C/N/S`、`PLY-C/N/S`、`SHOW-C/D` 类型均从权威角色/事件类型派生，不保存第二份列状态。`previous_chart_marker`、`previous_chart_marker_in_column` 和 `next_chart_marker` 只消费该数组，底层容器顺序不影响结果。`project_showtime_moves` 从相同全局顺序派生连续 `SHOW-D` 的首尾状态，并从自身 `showNCount` 生成展示范围；`chart_note_ordinal` 按同角色最近 `S` 边界派生 N 编号。所有结果拥有独立内存，不引用 `ChartDraft` 容器。

`ChartDraft` 对规范谱面时间行施加全局唯一约束：一个时间行在 NPC、玩家和 Showtime 三列合计最多拥有一个事件。该约束必须由 `av_core` 的所有上下文放置和自动生成命令统一维护，不能仅由 Flutter 命中测试或成品校验阻止。普通编辑的 `ChartEditor::click` 点击某列的空白区域但同一时间行已被其他列占用时，核心返回稳定的“位置已占用”结果，不替换已有事件，也不提交部分修改；选择命令按精确目标行命中，不受点击列限制。

自动生成器以命令开始时的 `ChartDraft` 快照规划并原子提交事件、数值快照、关系和一条历史，不建立观察器或额外级联命令。普通 NPC `click` 对源 N/S 的实际变化须在同一候选草稿中同步对应已有玩家动作，无法安全修正则清 N/S 保 C；删除 NPC-C 及演示内容须删除对应整玩家块。该限定行为由下文唯一核心同步规则负责。其他生成内容仍是快照：NPC 自动结束不随更早内容重新排布，Showtime 不重排、不刷新 `showNCount` 或展示范围。后续不匹配不能拒绝普通编辑，整舞步复制粘贴/剪切、加载恢复、undo/redo、只读查询和无关编辑不归一化玩家内容。内容失效的 Showtime、零角色事件的演练组和舞步在提交内清理；有效 Showtime、候选列表变化及其他允许的不匹配草稿继续保留，原生序列化与恢复逐值往返，只有 `FinishedChart` 和兼容 SLK 导出强制全部成品关系。

NPC 列上下文点击是 `av_core` 中的领域命令，不是 Flutter 条件分支。空白行命令接收规范时间位置，按功能规格的固定优先级查询前后事件并返回放置 `NPC-C`、`NPC-N`、`NPC-S` 或稳定错误码。无上一个事件时放置 `NPC-C`。`NPC-N` 的自动结束分支只统计点击位置之前、最近 `NPC-S` 之后的 NPC 列事件，跳过 `NPC-C` 和其他列事件；少于 11 个时放置 `NPC-N`，达到或超过 11 个时放置 `NPC-S`。若全局下一个事件是 `NPC-N` 或 `NPC-S`，则优先放置 `NPC-N` 并跳过数量限制。

点击已有 `NPC-C` 的领域命令删除自身及全局时间顺序中随后连续的 `NPC-N/NPC-S`，遇到其他事件停止，并删除命令前安全识别的对应整玩家 C/N/S 块（含只有 C 的块）。点击已有 `NPC-N` 时，如果当前组之后仍有同舞步演练的组，核心先为全部后继组创建一个新的 `DanceMoveDrill`，保持后继组和角色事件标识不变并原子改写其舞步引用，再把当前事件改为 `NPC-S`。空白点击达到 11 个 N 自动生成 `NPC-S` 时复用同一拆分入口，不能把结束标记写入仍带后继组的舞步中间。点击已有 `NPC-S` 时，如果当前组为当前舞步演练的末组、时间上的下一演练组属于另一舞步演练、两组间没有 Showtime 标记且没有属于其他演练组的 `NPC-S`，核心先把后继舞步的全部组及角色事件引用并入当前舞步并删除后继舞步对象，再删除当前事件；存在 Showtime 阶段边界或其他演练组的 `NPC-S` 时不得合并，删除 S 本身不要求末组。当前组仍含其他 `NPC-S`，或含位于被删 `S` 之后的 `NPC-N` 时也不得合并：中间 `NPC-N` 转为 `S` 再删除该 `S` 的取消操作必须还原原舞步，不能把一个舞步演练变成含两个已结束块的关系。两条命令重算受影响 `moveNCount`，只为本次动作序列实际变化的源块同步玩家内容，后继仅换归属的组不重建玩家动作。Flutter 只负责命中、发送稳定事件 ID 或空白行位置与展示结果，不扫描或实现领域判断。

玩家列上下文点击和自动生成同样属于 `av_core`。只有上一个全局事件为 NPC 列事件时，空白行点击才创建 `PLY-C`；上一个事件为 `PLY-C` 时，点击位置成为首个 `PLY-N` 的目标锚点；其他情况返回稳定的不可放置错误。生成器以当前 `PLY-C` 之前最近的 `NPC-C` 为源块开始，以两者之间的第一个 `NPC-N` 为源锚点，并读取从该锚点开始的全局连续 `NPC-N/NPC-S`。各目标行通过源行相对源锚点的整数 1/16 行差平移得到，事件类型按 `NPC-N -> PLY-N`、`NPC-S -> PLY-S` 映射。找不到源锚点时返回稳定错误，保留 `PLY-C`，不生成部分结果。

玩家首次生成器必须先计算完整目标事件列表及首尾闭区间，再检查该区间内三列的任何已有事件。区间内任一行已占用时，整个首次生成事务失败；不得仅检查目标落点，也不得提交无冲突的子集。成功时，事件创建、来源关系建立和单条历史记录一次提交。失败时不改变事件、关系或历史，但此前独立存在的 `PLY-C` 保持不变。首次生成的源锚点查找规则保持上文定义，不能套用下面同步时禁止跳过首 S 的规则。

普通 NPC 点击同步只有一份 `av_core` 实现，并在 NPC 操作前捕获对应块、动作与偏移。对每个 `PLY-C`，以修改前最近的前置 `NPC-C` 稳定身份匹配来源，玩家成员为该 C 后全局连续的 PLY N/S，遇到任何其他事件停止；不能仅凭 `drill_group_id` 删除或替换全部玩家事件。同组多 C 独立规划，关系数组缺项的旧草稿只处理显式角色、标记身份和连续范围能安全确定的成员。NPC-C 与 PLY-C 的事件自身组或舞步引用不一致时跳过该块；连续玩家动作的事件自身引用与 PLY-C 不一致时停止收集，保留该动作及后续内容，避免吞并其他舞步。只有 C 的玩家块跳过同步，主动清空后也不因 NPC 后续编辑自动填回；对应 NPC-C 删除时则删除该 C 及其整连续玩家动作。

对实际改变源 N/S 的块，以命令前首 PLY-N 减命令前首 NPC-N 得到每块独立的整数 `oldDelta`，将新源序列每一行映射为 `newNpcRow + oldDelta`；差与加法使用显式边界检查，不能在无符号整数中回绕，也不经过浮点或界面格式化。新源为对应 NPC-C 后全局连续 NPC N/S，可以从 N 或 S 开始；首 S 与后续 S 一样按偏移映射，不因其位于首端而清空。缺 S、超过 11 个 N、S 后 N 和多 S 都可映射，不调用成品校验筛掉这些草稿。NPC `N10,N14,S18`、PLY `N30,N34,S38` 前插 `N8` 的新 PLY 必须为 `N28,N30,N34,S38`，不能固定旧首玩家行。

每块新首尾闭区间检查三列全部占用，只排除本块待替换旧 N/S；PLY-C、本次其他块及无关事件不属于排除集，不覆盖任何无关标记。不能安全映射时清空该块全部旧 N/S 保 C，包括源为空、无可用旧玩家首 N、目标 `<= PLY-C`、行下溢/上溢、完整范围冲突及新增玩家 ID 耗尽；各块独立决定同步或清空。玩家 ID 耗尽允许安全清空，但为失败替换试分配的 ID 不得消耗。分配内存、NPC 原动作或最终 commit 失败必须回滚整个命令，不能提交局部同步或假报清空成功。

替换时按旧玩家 N/S 的时间顺序尽量复用稳定事件 ID，新增部分才分配新 ID，移除多余事件与其引用；事件及关系数组保持时序，C 的位置与 ID 不变。候选事务固定按 NPC 变更及既有拆分/合并、玩家同步/清空/对应整块删除、空组清理、失效 Showtime 清理、空舞步清理、一次 commit 的顺序执行。Showtime 完整性只看玩家处理后的最终内容：引用舞步仍完整、各 `showNCount` 匹配且已记录舞步仍是所在普通阶段完整舞步中的连续一段时保留原事件、排布和快照；引用了已失效舞步、count 不匹配，或在已记录舞步之间插入了新的完整舞步演练时删整段，不能刷新 count 或重排以修复。只变归属的后继组不参与玩家重建。undo/redo 恢复精确快照，不重新运行同步；加载恢复和批量快照落点也不运行同步。

核心成功结果附带 `player_actions_cleared` 布尔值：至少一个块因修正失败而实际清掉已有 N/S 时为 true，正常同步、仅 C 跳过、对应整玩家块删除及失败均不作此清空报告。Flutter 原样消费该结果并显示非阻塞提示 `演示已修改；对应练习无法自动修正，已清空练习 N/S，保留起始标记。`，不根据前后计数自行推断。C ABI 通过追加的 `otgy_chart_editor_click_with_effects` 暴露同一点击事务，以独立 `uint8_t` 输出清空标志，保留旧 `otgy_chart_editor_click` 签名及 `otgy_chart_edit_result` 布局。两个输出在参数校验前置零。C ABI 对点击和选择的 `uint32_t` 列值先作不回绕的转换，未知值不得窄化成合法列触发编辑或选择；核心继续决定无效列的命令结果。C ABI、Dart 和 Flutter 不复制同步、冲突、成员识别或降级算法。

点击已有 `PLY-C` 会原子删除自身及随后全局连续的 `PLY-N/PLY-S`。点击上一个全局事件为 `PLY-C` 的首个 `PLY-N` 时保留 `PLY-C`，原子删除从该事件开始的连续 `PLY-N/PLY-S`，用于重新定位；其他 `PLY-N` 和所有 `PLY-S` 返回自动生成事件不可编辑错误。多事件删除必须清理失效关系，但不得级联删除 NPC 或其他存活事件，引用舞步因此失效的 Showtime 按上文一次性生成器例外整段删除。Flutter 不得自行复制间隔、检查生成区间或判断首个 `PLY-N`。

Showtime 列点击、位置验证、舞步收集和自动补全全部属于 `av_core` 领域命令。`PLY-S` 后的空白点击创建 `SHOW-C`；小节线上的 `SHOW-C` 使用下一 1/16 位置作为自动补全起点，非小节线 `SHOW-C` 使用后续第一条小节线。已有 `SHOW-C/SHOW-D` 后的空白点击只在位置验证成功后作为补全起点。其他上下文返回稳定不可放置错误。

补全器从所属 `SHOW-C` 后已有连续 `SHOW-D` 数量确定舞步索引，并取得上一 Showtime 阶段结束后到当前 `SHOW-C` 前、按时间正序排列的完整 `DanceMoveDrill`。不完整演练不进入候选。扣除已有数量后，剩余列表为空时返回稳定错误；否则为所有剩余舞步创建显式引用，把生成当下的 `moveNCount` 复制为各自的 `showNCount`，并由该快照派生 `d` 值、时长、展示范围和后续行位置。提交后不再从关联舞步刷新这些结果。

补全在提交前按候选顺序计算锚点，并找出当前 `SHOW-C` 之后时间最早的其他标记（忽略所属 `SHOW-C` 与该 Showtime 已有 `SHOW-D`）。只要下一个锚点早于该标记就继续生成，到达或越过该标记时停止，只提交能放下的前缀；其余候选保持未展示。实际只生成前缀时结果码为 `ShowtimePartiallyCompleted` 且 `changed = true`；App 据此显示只读的部分生成提示（单一 `知道了` 确认按钮），生成结果保留，核心不提供部分回滚命令。展示范围只是视觉提示，标记位于最后锚点之后时不阻止生成，即使落在某个展示范围内。第一个锚点即被挡住时不生成任何 `SHOW-D`；新建 `SHOW-C` 后补全失败时只保留该 `SHOW-C`，手动补全失败不修改草稿。Flutter 只绘制核心返回的范围与结果，不得重新计算候选、位置或数量上限。

NPC 空白点击命令在创建 `NPC-C` 前查询核心维护的 `SHOW-D` 展示范围。通常命中范围即拒绝；只有命中某个连续 `SHOW-D` 序列最后一个事件的 `[rangeEnd - 8, rangeEnd)` 时允许继续执行普通 NPC-C 上下文规则。范围端点 `rangeEnd` 本身不需要例外。该判断不能由 UI 像素命中代替。

点击已有 `SHOW-C/SHOW-D` 的命令原子删除自身及随后全局连续的 `SHOW-D`，遇到其他事件停止，并同步清理失效引用。

当前可撤销领域入口为拥有单一 `ChartDraft` 的 `ChartEditor`。`click(column, row)` 先消费统一 `ChartMarker` 投影执行全局单行占用检查，再在候选草稿副本中完成 NPC、玩家或 Showtime 的全部事件与关系变化；只有完整结果可提交时才替换权威草稿。每次成功点击、整批生成、整段删除或清空把修改前的完整草稿作为一条历史，撤销/重做因此逐值恢复事件、稳定标识、显式角色和全部关系；失败不创建历史，新修改清空重做分支。历史和会话内标识分配状态不进入项目持久化。

`ChartEditor::place_marker(column, row, type)` 是桌面显式标记键的唯一领域入口：只接受 NPC 列和 `BlockStart/Note/MoveEnd` 三种事件类型，类型来自命令而不是上下文推导。空行直接写入该类型并复用 `click` 的结构校验（`SHOW-D` 展示范围、11 个 `N` 的追加上限、结束标记要求组内至少一个 `N`、后继组拆分）；同一行已是同类型时转交 `remove_marker`，其他占用行返回稳定的占用结果，不做替换。`ChartEditor::remove_marker(column, row)` 只删除该行的一个 NPC 事件：`NPC-C` 不级联、不同步玩家；`NPC-N` 与 `NPC-S` 复用既有玩家同步，`NPC-S` 还复用既有后继舞步演练合并。被单独删除起始事件的组保留标识与内容；新的 `NPC-C` 与最近的这类组之间没有任何其他标记时，`place_marker` 把起始事件写入该组而不是新建组和舞步。两条命令都用候选草稿副本、单次 `commit` 和一条历史，失败不改权威草稿或历史。C ABI 以追加的 `otgy_chart_editor_place_marker` / `otgy_chart_editor_remove_marker` 暴露同一事务，并像点击一样输出 `player_actions_cleared`；Flutter 只把改绑后的键解析为判定线行并调用同一控制器命令。

编辑器创建 `NPC-C` 时同时创建或延续显式 `DrillGroup`/`DanceMoveDrill`：上一个 `NPC-C` 到本次新 `C` 的行区间内没有 `NPC-S`、该上一个 `NPC-C` 自身所在组不含 `NPC-S` 且舞步未被 Showtime 使用时，新组延续该舞步；否则开始新舞步。新 `C` 之后更晚行上的 `S` 不属于该区间，不得阻止延续；但该 `S` 属于上一个 `NPC-C` 自身所在组时视为该组已结束，必须开始新舞步，否则结束标记会落入非末尾演练组。NPC `N/S` 与玩家生成内容直接加入对应关系。NPC 修改重算受影响舞步的 NPC `N` 总数，并按上述限定规则同步或清空对应玩家动作，删除 NPC-C 时删除对应整玩家块。S 边界拆分/合并只改写后继存活组和事件的舞步演练引用，后继稳定标识和动作不变。删除须清理已删引用；其他允许不完整的存活内容保留。完成玩家处理后依次清理空组、失效 Showtime 和空舞步，再一次提交；零角色事件以事件自身引用判断，不能因关系数组缺项删除仍有事件的组或舞步。

正式草稿和恢复快照加载后由 `ChartEditor::repair_relations` 做一次加载期关系归一化：按角色事件上的 `group_id` 与可见 NPC C/N/S 行序归组，NPC `MoveEnd` 关闭当前舞步；每个新舞步复用组集合完全相同的旧舞步标识，其余分配新标识，重算 `moveNCount` 并清理空对象。失效 Showtime 用同一补全入口在原 `SHOW-C` 行重建，无法补全时整段删除。归一化实际改写任意演练组的舞步归属时，已记录舞步不再是当前完整舞步候选列表有序前缀的 Showtime 也在原 `SHOW-C` 行重建，使补全覆盖修复后全部完整舞步；重建在草稿副本上尝试，失败时保留原 Showtime 内容。归属没有变化时不改写 Showtime。该操作不进入撤销/重做，正确草稿返回 `Unchanged`，重复调用必须幂等。C ABI 以 `otgy_chart_editor_repair_relations` 暴露 `changed`；App 在正式草稿和恢复快照解码后调用，并在变化时立即原子覆盖相同 `draftId` 的正式草稿或当前恢复记录。SLK 严格导入不调用该入口。

Showtime 补全只选择上一 Showtime 最后一个 `SHOW-D` 事件行之后、当前 `SHOW-C` 之前按关系顺序完整且 NPC/玩家块匹配的 `DanceMoveDrill`；展示范围终点不参与阶段归属，候选边界按舞步首个 N 行判断，`NPC-C` 不计入起点，因此位于最后半小节开放区间内的舞步可以直接作为下一段开始。已有 `SHOW-D` 必须对应候选前缀，生成器从余下候选复制当前 `moveNCount` 为独立 `showNCount`，按更晚内容允许的数量预先计算锚点并一次提交。新建 `SHOW-C` 时 Showtime 关系按起点行保持全局时间顺序；删除后重建较早的 Showtime 插回对应位置，不追加到既有较晚 Showtime 之后。新建 `SHOW-C` 后没有候选或首个锚点已被更晚内容挡住时只提交 `SHOW-C` 的单条历史；手动补全的相同失败不修改草稿。已有 `SHOW-D` 与候选列表的对应关系在补全前显式校验：记录的舞步数量与 `SHOW-D` 数量不相等，或记录的舞步不是候选列表的等长前缀时，返回稳定的内容不一致错误而不是笼统的草稿错误；这类 Showtime 仍然保留在草稿中，可以删除后重新放置 `SHOW-C`；恢复候选前缀与数量一致性后也可继续补全。每次编辑提交还会比较编辑前后：如果本次新增或补完了首个已记录舞步之前的完整舞步，使记录列表不再是阶段候选前缀，该 Showtime 随本次编辑删除；仅由删除或改动更早 Showtime 造成的边界变化保留为不完整草稿，重新放置更早的 Showtime 即可恢复。

Flutter 根据 `ChartDraft` 和校验结果生成展示投影。每个被绘制、选中或编辑的角色敏感项目都必须从模型取得显式角色。视觉样式可以区分 NPC 和玩家，但颜色、轨道、顺序或组件位置不得成为角色真值来源。展示投影不是第二份谱面模型，也不得重复导入、分组或导出算法。

SLK 导入比原生项目加载更严格。它会拒绝文件级解析错误、两种位置表示不一致、不支持的第 5/6 列组合，以及任何违反成品谱面语义规范的情况。它绝不会把可解析但不完整的 SLK 行转换成原生草稿。未来出现的未知行类型，在规格明确采纳之前一律不支持。

谱面校验不属于音频操作的前置条件。即使谱面草稿为空或无效，音频解码、波形生成、时序调整、播放、分析和 Ogg 导出仍然可以工作。

## 原生草稿存储与异常恢复契约

正式草稿和异常恢复快照都使用版本化的原生项目序列化容器，但采用不同负载、身份和生命周期：

```text
FormalDraftPayload = { chartDraft }
RecoveryPayload    = { chartDraft }
```

正式草稿中的 `ChartDraft` 必须完整保留谱面事件、稳定对象标识、显式角色、语义关系和 `SHOW-D.showNCount` 等谱面字段。BPM 和 Offset 属于当前音频会话，不写入正式草稿。正式草稿不包含音频引用或访问凭据、BPM、Offset、规范 PCM、波形、分析缓存、来源音频元数据、品质上限、当前时间光标、播放状态、Zoom、Scale、波形可见性、撤销/重做历史、`WorkspaceSource` 或其他会话与视图状态。

正式草稿使用稳定 `draftId`，显示名称不是存储主键。`draftId`、显示名称、格式版本和完整性校验属于容器或索引元数据，不是额外工作区负载。同名检查使用规范化后的显示名称；名称规范化、最大长度和平台文件名映射的具体实现必须集中定义，不能由各平台分别决定。

正式草稿保存通过“完整临时文件写入、刷新、原子替换”的方式发布。任何失败都必须保留上一份有效正式草稿和当前内存工作区。草稿列表读取正式草稿索引，默认按最后修改时间降序排列；恢复快照不进入该索引。

App 私有存储中最多存在一个恢复快照。每次已提交的谱面修改都会调度 `500` 毫秒防抖更新；进入后台和正常退出回调会请求立即刷新，但诊断文件选择器活跃期间的 `paused`/`hidden` 不额外刷新，既有防抖和真正 `detached` 退出处理继续独立运行。恢复快照只保存版本化的 `ChartDraft` 谱面数据，明确不使用正式草稿的 BPM/Offset 负载。写入通过临时文件发布新的不可变记录，再删除旧代，因此崩溃最多丢失尚未刷新的最新修改，不能同时破坏上一份有效快照。

恢复快照不保存 `WorkspaceSource`、正式草稿 ID、音频引用、平台授权凭据、BPM、Offset、规范 PCM、波形图块、分析缓存或视图状态。异常恢复只还原谱面数据，不恢复任何音频或界面会话状态。

会话状态使用独立的启动/正常结束标记。上次会话未正常结束且存在有效恢复快照时，App 才提示恢复。恢复操作把谱面数据加载为 `source = Untitled`、`dirty = true` 的当前工作区，避免自动覆盖任何正式草稿；放弃恢复删除快照。正式保存成功且工作区已干净、或用户在正常退出流程中明确放弃修改时，删除恢复快照。损坏或版本不支持的恢复快照不得替换当前工作区。

恢复快照写入和文件系统原子发布属于 App/平台存储层；`ChartDraft` 的版本化编码、解码和完整性校验由共享核心实现。任何平台都不得自行定义另一套恢复数据模型。

当前共享核心编码入口为 `encode_formal_draft` 与 `encode_recovery_snapshot`，对应严格解码入口为 `decode_formal_draft` 与 `decode_recovery_snapshot`。两者使用内部 `OTGYPRJ\0` version 2 二进制信封：固定小端版本、负载类型、零 flags、精确负载长度和负载 CRC32，之后按六个原生容器的当前顺序编码全部事件、稳定标识、显式角色、关系与 `showNCount`。解码器先校验信封类型、长度和完整性，再检查枚举/计数/剩余字节；成功值拥有全部内存且不引用输入。version 1 正式负载仍可读取，其 BPM/Offset 前缀会被校验后丢弃。该内部信封不决定未来用户可见文件扩展名。

正式负载与恢复负载都只包含 `ChartDraft`，编码/解码只验证容器安全和字段表示，不调用成品校验器，因此零关系、缺失块、角色不匹配或其他允许的不完整草稿仍可逐值往返。名称、`draftId`、修改时间等索引元数据由上层正式草稿容器管理；音频、BPM、Offset、授权、PCM、波形、缓存、来源、播放/视图状态、历史及 `ChartEditor` 的会话内下一标识均不进入任一负载。

当前 `LocalProjectStore` 在 App 私有支持目录使用 version 1 JSON 外层和 `.otgydata` 不可变记录保存索引元数据，核心二进制负载以 Base64 携带。每次保存先完整写入并刷新临时文件，再在同目录发布新代；读取时忽略截断、损坏或未知版本记录，并保留仍有效的上一代。该外层只是平台存储适配，不解释或重写核心负载。

## 本地诊断与验收构建契约

诊断属于 App 基础设施，不引入新的领域实现。`OTGY_DIAGNOSTICS` 是默认 `false` 的编译期开关；关闭时不初始化详细日志目录或写入队列，开启时在私有支持目录保存 JSONL。初始化、记录、轮换、读取或导出日志失败不得抛入编辑流程或吞掉原有异常处理；失败状态即使没有可写目录也必须能够进入主动导出的诊断包。整个流程不访问网络。

`LocalDiagnostics` 的生产上限是 `4` 个轮换文件，每文件 `262144` 字节，等待写入队列 `128` 条，单条 JSONL UTF-8 记录含换行最多 `16384` 字节。写入顺序按会话和序号保持，先检查字节上限再轮换，不拆出半条 JSON 记录。超长记录通过缩减字段并标记 `truncated`/`originalBytes` 保持合法 JSON；无法容纳或队列满时增加 `droppedRecords`。写入与导出读取串行化；读取损坏行时跳过并增加 `corruptLines`，存储错误以 `storageFailed` 和脱敏 `failure` 保留。日志队列、轮换与错误统计不得依赖 `LocalProjectStore`。

`main` 捕获 Flutter 框架错误和平台异步错误时先记录脱敏异常，再链接原处理器；已有处理器的返回语义与默认错误报告必须保留，诊断自身失败不得导致递归报告。应用控制器通过白名单记录命令 `code`、`changed`、明确允许的参数和记录时的单份 `state`；正常编辑在更新投影和修订号后记录操作后状态，阻止或异常路径记录当时状态，保存与恢复路径也记录结果。状态包含谱面计数、修订号、dirty、来源类型、BPM/Offset 数值、音乐结束模式与行、当前行、undo/redo、交互模式、选择与快照数量、待剪切意图、是否有音频和剪贴板，但不得转储任意控制器对象、选择成员、对象标识、来源名称、草稿 ID、文件名、完整路径/URI、音频或任何剪贴板负载。粘贴命令与预览事件只记录数值锚点和稳定结果码，不记录幽灵标记或批量快照。新异常记录只保留类型、原生 domain/code/status、OS 数值代码、白名单平台代码与脱敏堆栈，不读取异常 message、details 或调用 error.toString()；非白名单平台代码写为 `[redacted code]`。历史 `exception` 仅允许 `operation`、`type`、`domain`、`code`、`status`、`osCode`、脱敏 `stack` 与严格白名单 `state`；`state` 只保留整数 `revision/markerCount/showtimeMoveCount/selectionCount/clipboardCount/pastePreviewAnchorRow/pastePreviewCode/audioEndRow`、有限数值 `bpm/offset/currentRow`、布尔 `dirty/hasAudio/canUndo/canRedo/hasSelection/hasClipboard/cutPending`、已知枚举名 `sourceKind`、`interactionMode`（`normal/selection/pastePending`）和 `audioEndMode`（`sourceEnd/beat`）。未知键和类型不匹配字段丢弃，不允许任意嵌套文本。安全代码仍须遵守相应类型和平台代码白名单，未知异常原文丢弃。通用脱敏递归清理敏感键 `message`、`details`、`error`、`exception`，未知对象与非字符串 key 不调用 `toString()`。

主动导出的版本 1 单 JSON 包使用以下字段契约：

- `schemaVersion = 1`，`session` 为不含用户路径的会话标识；有 logger 时沿用其会话，无 logger 的 fallback 为本次导出生成会话。`capturedAt` 为复制恢复字节与状态时取得的 UTC 时间，在日志 `flush` 前记录；`createdAt` 为组包时取得的 UTC 时间，两个字段均保留。
- `identity` 包含 `appVersion`、`buildId`、`buildTime`、`coreVersion`、`apiVersion`、`operatingSystem`、`operatingSystemVersion`、`buildMode`；验收包的 `buildMode` 为 `release`。
- `captureState` 是白名单状态摘要，不包含来源/草稿名称、路径、音频或剪贴板内容。
- `snapshot = { encoding: "base64", kind: "recovery", payload: "…" }`，`payload` 是当前 `NativeChartEditor.encodeRecovery` 返回字节的原样 Base64；该入口最终调用唯一的核心 `encode_recovery_snapshot`，不经过 SLK、不重新序列化字段、不读磁盘恢复快照、不调用成品校验。
- `logs` 是按日志文件轮换与记录顺序汇集、再次执行通用脱敏的日志记录；`corruptLines`、`droppedRecords`、`storageFailed`、`failure` 描述采集限制与失败。`detailedDiagnosticsEnabled` 是编译期开关；`initializationFailure` 是可空的安全错误字段 JSON 字符串，`failure` 优先报告 logger 存储错误，否则报告初始化错误。普通构建与验收构建均提供导出菜单；无 logger 时调用 `snapshotBundle`，保留身份、状态与谱面，`logs` 为空、丢弃与损坏计数为零，初始化失败由上述字段明确表示。仅关闭日志不令 `storageFailed` 为 true。

只有用户主动导出时才捕获 `snapshot`，日常日志不保存谱面负载；包不包含音频、系统或编辑器剪贴板、私人完整路径/URI、文件名或草稿名。空草稿和无 store 会话均可编码。捕获/发布属于只读操作，不改 dirty/source/revision/history/selection/clipboard，不调用 store 的恢复写入、清理或会话标记。诊断文件选择器的 `paused`/`hidden` 只抑制额外的生命周期恢复刷新，已有 `500` 毫秒防抖继续独立运行，`detached` 仍执行真实退出处理。

JSON 发布复用 `io.onetwogoyeah/files`：桌面在同名 `.json` 或发布瞬间竞争时重试可用名称，最终发布必须不替换；移动端原样分块写入后 publish/commit，在 `finish` 返回后及 `commit` 前检查取消；失败或取消时对未提交文档执行 abort 并尝试删除，失败清理必须在显示错误弹窗前尝试完成。适配层只处理字节、目标与所有权，不理解包内谱面。导出遵守一般操作门控和 `!isBusy`，并单独防止重复导出。

`scripts/build-acceptance.py` 使用 Python 标准库生成并运行验收 Release 命令，只支持 `apk`、`macos`、`windows`、`ios`，不安装或操作设备。脚本从实际 `app/pubspec.yaml` 的唯一顶层 `version` 读取版本，同时传递对应 `--build-name`、`--build-number` 与 `OTGY_APP_VERSION`；注入 `OTGY_DIAGNOSTICS=true`、`OTGY_BUILD_ID`、`OTGY_BUILD_TIME`。构建 ID 由完整 git commit、clean/dirty 状态和 UTC 时间构成，构建时间采用 UTC ISO 8601；身份字段不包含本机用户名、工作目录或 SDK 路径。

脚本默认优先使用仓库缓存的 `build/flutter-sdk/bin/flutter`（Windows 为 `flutter.bat`），否则使用 PATH 中的 Flutter；`--flutter` 可指定已有 SDK。固定 `--release`、`--no-pub`、禁用 analytics 与版本检查，要求依赖和平台工具预先缓存，不自动解析依赖或下载 SDK。`--dry-run` 只读取本地版本和 git 身份并输出实际命令 JSON，不运行 Flutter。APK 目标固定追加 `--target-platform=android-arm64`；`--` 后仅允许 iOS 的 `--no-codesign`，`--split-per-abi`、`--target-platform` 及其他打包参数一律拒绝，模式、版本、全部 dart defines/define 文件、配置与目标入口覆盖同样拒绝，防止替换诊断开关或身份或产出非 arm64 包。常规构建仍默认关闭详细日志。

`scripts/build_android_release.py` 是面向 64 位 ARM 真机的普通 Release 入口：从同一 pubspec 版本构建 `--target-platform android-arm64`，不接受任何 Flutter 参数覆盖；构建后校验 APK 的 `lib/` 只含 `arm64-v8a`，再复制为 `build/releases/OneTwoGoYeah-<version>-arm64.apk` 并打印 SHA-256。32 位与模拟器 ABI 不进入发布流程。

## 平台边界

平台代码仅负责：

- 获得授权的文件访问和文件选择器集成
- 音频设备输出
- 窗口、键盘、指针和触控输入
- 生命周期与应用存储路径
- 向 C ABI 传入和取出数据

macOS Runner 保持 App Sandbox，并在 Debug/Profile 与 Release 签名中声明 `com.apple.security.files.user-selected.read-write`，只通过系统文件选择界面获得用户明确选择的文件访问权。该权限不允许扫描任意目录，也不改变产品完全离线的边界。

平台播放适配层消费共享核心生成的帧，不得重新实现速度、混音、裁剪、节拍时序或重采样规则。

iOS `AVAudioEngine` 连接只接受引擎标准的 Float32 非交错格式。iOS Runner 在不改变采样率、帧数、声道或时序的前提下，把 MethodChannel 收到的交错 PCM16LE 逐样本转换为等价的双声道 Float32 非交错缓冲，再用该标准格式连接 `AVAudioPlayerNode`；设备采样率适配仍由 `AVAudioEngine` 完成，Runner 不实现重采样算法。播放位置继续以转换前后完全相同的源帧编号返回。

iOS 文件提供器可能把未知 `.slk` 文件报告为只遵循 `public.data` 的动态 UTI，因此 Flutter 在 iOS 请求 `public.data`，并在文件返回后、读取前强制校验 `.slk` 扩展名。该平台兼容处理只负责选择范围与输入门控，不解析或改写 SLK；严格解析和语义校验仍只调用共享核心。

Android `file_selector` 会把授权的 `content://` 输入复制到 App 缓存，并可能依据系统 MIME 把原始 `.ogg` 名称改成 `.oga` 或无扩展名。Flutter 只在系统 MIME 明确属于 Ogg Vorbis 或 MP3 时，把送入核心分派器的缓存名称规范化为 `.ogg` 或 `.mp3`；未知 MIME 和不受支持的显式扩展名仍在读取前拒绝。该适配不嗅探或解析音频内容，最终容器及字节有效性仍只由 `av_core` 的对应解码器决定。

同一 Android 缓存复制行为会把文件提供器报告为 `application/octet-stream` 的已选 `.slk` 文档改名为 `.bin`。Flutter 在专用 SLK 选择流程中只把这个通用 MIME 对应的缓存逻辑名称恢复为 `.slk`，使其进入唯一的 `import_finished_slk` 严格事务；其他 MIME 仍在读取前拒绝，恢复文件名不解析、不修复也不信任内容，任何非规范或不完整字节仍由共享核心原子拒绝。

Flutter 与 Runner 使用三个轻量 MethodChannel：`io.onetwogoyeah/playback` 只传递采样率、交错 PCM16LE 和已播放帧数；`io.onetwogoyeah/files` 对路径型目标在同目录把完整临时文件以原子、不替换硬链接发布到目标，macOS 再以 `renameatx_np(RENAME_EXCL)` 作为硬链接不可用时的原子回退，并在用户选定路径不允许创建临时兄弟文件时使用原生 `O_EXCL` 写入和失败清理；Android 通过同一文件通道发起 `ACTION_CREATE_DOCUMENT`、持有 `content://` 写入会话、原样分块写入以及失败删除，iOS 通过该通道导出空占位文档取得安全作用域 URL、持有文件句柄、原样分块覆盖以及完成释放或失败删除；`io.onetwogoyeah/lifecycle` 只用于 Windows/macOS 窗口关闭前请求 Flutter 完成未保存确认。Android 使用 `AudioTrack`/`ContentResolver`，iOS 使用 `AVAudioEngine`/安全作用域文件 URL，macOS 使用 `AVAudioEngine`/POSIX 文件操作，Windows 使用 `waveOut`/`CreateHardLinkW`。这些适配层不得解释音频时序、文件名或项目语义。

## 音频兼容契约

初始兼容目标是已检查的 ArrowVortex 源码树。

解码依赖固定为与该参考树相同的仓库内源码快照：libmad `0.15.1 beta`、
ArrowVortex 随附 libogg 快照（公开 `ogg.h` 修订 `18044`，源文件修订最高
`19149`）、libVorbis `1.3.5`。源码、许可证和
内容摘要保存在 `native/third_party`，常规配置、构建和测试不得从网络下载依赖，
也不得搜索或链接系统安装的同名库。libmad 统一定义 `HAVE_CONFIG_H`、
`FPM_64BIT`、`ASO_ZEROCHECK`，三个依赖目标都明确禁用 `fast-math` 并构建为
位置无关静态库。当前 libVorbis 目标同时包含核心、`vorbisfile`、
`vorbisenc.c` 与固定的编码模式/码本表；解码和编码均只链接该仓库内快照，
不搜索平台或系统编解码器。

- MP3 使用固定版本和配置的 libmad。
- Ogg Vorbis 使用固定版本和配置的 libogg/libvorbis。
- 规范解码音频为平面双声道有符号 16 位 PCM，并保留原始采样率。
- 单声道输入复制到左右两个声道。
- 超过两个声道的输入只使用前两个声道，与参考行为一致。
- 解码时不自动重采样。
- BPM 分析使用与 ArrowVortex 相同的运算和舍入方式，把双声道 PCM 转为单声道。
- 固定并记录解码器版本、编译定义和相关优化标志。
- 兼容性结果绝不使用平台原生解码器。

当前 MP3 权威入口为 `av_core` 的同步 `decode_mp3(data, size)`。它只接收调用方
拥有的内存字节，不接收路径，不保留输入别名；libmad 只作为 `av_core` 的私有
链接依赖，公共头不暴露第三方类型。入口识别参考实现处理的前导 ID3 标签和首个
Xing 帧，保留 Info 帧的参考行为，并拒绝同一输入中途改变采样率或单双声道布局。

libmad 定点样本严格按参考顺序转换：先加
`1 << (MAD_F_FRACBITS - 16)`，再限制到
`[-MAD_F_ONE, MAD_F_ONE - 1]`，最后右移
`MAD_F_FRACBITS + 1 - 16` 位得到 PCM16。单声道转换后的同一样本分别写入 Left 和
Right；双声道保持原始左右顺序。成功结果才构造并返回 `Pcm16`。空指针、空输入、
libmad 长度上限、无可解码完整帧、中途格式变化、尺寸溢出和分配失败都有稳定的
`Mp3DecodeError`；即使错误发生前已经解码帧，失败结果也不包含部分 PCM。
成功结果还携带 `AudioSourceInfo`：编解码器固定为 MP3，来源码率按每个已解码
音频帧产生的样本数对帧头码率加权求平均，首个 Xing 非音频帧不进入分子或分母。

当前 Ogg Vorbis 权威入口为同步 `decode_ogg_vorbis(data, size)`。它同样只接收
调用方拥有的内存字节，不接受路径或保留输入别名；libogg/libVorbis 是
`av_core` 私有依赖，公共头不暴露第三方类型。入口先用 libogg 流式校验全部物理
页、校验和、末页边界与 EOS，再以支持读、定位和查询位置的内存游标调用
`ov_open_callbacks`。因此损坏页、残留不完整末页以及缺少 EOS 的截断输入不会被
libvorbisfile 的宽容扫描误当作合法零帧音频。

样本转换直接使用参考实现的 `ov_read(..., 0, 2, 1, ...)`，即 libVorbis 固定的
小端有符号 PCM16 舍入与削波路径；核心显式解析返回字节，所以结果不依赖主机
字节序。单声道样本复制到 Left/Right，双声道保持通道 0/1 顺序，更多声道也只
选择通道 0/1，不混音或重采样。物理 Ogg 中的串联逻辑流只有在采样率和声道数
始终相同时才可解码；格式变化返回 `StreamFormatChanged`。空指针、空输入、内存
游标范围、容器/编解码错误、格式变化、尺寸溢出和分配失败由稳定的
`OggVorbisDecodeError` 区分，任一失败结果都不包含已经暂存的部分 PCM。
成功结果的 `AudioSourceInfo` 保留 Ogg Vorbis 流头标称码率和 `ov_bitrate` 测得
码率；标称值无效或串联流不一致时置零，测得值不可用时才按完整容器位数与实际
PCM 时长回退计算。

规范 PCM 的诊断哈希同样由 `av_core` 唯一实现。`pcm16le_sha256_hex(pcm)` 按
Left 全部帧后接 Right 全部帧的顺序，把每个有符号样本投影为恰好两个小端字节，
再返回 64 个小写 ASCII 十六进制字符。该字节投影的稳定格式标识为
`pcm16le-planar-left-right`。采样率和帧数不进入哈希输入，由调用方作为独立字段
报告；主机字节序、区域设置和平台文本编码都不能改变哈希。

当前 `avtool decode <audio> --pcm-hash` 是该解码能力的轻量文件适配层。CLI 只按
大小写不敏感的 `.mp3`/`.ogg` 扩展名选择上述现有权威入口、以二进制读取完整
文件，并输出一行固定 LF 结尾的 ASCII JSON；它不探测或修复容器，不实现另一份
解码、通道选择、PCM 投影或 SHA-256。成功 schema v1 的字段顺序和含义固定为：

```json
{"schemaVersion":1,"codec":"mp3","pcmFormat":"pcm16le-planar-left-right","sampleRate":8000,"frameCount":9216,"pcmSha256":"b1d85a34af1e9e16f7530f6efdbde26e3f8e5e5a71fc8c7345aed2345a77a451"}
```

`codec` 只允许 `mp3` 或 `ogg-vorbis`；其余字段直接来自规范 PCM 和核心哈希。
成功时只写 stdout。参数错误返回 `2` 并显示用法；未知扩展名或文件读取失败返回
`3`；权威解码器拒绝输入时返回 `4`；未预期异常或输出失败返回 `5`。后三类运行
错误保持 stdout 为空，并在 stderr 输出只含 `schemaVersion` 和稳定 `error` 码的
单行 JSON。所有数字使用经典 C 区域设置的无分组十进制 ASCII；Windows 也把
机器输出流设为二进制，避免换行转换。

大文件和移动端内存优化必须在测试保护下引入。它们可以改变存储和缓存方式，但不能改变可观察的解码 PCM 或算法结果。

替换音频必须是事务性的，并且只能在非播放状态开始。播放中到达应用操作协调层的替换请求返回稳定的 `PlaybackActive` 拒绝结果，不打开选择器、不启动解码，也不修改状态。所选文件先解码到临时、未发布状态，再从临时规范 PCM 生成完整的编辑密度波形和播放密度波形；这些步骤都不得修改当前音频、文件名、缓存、波形、BPM、Offset 或其他编辑器状态。取消平台文件选择、解码失败或任一波形生成失败时，丢弃全部临时工作并保持当前状态不变。

只有解码和两种密度的波形全部成功后，才取消并等待上一音频版本的分析任务结束或以安全方式解除关联，然后原子发布新音频、规范 PCM 和波形缓存。提交事务必须清除上一音频版本的分析任务与候选、来源码率、品质上限、元数据以及其他派生缓存，再仅从新来源建立新音频对应的信息；同时把权威 BPM 重置为精确 `100`，把权威 Offset、当前时间光标和播放位置重置为精确的零，把音乐结束线重置为默认，并保持暂停。默认 BPM 和 Offset 立即重建时序网格。每个异步结果都携带音频版本；即使协作取消较晚才完成，也必须丢弃过期结果。提交后绝不能复用上一音频的缓存。

音频替换不修改 `ChartDraft`、谱面撤销/重做历史、Scale、波形可见性或其他纯视图偏好。成功替换时根据默认 BPM `100` 和 `46` 逻辑像素目标 1/16 行距重新选择自动编辑 Zoom，并用该 Zoom 生成待提交波形；取消、解码失败或波形失败不得修改当前 Zoom。BPM/Offset 重置和自动 Zoom 都不进入谱面撤销历史；BPM/Offset 属于音频会话，重置不参与草稿未保存判定。

## 音乐结束线与音频导出长度

音乐结束线是当前音频会话的编辑边界，不属于 `ChartDraft`、正式草稿、恢复快照或 SLK。App 只在会话内保存 `{mode: sourceEnd | beat, row}`；`row` 为 1/16 行且必须能被 `4` 整除。BPM 改变、音频替换成功或应用重启时回到 `sourceEnd`；Offset 改变不重置。BPM 为零时只允许 `sourceEnd`。

导出时间轴以拍零对齐导出时间零。`av_core` 的 `calculate_audio_end_export_seconds` 计算自定义结束秒数，`calculate_audio_end_plan` 计算最终帧数：

```text
R  = sampleRate
N  = source frame count
s  = roundHalfAwayFromZero(offsetSeconds * R)
N0 = N + s

sourceEnd: M = N0
beat(row): T = (row / 4) * (60 / bpm)
           M = roundHalfAwayFromZero(T * R)
```

`M < N0` 裁掉尾部；`M > N0` 追加双声道零帧。`T` 不减 Offset；Offset 只作用于头部。`M <= 0`、`M <= max(0, s)`、BPM 非正、行不是 `4` 的倍数、尺寸溢出或分配失败都必须返回稳定错误。默认路径必须与未引入结束线时逐字节一致。

导出确认时把权威 BPM、结束模式和行解析进不可变快照。`AudioExportSnapshot` 保存最终帧数；`encode_audio_export_snapshot` 对自定义结束用一次分配完成头部移位、尾部裁切和零填充，默认结束仍使用原有 `apply_pcm_offset` 路径。导出任务开始后不得读取可变的实时结束线。

播放有效终点为 `clamp(endSeconds, 0, audioDurationSeconds)`；渲染后的播放 PCM 按共享核心计算的帧数截断。到达截断终点视为自然结束，不保留音效续播状态。当前光标在有效终点或之后时不得开始播放或节奏测试。

`calculate_timeline_range` 的可选自定义结束行参与与音频结束、事件行相同的一次最大比较；Flutter 不得自行取最大。BPM 为零时使用与时间线范围相同的内部 `100 BPM` 投影，只用于显示。

## Ogg 音频导出契约

叠加 Offset 的音频编码只在 `av_core` 中实现一次。输入为当前规范平面双声道 PCM16、原始采样率、权威 Offset、权威 BPM、结束规格和所选品质。MP3 与 Ogg 来源只要得到相同规范 PCM，在相同品质下就必须经过同一转换和编码路径。原始来源元数据只影响 UI 和公开导出流程允许选择的品质；一旦品质有效，就不能影响编码器输出。

导出时转换 PCM，使结果音频可以配合 Offset 为零的谱面使用：

```text
signedShiftFrames = roundHalfAwayFromZero(offsetSeconds * sampleRate)

signedShiftFrames > 0  => 在开头添加相同数量、值为零的双声道帧
signedShiftFrames < 0  => 从开头删除 abs(signedShiftFrames) 个双声道帧
signedShiftFrames == 0 => 保持输入 PCM 帧序列不变
```

Offset 到帧的量化只是导出投影，绝不能覆盖权威 binary64 Offset。转换后的 PCM 保留原始采样率和双声道布局。

使用固定版本和配置的 libogg/libvorbis 在 `av_core` 内编码转换后的 PCM。编码器只接受整数 VBR 品质 `1..5`，依次映射到 libvorbis API 值 `0.1..0.5`。实现必须在进程内完成，不得调用 `oggenc2`、子进程或平台编解码器。必须固定 Ogg 流序列号生成方式、注释/头字段、PCM 到编码器的转换、品质映射以及所有会影响输出的设置，确保核心直接测试、App、C ABI 和 `avtool` 得到相同结果。

来源品质的可选上限策略同样属于 `av_core`，防止 App、CLI 和测试结果不一致：

1. 在解码 PCM 旁保留来源编解码器、采样率和来源码率元数据。
2. MP3 的来源码率取所有已解析音频帧的平均值。
3. Ogg Vorbis 优先使用有效的流头标称码率，否则根据流计算平均码率。
4. 在当前解码采样率和双声道布局下，查询固定 libvorbis 配置中品质 `1..5` 对应的标称码率。
5. 选择标称码率不超过来源码率的最高品质，并限制到 `1..5`。
6. 无法确定来源码率时，品质上限使用 `5`。

默认品质为 `min(3, qualityCeiling)`。请求的品质不在 `1..5` 内或超过当前来源上限时必须拒绝，不能静默降低。

核心导出器接收显式品质和输出接收器，或返回拥有所有权的字节；它不打开文件选择器，也不选择平台路径。平台适配层获取授权目标，并保存核心生成的字节。导出不得修改已加载音频、项目时序、波形缓存或分析缓存。

导出任务在确认时捕获不可变 PCM 所有权或稳定 PCM 快照、采样率、Offset、BPM、结束模式/行、品质、文件名和元数据。任务开始后绝不能读取可变的实时项目值。核心编码器接受协作取消信号，并报告阶段与进度。路径型平台保存过程先写入临时目标，只在成功时原子发布最终文件；iOS/Android 文档目标由系统先预留空文档，成功编码后由平台文件层分块覆盖，受控取消或失败时删除该文档。任何平台都不能保留已知失败的临时或最终输出。如果负 Offset 会删除全部输入帧，必须在编码前拒绝。

可移植文件基础名规范化由共享核心纯函数实现。默认输入为来源音频去除扩展名后的名称，缺失时为 `audio`。合法 Unicode 保留；Unicode 控制字符以及 Windows、macOS 和常用文档提供器不接受的 `<>:"/\\|?*` 替换为 `_`；去除首尾空白和末尾句点；大小写不敏感匹配 `CON`、`PRN`、`AUX`、`NUL`、`COM1..COM9` 或 `LPT1..LPT9` 时在前方添加 `_`；结果为空时回退为 `audio`。平台层传入设备本地时间，生成 `<base>_<yyyyMMdd_HHmmss>.ogg`。路径型目标已存在时不得覆盖，依次尝试在扩展名前追加 `_2`、`_3` 等最小可用序号；iOS/Android 目标冲突由系统文档提供器以不覆盖语义处理，并以系统界面最终确认的名称为准。

桌面平台通过原生保存界面取得目标授权，移动平台通过系统文档保存界面取得目标授权，二者都使用核心生成的规范建议名称。Android 不调用未实现路径保存的通用插件接口，而由 Runner 发起 `ACTION_CREATE_DOCUMENT` 并返回仅供平台文件层使用的 `content://` URI；iOS 同样不调用该接口，由 Runner 通过 `UIDocumentPicker` 导出同名空占位文档并保留返回 URL 的安全作用域授权。iOS 隐式 Flutter 引擎初始化可能早于视图控制器就绪，因此 Runner 必须在用户实际请求保存界面时解析当前视图控制器，不能在引擎初始化时快照并长期保存空引用。两种移动文档标识都不能传给 `dart:io File`。用户取消目标选择时不得启动编码。导出期间应用进入后台或平台撤销继续执行资格时，应用协调层发送取消信号并等待任务停止，平台层删除临时输出或已预留的移动文档且不保留半成品；恢复前台后不自动重试。任何保存失败都必须保留原始异常类型、消息、平台错误码/详情和堆栈，Flutter 错误弹窗展示完整诊断并提供复制到系统剪贴板的操作，不得只显示无法定位原因的泛化文案。

Vorbis 注释不是文件正确性的必要条件。存在有效 UTF-8 来源元数据时，按固定、确定的顺序写入支持字段。缺失、格式错误或不支持的元数据应省略，并且元数据处理绝不能使 Ogg 流无效。

当前共享实现由以下边界组成：`calculate_pcm_offset_plan` 与
`apply_pcm_offset` 唯一执行 Offset 帧量化/变换；
`calculate_audio_export_quality` 查询同一固定 libvorbis 配置得到品质表和来源
上限；`encode_ogg_vorbis` 固定 Ogg serial `0x4F544759`、1024 帧提交块、
`TITLE`/`ARTIST`/`ALBUM` 注释顺序和品质映射。品质 `1`、`3`、`5` 各有独立
逐字节黄金文件及配置期 SHA-256 校验。

`create_audio_export_snapshot` 在开始任务前深复制 PCM 和元数据，并捕获 PCM
标识、原始 binary64 Offset、品质、来源信息、规范基础名及预计算变换计划；
`encode_audio_export_snapshot` 之后只读取该不可变快照，依次报告准备音频和编码
进度，协作取消时不返回部分字节。所有成功字节拥有独立生命周期；来源容器类型
不进入编码分支。

C ABI 的 `otgy_audio_export_cancel_flag` 是线程安全原子取消句柄，可创建、请求、查询并销毁。Dart 在主 isolate 持有拥有句柄，把地址借给编码 isolate；原生编码回调只查询该句柄。编码完成后文件保存仍保持 `ExportingAudio` 全局门控，并在每个写入块之间查询同一取消状态。路径型平台文件层优先把完整临时文件以不替换硬链接原子发布，macOS 在硬链接不可用时改用 `renameatx_np(RENAME_EXCL)`；若沙盒只授权了用户选定的最终路径而拒绝临时兄弟文件，则使用原生 `O_EXCL` 写入、`fsync` 和失败清理。路径型目标在发布瞬间已存在时不触碰旧文件，并继续调用核心命名规则尝试最小 `_2/_3` 后缀。iOS/Android 文档提供器以不覆盖语义创建目标文档；Flutter 以 `256 KiB` 块把核心字节发送给 Runner，Runner 只负责按序写入、刷新和关闭。控制器确认导出完整结束后再发送提交以释放 iOS 安全作用域；此前任何失败都发送中止删除，Runner 不能解释 Ogg 内容。

当前 `avtool export-audio` 的严格参数形态为
`avtool export-audio <audio> --offset <seconds> --quality <q> --output <file.ogg>`。
它是上述入口的轻量文件适配层，Offset 与品质使用独立于区域设置的
`from_chars` 严格解析；CLI 从解码结果取得 `AudioSourceInfo`，创建同一快照并调用
同一编码任务。输出先完整写入目标同目录的 `.otgy-tmp` 临时文件，再以不替换既有
目标的硬链接操作原子发布并删除临时名；任何解析、解码、编码、写入或发布失败都
不留下最终文件，也不覆盖已有目标。成功只输出一行固定字段顺序的 schema v1
ASCII JSON，例如：

```json
{"schemaVersion":1,"codec":"ogg-vorbis","sampleRate":11025,"frameCount":8192,"signedShiftFrames":0,"quality":3,"byteCount":4421}
```

参数形态错误返回 `2`；输入/输出路径、已有目标、写入或发布失败返回 `3`；数值、
解码或核心导出拒绝返回 `4`；未预期异常或机器输出失败返回 `5`。运行错误保持
stdout 为空，并在 stderr 输出单行稳定错误 JSON。

## 波形兼容契约

波形图块由 `av_core` 在 CPU 上生成。Flutter 只显示生成的 Alpha 数据，不重新计算波形几何。

参考行为包括：

- 左右声道分别显示在谱面中心两侧
- 每个垂直图块为 128 个逻辑像素
- 逐像素计算采样最小值和最大值
- 使用相同的采样跳过、采样舍入、限制和整数缩放规则
- 支持整流和有符号形状
- 支持均匀亮度和振幅亮度
- 支持 1x、2x、3x、4x 采样模式，并使用相同的降采样平均方式
- 图块失效和缓存键包含所有会影响输出的输入

ArrowVortex 视图映射属于兼容契约：

```text
internalZoom = uiZoom - 3
pixelsPerSecond = round(21.077 * pow(1.518, internalZoom))
pixelsPerRow = round(11.588 * pow(1.48, internalZoom)) / 48
scaledPixels(v) = (v * int(64 * scaleLevel)) >> 8
waveformChannelWidth = min(256, scaledPixels(256))
waveformSpacing = scaledPixels(24)
```

支持的核心参考范围为 UI Zoom `1..19`、内部 Zoom `-2..16`、Scale `1..10`。Windows 和 macOS 暴露 Scale 调节，并以 ArrowVortex 的 `0.25` 步长提交；iOS 和 Android 的公开 UI、手势及无障碍操作始终使用固定 Scale `4.0`，不发送其他 Scale 值。核心、C ABI 和黄金测试仍支持完整 `1..10` 范围，不能为移动端另写波形算法。移动端双指缩放只累积 Zoom 并提供连续反馈，最终提交到最近的 `0.25` Zoom 参考级别，任何手势轴都不得修改 Scale。缩放时当前时间光标/判定线保持固定。相同逻辑视口、参数、PCM 和设备像素策略必须生成相同的 Alpha 图块。

当前四个 1x 权威入口为
`generate_signed_uniform_waveform_tile_1x(pcm, uiZoom, scale, blockIndex)`、
`generate_rectified_uniform_waveform_tile_1x(pcm, uiZoom, scale, blockIndex)`、
`generate_signed_amplitude_waveform_tile_1x(pcm, uiZoom, scale, blockIndex)`、
以及 `generate_rectified_amplitude_waveform_tile_1x(pcm, uiZoom, scale, blockIndex)`。
另有 `generate_signed_uniform_waveform_tile_2x(pcm, uiZoom, scale, blockIndex)`、
`generate_rectified_uniform_waveform_tile_2x(pcm, uiZoom, scale, blockIndex)`、
`generate_signed_amplitude_waveform_tile_2x(pcm, uiZoom, scale, blockIndex)`，以及
`generate_rectified_amplitude_waveform_tile_2x(pcm, uiZoom, scale, blockIndex)`，覆盖
两种形状/两种亮度的抗锯齿级别 `1`。另有
`generate_signed_uniform_waveform_tile_3x(pcm, uiZoom, scale, blockIndex)`、
`generate_rectified_uniform_waveform_tile_3x(pcm, uiZoom, scale, blockIndex)`，以及
`generate_signed_amplitude_waveform_tile_3x(pcm, uiZoom, scale, blockIndex)` 和
`generate_rectified_amplitude_waveform_tile_3x(pcm, uiZoom, scale, blockIndex)`，覆盖
两种形状/两种亮度的抗锯齿级别 `2`。另有
`generate_signed_uniform_waveform_tile_4x(pcm, uiZoom, scale, blockIndex)` 和
`generate_rectified_uniform_waveform_tile_4x(pcm, uiZoom, scale, blockIndex)`，以及
`generate_signed_amplitude_waveform_tile_4x(pcm, uiZoom, scale, blockIndex)` 和
`generate_rectified_amplitude_waveform_tile_4x(pcm, uiZoom, scale, blockIndex)`，覆盖
两种形状/两种亮度的抗锯齿级别 `3`。十六个入口共享同一采样、形状和亮度实现；
1x、2x、3x 与 4x 入口分别覆盖 ArrowVortex 的有符号/整流形状与均匀/振幅亮度
全部组合。每个入口返回拥有所有权的 Left、Right 两个行主序 Alpha 平面。每个
平面高度固定为
`128`，宽度为上述 `waveformChannelWidth`，UI 之后再独立着色。结果同时返回
`blockIndex`、`pixelsPerSecond`、声道宽度和声道间距；不包含 GPU 纹理、颜色、
Flutter 对象或全局视图状态。

每个 block 覆盖连续 128 个垂直像素，源起点按
`trunc(128 * samplesPerPixel * blockIndex)` 计算。每个输出行使用参考实现相同的
`sampleSkip = max(0.001, samplesPerPixel / 200)`、最接近整数采样索引、最小/最大
幅度、限制范围和整数幅度缩放。负数缩放显式实现与 ArrowVortex 目标工具链的
算术右移相同的向负无穷取整，不依赖 C++17 对负数右移的实现定义行为。最后一个
不完整图块中，若四舍五入索引落到输入末尾后一帧，核心把它限制到最后一个有效
帧，避免复制参考代码的越界读取；音频结束后的行和完整 block 返回全透明 Alpha。
有符号入口把每行边缘投影到
`[center + min(left, 0), center + max(right, 0))`；整流入口使用
`magnitude = max(abs(left), abs(right))` 并投影到
`[center - magnitude, center + magnitude)`。两种形状之外的几何、采样、限制、
错误和所有权契约完全相同。

均匀亮度把形状覆盖范围内的 Alpha 固定为 `255`。振幅亮度保持所选形状的
覆盖范围，但逐行使用 ArrowVortex 的整数算术：

```text
amplitudeScalar = ((255 << 16) * 2) / renderWidth
magnitude       = max(abs(left), abs(right))
rowAlpha        = (magnitude * amplitudeScalar) >> 16
```

`renderWidth` 在 1x 时等于 `waveformChannelWidth`，在 2x/3x/4x 时分别等于声道
宽度的两倍、三倍和四倍；振幅亮度在超采样投影前按该宽度逐行计算，之后才参与
对应整数降采样。
覆盖范围外仍为透明 `0`。上述乘法在当前最大声道宽度和边缘限制内不会溢出
32 位有符号整数；所有操作数为非负整数，因此右移结果不依赖实现定义行为。

四个 2x 入口先使用 `2 * waveformChannelWidth` 的宽度和 `2 * 128` 的高度执行
所选形状与亮度的相同采样边缘和投影。`samplesPerBlock` 仍按最终的 128 行计算，
每个超采样行的 `advance` 则相应减半。投影完成后，每个最终 Alpha 使用参考实现
相同的 2×2 非负整数平均：

```text
alpha2x(x, y) = (source(2 * x,     2 * y) + source(2 * x + 1, 2 * y)
               + source(2 * x, 2 * y + 1) + source(2 * x + 1, 2 * y + 1)) / 4
```

这里 `/` 是截断小数部分的整数除法。最终图块的宽、高、声道间距和 block 范围
仍与 1x 入口相同；只有采样和 Alpha 边缘结果不同。

四个 3x 入口相应使用 `3 * waveformChannelWidth` 的宽度和 `3 * 128` 的高度执行
所选形状与亮度的采样和投影，每个超采样行的 `advance` 为 1x 的三分之一。
每个最终 Alpha 使用参考实现相同的 3×3 非负整数平均：

```text
alpha3x(x, y) = sum(source(3 * x + dx, 3 * y + dy), dx = 0..2, dy = 0..2) / 9
```

这里同样使用截断小数部分的整数除法；最终图块几何和 block 范围保持不变。

四个 4x 入口使用 `4 * waveformChannelWidth` 的宽度和 `4 * 128` 的高度执行
所选形状与亮度的采样和投影，每个超采样行的 `advance` 为 1x 的四分之一。每个
最终 Alpha 使用参考实现相同的 4×4 非负整数平均：

```text
alpha4x(x, y) = sum(source(4 * x + dx, 4 * y + dy), dx = 0..3, dy = 0..3) / 16
```

这里仍使用截断小数部分的整数除法；最终图块几何和 block 范围保持不变。

UI Zoom 或 Scale 非有限或超出上述范围时分别返回稳定
`InvalidUiZoom`/`InvalidScale`，不返回部分图块；两个 Alpha 平面任一分配失败返回
`AllocationFailure`。`WaveformTile` 深复制两个平面，移动则转移所有权。除保留
上述十六个兼容命名入口外，核心还提供
`generate_waveform_tile(pcm, uiZoom, scale, blockIndex, shape, luminance, sampling, filter, filterStrength)` 通用入口。`shape` 为有符号或整流，
`luminance` 为均匀或振幅，`sampling` 为 1x/2x/3x/4x（C ABI 中分别编码为
0/1/2/3），并且所有组合共享同一实现。`filter` 为关闭、高通或低通；开启时
核心从第 0 帧开始对完整左右声道分别应用三阶 Butterworth IIR，不能按图块局部
过滤或从中途重置滤波状态。每个完整声道过滤结束后，严格按 ArrowVortex
`FilterOrder3` 以该声道最大的正样本计算归一化系数，再执行兼容的 32 位整数
乘法回绕和算术右移；不能改用绝对峰值、浮点缩放或局部归一化。过滤结果随后按
相同的采样、形状、亮度和抗锯齿流程生成 Alpha 图块。滤波截止频率由
`filterStrength` 的 `0..1` 值按 ArrowVortex 兼容映射确定：高通为
`0.10 + 0.80 * strength`，低通为 `0.01 + 0.10 * (1 - strength)`，会话默认强度
为 `0.75`。滤波只改变波形显示，不改变规范 PCM、BPM、Offset、播放音频或导出
结果。`filterStrength` 即使滤波关闭也必须有限且在 `0..1`，非法选项或强度返回
稳定错误。

C ABI 不实现滤波算法；它只调用上述核心完整声道入口，并在同一个 `otgy_pcm`
句柄内按滤波类型和强度缓存一份拥有所有权的过滤 PCM。连续请求相同设置的多个
图块必须复用该缓存；类型或强度变化时替换缓存。Flutter 可同时缓存原始与滤波
图块，按视图设置决定只显示滤波层或以冷灰青滤波层叠加原始青绿色层。颜色与
透明度只属于 Flutter 展示参数，不进入核心 Alpha 图块。

## BPM 与 Offset 兼容契约

- 移植 ArrowVortex 的起音、BPM 和 Offset 算法，并保持操作顺序、常量、线程语义和候选顺序不变。
- 检测器输出使用 `double`，并保留未舍入结果。
- BPM 候选排序和前三名行为保持兼容。
- 主界面分析操作使用 ArrowVortex 默认的“未选择区间”范围：从音频时间零开始，最多分析 600 秒，并限制到实际音频长度。
- BPM 与 Offset 联合分析按已提交音频版本延迟执行且单次在途。成功提交替换音频后立即自动请求一次共享核心检测器并打开候选弹窗；并发的手动请求复用该任务，之后的请求复用已完成结果。
- 缓存原始候选 BPM、Offset 和 fitness。UI 排名、小数格式和标准化置信度百分比只是派生视图，不替换原始结果。
- 成功提交替换音频后，使上一音频的联合分析结果失效。取消选择、替换音频解码失败或波形生成失败时保留原结果。
- 应用检测候选是一条原子共享核心领域命令，同时提交候选的未舍入 BPM 和按兼容符号转换的 Offset。UI 不得把两个值当作无关修改分别写入。
- 应用候选使时序转换和时序网格派生数据失效，但保留解码 PCM、波形图块和当前音频的分析结果缓存。
- 检测器结果与 App 权威 Offset 之间的符号转换必须显式实现并测试。转换后的权威值保持未舍入；三位小数 UI 文本和 Ogg 导出整帧位移都只是派生投影。

当前起音权威入口为同步 `detect_onsets(pcm)`。它只接收规范 `Pcm16`，不接受文件、
音频设备、线程数或检测方法参数；内部固定使用 ArrowVortex 的 `windowlen = 256`、
`bufsize = 1024`、`complex` 方法和单线程分支。算法所需的 Aubio 子集、Ooura FFT
与 `FindOnsets` 作为 `av_core` 私有源码切片编译，公共头不暴露参考实现类型，也不
依赖本地 `../ArrowVortex-release` 目录。

每个 PCM 帧严格按参考表达式
`(float)((int)left + (int)right) / 65536.0f` 混为单声道，不重采样。输出 `Onset`
保存非负规范 PCM 帧索引和 binary64 strength；当前参考检测器对每个已接受起音
写入精确 `1.0`。空 PCM 和少于 256 帧的 PCM 成功返回空序列。采样率或帧数超过
参考 `int` 范围时返回稳定 `InputTooLarge`，工作缓冲或输出分配失败时返回稳定
`AllocationFailure`；失败结果不暴露部分序列。调用方获得拥有所有权的数组，复制
执行深复制，移动转移所有权。600 秒应用分析范围、BPM/Offset 候选、进度阶段、
缓存、CLI 和 C ABI 不属于该底层起音入口。

当前 BPM 权威入口为同步 `detect_bpm_candidates(pcm)`。它先调用上述唯一的起音
入口，再严格按 `TempoDetectorImp::exec` 只把前 100 个起音 strength 替换为对应
起音位置前后各最多 100 帧单声道样本的平均绝对幅值。PCM16 到单声道的逐帧公式
由起音与 BPM 共用的私有函数实现，不能在两个检测器中各写一份。零个或一个起音
直接返回 ArrowVortex 的默认候选 `BPM = 120.0, fitness = 1.0`。

两个及以上起音使用固定 `89..205` BPM 搜索范围、10 帧 interval 粗扫步长、3 级
粗扫降采样、2048 点 Hamming 窗、三次多项式 fitness 归一化、40% 局部精扫阈值、
稳定降序排序、`0.1` BPM 的近重复/倍频去除、整数 BPM 吸附和最多前三名截断。
输出 `BpmCandidate` 只包含未舍入 binary64 BPM 与 fitness；Offset 尚未计算，也不
使用三位小数显示值。ArrowVortex 的粗扫项目之间没有共享浮点归约，独立参考已经
证明 1、2、8、16 线程结果逐字节相同，因此核心固定按一个线程的项目顺序串行
执行，避免把硬件并发数变成跨平台输入，同时保持候选值和顺序。

参考环形 gap 窗要求最大 BPM 对应的最小 interval 至少为 2048 窗的一半，即
`round(sampleRate * 60 / 205) >= 1024`；不满足时返回稳定
`UnsupportedSampleRate`，不进入会产生负环形索引的参考路径。超过参考整数范围、
工作内存分配失败分别返回 `InputTooLarge`、`AllocationFailure`，失败不暴露部分
候选。成功数组由结果拥有，复制为深复制、移动转移所有权。Offset、600 秒应用层
范围和六阶段进度不属于这个独立 BPM 入口。C ABI 暴露该诊断入口；产品 Flutter
工作流只调用下述联合分析并按音频版本缓存结果。分析协作取消和 CLI `detect-sync`
仍未开放。

当前 Offset 权威入口为同步 `detect_offsets(pcm, bpmCandidates)`。它只接受零至三个
有限、位于 `89..205` 的 `BpmCandidate`，并保持输入顺序、未舍入 BPM 与 fitness
不变。入口调用唯一的起音实现取得位置；Offset 的基础相位直方图按参考对每个起音
固定累加 `1.0`，不使用 BPM 阶段临时重算的 strength。BPM 与 Offset 共用同一份
2048 点 Hamming gap 窗和环形置信度实现。

对每个候选，基础检测相位是最强 gap 位置除以采样率。随后按参考以
`sampleRate / 20` 帧为单边窗口生成单声道平均幅值上升斜率，把基础相位和半拍
offbeat 在整个 PCM 上的斜率支持相加；基础支持大于或等于 offbeat 时保留基础值，
否则选择 offbeat。这个结果以非负 `detector_offset_seconds` 返回。ArrowVortex
`DialogAdjustSync` 在应用候选前执行一元取负，因此同一结果还显式返回
`app_offset_seconds = -detector_offset_seconds`。转换不经过显示舍入；检测值为
`+0.0` 时 App 值按 IEEE-754 保留为 `-0.0`。

`OffsetCandidate` 同时返回 BPM、检测相位、App 权威 Offset 和 fitness，避免适配层
按数组位置自行重新配对。空 BPM 候选成功返回空数组；超过三名、非有限、越过
兼容 BPM 范围或 fitness 非有限时返回 `InvalidBpmCandidates`。采样率、参考整数和
分配边界与 BPM 入口一致，失败不暴露部分候选。这个独立入口分析全部 PCM，并会
自行调用起音检测；需要 600 秒范围和单次起音执行的调用方必须使用联合入口。

当前联合权威入口为同步 `analyze_sync(pcm, stageCallback, context)`。它从 PCM 帧零
开始，把实际分析帧数固定为 `min(frameCount, sampleRate * 600)`，然后只对这个前缀
运行一次起音检测；BPM strength 重算、BPM 候选和 Offset/offbeat 比较全部复用同一
起音序列和同一个前缀帧边界。后缀 PCM 不能进入任一阶段。独立起音、BPM 和 Offset
入口继续保留为直接算法测试与诊断接口，但产品联合分析不得依次调用它们而重复起音。

可选同步回调严格按真实转换报告
`DetectingOnsets`、`ScanningIntervals`、`RefiningCandidates`、`SelectingBpm`、
`CalculatingOffsets`、`FinalizingResults` 六个阶段，不虚构阶段内百分比。成功结果
拥有实际分析帧数和原顺序 `OffsetCandidate` 数组，其中 BPM、检测相位、已转换的
App Offset 与 fitness 均保持未舍入 binary64；复制执行深复制，移动转移所有权。
不支持的采样率、参考整数边界和分配失败返回稳定错误且不暴露部分结果。C ABI
原样转发六阶段回调；Dart FFI 在分析 isolate 中接收回调，并通过 `SendPort` 把真实
阶段更新到 Flutter。App 对同一已提交音频复用唯一在途任务和完成候选缓存，音频
版本变化时丢弃过期结果。分析协作取消和 CLI `detect-sync` 仍未开放。

## 精度模型

每个值只有一份权威高精度表示。显示形式和导出投影都由它派生，不能成为独立可编辑副本。

Offset 保存以下信息：

- 以 IEEE-754 binary64 表示的权威原始秒数
- 可选的检测来源信息，例如来源采样率和基础采样索引
- 自动检测得到该值时所使用的算法/版本元数据

由于非正拍调整可能包含由 BPM 派生的小数间隔，并不是每个最终检测 Offset 都恰好对应一个采样索引。因此不能只用采样索引作为规范表示。

BPM 保存检测器或用户输入的权威 binary64 值。新工作区和成功替换音频后的默认值为精确 `100.0`；显示和导出字符串均由权威值派生。

BPM 微调通过共享核心领域命令执行，不能修改 UI 副本。增减命令在当前权威 BPM 上精确加 `+0.001` 或 `-0.001`。合法的三位小数直接输入会用独立于区域设置的解析值替换权威 BPM。每次提交 BPM 修改都会使时序转换和时序网格派生数据失效，但保留 PCM、波形图块和音频分析结果。

BPM 文本输入只接受十进制数字和一个独立于区域设置的小数点。Flutter 输入适配层在输入框获得焦点期间保留局部文本，不把中间数字发送为领域命令；按回车或失去焦点结束编辑时才解析完整文本并最多提交一次。App BPM 范围为 `0.0..100000.0`：保留 ArrowVortex 的最大值，不允许其负值范围，并允许零表示未设置/无网格状态。无效、空白或越界输入不发送领域命令，输入框恢复为当前权威值。步进操作在边界处停止。短按立即提交；长按重复值作为 UI 临时状态累积，在松开时只发送一次最终 BPM 命令，只触发一次网格重算。BPM 和 Offset 修改（包括应用检测候选）都不创建撤销历史。

Offset 微调同样通过共享核心领域命令执行。Flutter 输入适配层在输入框获得焦点期间保留局部文本，不把中间数字发送为领域命令；按回车或失去焦点结束编辑时才用独立于区域设置的规则解析完整文本并最多提交一次。无效、空白或越界输入不发送领域命令，输入框恢复为当前权威 Offset。按拍移动使用当前权威 BPM、一个未舍入基准 Offset 和累计的有符号整拍/半拍数量：

```text
secondsPerBeat  = 60 / bpm
cumulativeBeats = sum(forwardOne: +1, forwardHalf: +0.5,
                      backwardHalf: -0.5, backwardOne: -1)
initialBeats    = round(-baselineOffset / secondsPerBeat)  // 仅应用分析候选
offset          = clamp(baselineOffset + secondsPerBeat * cumulativeBeats,
                        -30, 30)
```

应用分析候选时，以候选未舍入 Offset 为新基准，累计拍数初始化为共享核心
`nearest_whole_beat_offset` 返回的 `initialBeats`，使实际 Offset 落在半拍范围内；
候选的 BPM、Offset 和 fitness 均不修改。成功加载音频以及合法 Offset
直接输入会把对应未舍入 Offset 设为新基准并把累计拍数归零。修改 BPM 时不重新解释
旧拍数，而是保留当时的有效 Offset、以它建立新基准并归零拍数；到达 Offset 边界时
同样从边界开启新序列。基准和拍数只是当前编辑会话的命令状态，不进入正式草稿、SLK
或导出快照；每次共享核心计算得到的最终 binary64 Offset 仍是唯一权威值。

合法 Offset 直接输入在编辑结束后提交；按拍移动仍立即提交。每次提交都会使时序转换和时序网格派生数据失效，同时保留 PCM、波形图块和音频分析结果。显示文本只从最终权威 binary64 值派生；音频导出直接快照该值并在共享核心中量化为裁剪或补静音帧，绝不解析三位小数文本。

App 权威 Offset 范围为 `-30.0..30.0` 秒。超出范围的直接输入会被拒绝；按拍移动限制在最近边界。BPM 为零时不能使用按拍 Offset 命令，时序网格也不生成小节、拍或细分引导线，但 `ChartDraft` 的抽象 1/16 行坐标、上下文点击命令和撤销历史仍然可用。BPM 为零时，画布仅为行布局和范围比较使用不可持久化的 `100.0` 投影 BPM；它不生成可见网格、不替换权威零值，也不参与保存或导出。

当前高精度时序换算入口接收按值传入的 `TimingParameters { bpm, offset_seconds }` 快照，不持有或修改工作区权威值。App Offset 表示音乐开始相对第一
拍的秒数，因此固定 BPM 下的连续坐标公式为：

```text
secondsPerBeat = 60 / bpm
beat            = (seconds + offset) / secondsPerBeat
seconds         = beat * secondsPerBeat - offset
sixteenthRow    = beat * 4
beat            = sixteenthRow / 4
```

所有运算按上述 binary64 顺序执行，不先使用三位小数显示值，也不在换算层吸附、
取整或覆盖输入。连续拍和 1/16 行可以为负；具体编辑命中边界由后续命令层决定。
`seconds_to_beat`、`beat_to_seconds`、`seconds_to_sixteenth_row` 和
`sixteenth_row_to_seconds` 在 BPM 为零时返回稳定 `NoGrid`，不使用投影 BPM。
纯谱面坐标 `beat_to_sixteenth_row` 与 `sixteenth_row_to_beat` 不依赖 BPM，因此零
BPM 下仍可用。BPM 必须有限且位于 `0..100000`，Offset 必须有限且位于
`-30..30`；位置输入和结果也必须有限，错误不返回部分数值。网格集合、时间线范围、
翻页和播放位置继续消费这些换算入口，不得另写公式。

当前时序校准网格入口为 `generate_timing_grid(timing, beginSeconds, endSeconds)`。输入秒范围是半开区间 `[beginSeconds, endSeconds)`；相邻范围可以直接
拼接而不会重复边界线。核心只枚举非负整数 1/16 行，并为每行调用
`sixteenth_row_to_seconds` 取得未舍入秒数，不能通过累计浮点步长生成。首版固定 4/4：
行号可被 16 整除时返回 `Measure`，否则可被 4 整除时返回 `Beat`，其余返回
`Sixteenth`；结果按行号和音频时间严格递增。

BPM 为零时网格入口成功返回空数组，不能使用画布的非持久化投影 BPM。空范围或
整个范围位于非负谱面行之前也成功返回空数组。为防止视口查询意外物化整首极端
BPM 数据，单次结果最多 `1,000,000` 条；超出数量、超过 binary64 可精确表示的整数
行范围或时间投影溢出返回稳定 `RangeTooLarge`。非法时序、非法范围和分配失败分别
返回稳定错误且不暴露部分网格。结果拥有行数组，复制为深复制，移动转移所有权。
Flutter 只查询当前视口所需的网格范围；时间线总范围、翻页目标和播放位置分别消费下述共享核心入口，不复制换算公式。

当前共享时间线范围入口为 `calculate_timeline_range`。核心在非负连续 1/16 行坐标中
比较所有候选，起点固定为行 `0`，基础终点固定为行 `64`，即四个 4/4 小节。可选
音频结尾先通过 `seconds_to_sixteenth_row` 投影；谱面适配方把全部事件行和全部
`SHOW-D.rangeEnd` 行交给核心，由核心分别扫描最大值，Flutter 不得预先筛选。结果
返回起点/终点的连续行和未舍入秒数。音频结尾成为唯一最晚项时，结果保留调用方
传入的精确 audio-end 秒值，不执行行到秒的往返量化；行候选成为最晚项时则调用
`sixteenth_row_to_seconds`。

权威 BPM 为 `+0.0` 或 `-0.0` 时，范围入口只在局部快照中把 BPM 投影为精确
`100.0`，继续保留权威 Offset，并通过结果标记该投影；调用方的 BPM/Offset 不被
修改，时序网格仍保持空。音频结尾必须是有限非负秒数，整数事件行和 Showtime
范围终点不得超过 binary64 可精确表示的最大整数，任何投影结果都必须有限。错误
不返回部分范围。底层数值入口继续接收显式行数组，用于直接算法测试；当前模型入口
`calculate_chart_timeline_range(draft, timing, audioEnd)` 先调用唯一的标记与
Showtime 投影，从 `ChartDraft` 收集全部事件行和派生 `SHOW-D.rangeEnd`，再调用
上述数值入口。C ABI 和 Flutter 只传入谱面句柄，不扫描、筛选或重算行范围。翻页和
播放位置继续消费同一 `TimelineRange` 结果。

当前共享导航入口为 `calculate_timeline_navigation(range, currentRow, visibleBeats)`。它直接消费 `TimelineRange` 的连续 1/16 行边界，一次返回到开头、
上翻页、下翻页、到末尾四个目标及各自可用状态。翻页距离固定为：

```text
pageRows = (visibleBeats - 1) * 4
pageUp   = max(rangeBegin, currentRow - pageRows)
pageDown = min(rangeEnd,   currentRow + pageRows)
```

`visibleBeats` 可以带小数但必须有限且至少为 `1`；恰好为 `1` 时两个翻页目标等于
当前位置并稳定禁用。当前位置必须是范围内的有限连续行。计算保留小数行，不吸附到
拍、小节或 1/16 整数行；只有目标越过边界时才限制。某个目标与当前位置相同时对应
操作禁用，因此既覆盖位于首尾边界，也覆盖浮点精度下已经无法继续移动的情况。
Flutter 只能消费这些目标和状态，不能自行重算页距或只按音频长度决定末尾。

当前纯播放位置入口由 `start_playback`、`advance_playback` 和 `pause_playback` 组成，
只维护 `PlaybackPhase`、精确时间线秒数和音频时长，不打开音频设备或读取 PCM。
开始播放要求已加载的有限非负音频时长；光标可以是有限负秒数，表示音频 0 秒前
的静音时间线，但不能晚于音频末尾。光标恰好位于非空音频末尾时，开始操作先把
位置设为精确 `0.0` 再进入 `Playing`；零时长音频在 `0.0` 保持 `Paused`。

播放推进只接受有限非负增量。未到音频末尾时保留 binary64 加法结果；达到或越过
末尾时固定为精确音频时长并原子转为 `Paused`。已经暂停的状态不随推进调用改变。
普通暂停只把 phase 改为 `Paused`，时间线秒数和音频时长位模式保持不变。设备回报
使用 `advance_playback_frames(start, playedFrames, sampleRate)`，在核心中把整数帧数
投影为精确推进量，避免平台层各自换算时间。自然结束后应用操作协调层立即解除播放门控。

`render_playback_pcm(pcm, timelineStartSeconds)` 是无谱面音效时的设备缓冲生成入口。它把有限
时间线起点按当前采样率投影到最近整数帧，半帧远离零：负位置生成对应数量的前置
双声道静音，非负位置跳过已播放来源帧；输出固定为交错 stereo PCM16LE，并保留
采样率和帧数。它拒绝晚于音频末尾、非有限、`int64` 边界和尺寸溢出，不修改规范
平面 PCM。C ABI 只提供拥有句柄及采样率、帧数、字节视图。

谱面预听使用同一核心播放切片的 `render_chart_playback_pcm` 扩展入口。Flutter 把两套
离线 WAV 字节同步交给核心建立拥有型 `PlaybackSoundBank`；核心自行解析 RIFF/WAVE
PCM16，接受单声道或双声道并规范为平面双声道，拒绝缺失、重复、损坏或非 PCM16
资源。音效选择配置包含中文/英文、4K/8K、切换音效开关和显式 `uint64` 随机种子；
Flutter 不能根据标记类型选择文件，C ABI 只能转换枚举、字节视图、配置与所有权。
`space_bar`、`turn_1` 和 `turn_2` 在两种语言下逐字节相同，只从 `assets/sounds/common`
打包一次；Flutter 为两种语言传入同一份字节，核心仍按语言建立各自拥有型资源。

音效版本只改变 Flutter 交给核心的 WAV 字节，不改变核心契约。默认使用裁切资源：
中文的 7 个片段从 `assets/sounds_trimmed/zh` 加载，其余片段仍从 `assets/sounds` 加载；
被替换的 7 个原始片段存放在不被 pubspec 声明的 `assets/sounds_original/zh`，不进入
安装包。裁切在资源生成阶段一次完成，运行时不修改 PCM、重采样、混音或 C ABI。
`原始` 变体和隐藏测试入口保留在 Flutter 侧，入口默认关闭；恢复原始变体前必须先把
`assets/sounds_original/zh/` 加入打包资源。音效版本是会话状态，不进入核心配置结构，
也不写入项目、草稿或偏好文件。

使用指南截图按桌面和触屏两套保存在不被 pubspec 声明的 `assets/guide_source`，
`scripts/optimize_guide_images.py` 生成 WebP 到 `assets/guide`，只有后者进入安装包；
运行时仍按平台在桌面和触屏两套 WebP 之间二选一。

音效计划只读取 `ChartDraft` 的显式语义关系。核心按固定伪随机算法为 `NPC-N` 选择
`m1-` 前缀的 4K `2/4/6/8` 或 8K `1/2/3/4/6/7/8/9`。每个
`ChartDrillGroup` 的 `npc_event_ids` 与 `player_event_ids` 必须各自按对应事件的
`(row, id)` 递增顺序维护，不能合并两边后交错排序，也不能只按 ID 排序。核心分别按
两个数组中的 N 顺序让关联 `PLY-N` 复用编号并改用 `f1-` 前缀；N 与 S 使用互相独立的
序列索引，`NPC-S` 在 `m1-go/m1_yeah` 中选择，关联 `PLY-S` 复用同一选择并改用
`f1-go/f1_yeah`。无关联 NPC 的额外玩家 N/S
在不完整草稿中安全跳过。`NPC-C/PLY-C -> turn_1`、`SHOW-C -> turn_2` 只受切换开关
控制，`SHOW-D -> space_bar`、N 和 S 始终启用。固定输入和种子必须跨平台得到完全
相同的选择；应用在新的连续播放会话提供新种子，暂停后原位置继续复用该种子。

核心通过共享时序换算把事件行投影到歌曲采样帧；音效采样率不同时使用确定性的整数
线性插值重采样，单声道复制到左右声道。歌曲、音效和同时触发的音效都在交错设备
缓冲内按稳定触发顺序进行 `int16` 饱和相加。时间线起点落在音效内部时跳过其已过
帧而保留尾部；输出长度仍由歌曲时间线范围决定。平台播放适配层只接收最终采样率、
帧数和交错 PCM16LE，不得看到谱面、音效配置或独立音效流。

节奏测试使用独立核心入口 `render_rhythm_test_playback_pcm`。该入口只接受歌曲 PCM、
拥有型 `PlaybackSoundBank`、语言、权威 BPM/Offset 和时间线起点，不接受 `ChartDraft`，
因此结构上保证全部谱面事件和谱面音效配置都不能影响结果。核心从非负行 `0` 开始，
在每个 `row % 16 == 0` 的四拍小节边界触发所选语言的 `space_bar`，并复用上述帧投影、
音效尾部裁剪、重采样、PCM16 饱和相加和输出长度契约。BPM 为零时没有可投影网格，
核心返回稳定的 `SoundTimingFailure`；Flutter 在入口处明确禁用该状态。

播放预听与后续模拟播放共用 `PlaybackPlan`。`build_playback_plan` 只读取 `ChartDraft`
的显式语义关系，按与上述相同的伪随机规则生成两类数据：`audio_triggers` 保存
NPC/SHOW 以及玩家切换音；`player_notes` 保存有配对音效的 `PLY-N/PLY-S` 及其期望
`f1-*` 音效。`PlaybackTrigger.player` 精确标记玩家列拥有的切换音，供模拟播放过滤，
离线预听仍消费全部触发。两个数组共享同一个递增 `order`，因此按 `(row, order)` 合并
后与旧实现的触发顺序完全一致；无关联 NPC 的玩家 N/S 不进入 `player_notes`，但仍按
原规则推进配对索引。`render_chart_playback_pcm` 只消费该计划，不复制选音逻辑。

`JudgementSession` 是模拟播放的判定核心。`reset` 复制并按 `(row, order)` 排序
`player_notes`，用共享时序换算预计算每个音符的歌曲秒数；`press` 和 `advance` 在
reset 之后不再分配。窗口由权威 BPM 和显式基准计算；应用默认使用半拍基准。1/16 音符基准的单位为
`60000 / BPM / 4`，半拍基准为 `60000 / BPM / 2`；四档窗口是该单位的显式正数倍率，
必须严格递增。一次按键匹配 Bad 窗口内最近的未判音符，距离相同时取较早的音符；
窗口内按错键判 Miss、消费该音符并触发 `PlaybackSoundId::Miss` 反馈音；超过 Bad
仍未按由 `advance` 判 Miss，并播放同一 Miss 反馈音。没有任何
待判音符时 `press` 返回空结果，调用方不得发声或产生判定。内部 `offset_ms` 从测得
的时间差中扣除，用于设备校准和隐藏测试菜单调参，只属于当前 App 会话；它不进入
用户偏好文件、草稿、SLK 或撤销历史。每条 `JudgementResult` 同时携带音符歌曲时间和
判定按键歌曲时间，供测试菜单记录最近 `200` 条判定日志；日志只在内存中保留当前
播放会话，下一次模拟播放开始时清空。

模拟播放引擎 `PlaybackEngine` 在同一 `PlaybackPlan` 上按块渲染。`initialize` 构建
计划、过滤玩家拥有的切换音、把 NPC/SHOW 行投影到采样帧并按 `(frame, order)` 排序，
判定开启时重置判定会话；这一步允许分配。`render`、`push_trigger` 和 `drain_judgements`
在初始化后不再分配。`render` 先写入歌曲块，再混合跨块继续的 voice、当前块内的
NPC/SHOW 触发，然后从无锁队列取出按键事件：按键时间戳按 native 稳定时钟映射到当前
块的歌曲时间，`press` 返回结果时把正确键音效或 Miss 反馈音作为新 voice 混入并回传判定；没有待判
音符时不发声。块末用 `advance` 把超过 Bad 的音符记为 Miss。判定开关关闭时，引擎不重置
判定会话；按键直接在块首混入其映射音效，不计算按键歌曲时间、不匹配或消费音符，也不
执行 `advance`，因此不产生判定结果或 Miss 音效。输出为交错 stereo
PCM16LE，到达有效终点后剩余帧写静音并置为完成。voice 表、按键队列和判定队列都是
固定容量，按键和判定分别使用单生产者/单消费者无锁环形队列。平台适配层只按块调用
渲染并消费判定，不解释谱面或判定规则。C ABI 提供引擎创建/销毁、按键入队、按块
渲染、判定读取和状态查询；另提供一个只读窗口预览函数，用权威 BPM、判定基准和
四档比例调用同一 `judgement_windows_ms`，供测试菜单显示毫秒宽度，Flutter 不得自行
实现窗口计算。`analyse_judgements` 是一次性判定分析算法：输入本次已匹配按键的音符
歌曲时间、按键歌曲时间和按键正确性，加上自动漏按数量；原始时间差由两条歌曲时间线
算出，不依赖判定时使用的 Offset。它返回当前 Offset 下的等级计数、均值/中位数/标准差、
`1 ms` 时间差直方图，并在 `-100..100 ms` 范围内逐毫秒重放四档窗口，给出 P 数最多的
Offset、该最大值对应的连续区间及该 Offset 下的预测等级分布。C ABI 的
`otgy_analyse_judgements` 只转换数据并调用同一实现；Flutter 不计算窗口或推荐值。渲染与入队路径不分配、不返回错误，创建阶段的配置、时序和缺失资源
错误走独立错误域。

Flutter 播放状态使用 `Idle`、谱面预听、节奏测试和模拟游戏四值互斥模式，不能用两个
可同时为真的布尔值表示。三个活跃模式在应用操作协调层都映射到 `Playing`，共享同一
白名单；只有当前模式自身的暂停入口可用。普通谱面预听和节奏测试的平台层只接收核心
生成的最终交错 PCM 和帧数；模拟游戏由平台层按块调用同一个核心引擎，并只消费最终
PCM16LE 与判定结果，不能区分或重新实现混音模式。

模拟模式的判定开关、判定基准、四档比例、校准 Offset 和文字显示时长都是 Flutter
会话状态，创建引擎时一次性传给 `PlaybackEngineConfig`；其中判定开关同时决定引擎是否
进入自由发声路径。引擎运行中不读取或修改这些值，播放中不能切换。`PlaybackJudgement` 的等级与带符号时间差同时驱动判定文字和测试菜单的上次判定
读数，Flutter 不根据时间差重新分级。测试菜单的毫秒预览通过 C ABI 只读函数获取。

Flutter 把普通播放的核心缓冲交给 Android `AudioTrack`、Apple `AVAudioEngine` 或
Windows `waveOut`，并以约 33 毫秒轮询平台已播放帧数。模拟游戏使用平台流式输出按块
渲染，四个平台共用同一 `start/pause/resume/query/stop` 方法通道契约：

- Android 使用 `AudioTrack` 的 `MODE_STREAM`、`PERFORMANCE_MODE_LOW_LATENCY`、
`USAGE_MEDIA`/`CONTENT_TYPE_MUSIC` 音频属性，在 urgent-audio 优先级线程上阻塞
写入；通过只做句柄转换、固定块渲染和完成状态查询的 JNI 桥调用 C ABI。
Release 使用 native-assets 链接钩子，`android_jni.cpp` 必须进入送给链接钩子的
静态库，最终 `.so` 必须导出两个 JNI 符号。设备原生采样率与歌曲采样率不同时，
Flutter 在开始模拟播放前调用共享核心 `otgy_pcm_resample` 把歌曲 PCM 重采样一次，
并以设备采样率创建引擎和 `AudioTrack`，使轨道能进入系统低延迟快速通道；块大小
取设备 `PROPERTY_OUTPUT_FRAMES_PER_BUFFER`，缓冲取平台最小值，并在轨道支持时用
`setBufferSizeInFrames` 收缩到两个设备块。重采样只作用于
本次模拟播放的内存副本，不改变歌曲文件、常规预听、波形、项目数据或导出结果。
- iOS/macOS 使用 `AVAudioSourceNode` 的 Int16 交错格式回调直接调用 C ABI 渲染；
iOS 额外设置 `5 ms` 首选 IO 缓冲。适配层通过 `dlopen` 加载
`otgy_native.framework`，不复制音频或判定逻辑。
- Windows 使用 WASAPI 共享模式、事件回调和 `AUTOCONVERTPCM`，事件线程以 MMCSS
优先级按块渲染并写入，暂停和停止由原子状态与事件控制。

Dart 负责创建引擎、入队按键和读取判定。暂停、自然结束、音频焦点丢失、输出中断或
App 进入后台都先保留平台最后已播放帧，再调用共享帧推进/暂停入口；恢复焦点或返回
前台不自动播放。平台不得用墙钟时间或缓冲总长度猜测中断位置。

移动端方向锁定由平台清单和应用启动共同保证：iOS `Info.plist` 只声明竖屏方向，
Android `MainActivity` 声明 `screenOrientation="portrait"`，Flutter 启动时调用
`SystemChrome.setPreferredOrientations` 只保留 `portraitUp`。方向键屏幕键盘的横屏
布局只是竖屏画布内旋转 90° 的绘制结果，不依赖系统旋转或平台方向 API。桌面窗口
不参与该锁定。

权威 BPM 和 Offset 在当前音频会话内保持 binary64 精度，并独立于界面三位小数投影。正式草稿序列化只往返 `ChartDraft`，不保存 BPM 或 Offset；只含 `ChartDraft` 的异常恢复快照同样不保存 BPM 或 Offset。

UI 格式与存储相互独立：

- 主界面音频行的 BPM 和 Offset 固定显示三位小数
- BPM 候选摘要固定显示三位小数
- 首版正式用户界面不提供完整精度查看入口；完整值只通过 C++ 测试、C ABI/CLI 调试输出和开发工具检查
- 修改显示精度绝不能改变项目数据

## C ABI 规则

- C ABI 只是 `av_core` 的轻量适配层，不包含领域算法。
- 只导出普通 C 兼容类型和不透明句柄。
- 不允许 C++ 异常穿过 ABI 边界。
- 每次分配都必须有显式匹配的释放函数，并记录所有权。
- 字符串使用 UTF-8 并携带显式长度。
- 数组和字节缓冲区携带显式长度，不依赖哨兵值。
- 错误结果使用稳定错误码，并可附带诊断文本。
- ABI 测试把结果与直接 C++ 调用比较。

当前 ABI 版本为 `2`，公共纯 C 头为
`native/av_c_api/include/otgy/av_c_api.h`，动态库目标为 `otgy_c_api`，共享库主版本
固定为 `2`。状态码只描述 ABI 调用边界；核心拒绝通过拥有型 `otgy_error` 返回稳定的
领域、领域内错误码和静态诊断文本。任何 C++ 异常都在最外层捕获并转换，不能越过
`extern "C"`。所有长度在 ABI 中使用 `uint64_t`，转换到本机 `size_t` 前检查范围；
布尔值使用只接受 `0/1` 的 `uint8_t`，枚举使用固定宽度整数和公开常量。

应用操作门控的唯一领域实现必须位于 `native/av_core/src/coordination/operation_gate.cpp`
的纯函数 `coordinate_application_operation`，声明位于
`native/av_core/include/av_core/coordination/operation_gate.hpp`；C ABI、Dart FFI 和 Flutter
只转换模式、操作类和结果，不得复制白名单。模式、操作类和结果都使用同宽度枚举追加
常量，必须保留原有输入输出逐值不变。C ABI 公开追加
`OTGY_APPLICATION_MODE_CHART_INTERACTION = 3`、
`OTGY_APPLICATION_OPERATION_SELECTION_COMMAND = 4`、
`OTGY_APPLICATION_OPERATION_NAVIGATION_OR_VIEW = 5`、
`OTGY_APPLICATION_OPERATION_HISTORY_NAVIGATION = 6` 和
`OTGY_APPLICATION_OPERATION_CHART_INTERACTION_ACTIVE = 5`。
函数签名、结构布局和所有权均不改变，因此该追加不提升 ABI 版本。后续判定结果在末尾
追加 `note_seconds` 和 `press_seconds` 改变了 `otgy_playback_judgement` 的布局，
`OTGY_C_API_VERSION` 提升为 `4`；判定结果随后又追加诊断字段 `queue_delay_ms`，动态库 `VERSION 4.0.0` 与 `SOVERSION 4` 同步更新。引擎配置随后在末尾追加 `judgement_enabled` 并改变结构布局，`OTGY_C_API_VERSION` 提升为 `5`，动态库 `VERSION 5.0.0` 与 `SOVERSION 5` 同步更新。选择单元随后追加 `drill_group_id`、`role`、`part` 和 `event_count` 字段，`OTGY_C_API_VERSION` 提升为 `6`，动态库 `VERSION 6.0.0` 与 `SOVERSION 6` 同步更新。

`otgy_pcm`、波形、分析数组、字节、音频导出快照/输出、谱面、编辑器、校验结果、
成品谱面和 SLK 文档均为不透明拥有句柄。每类句柄只有一个匹配销毁函数，销毁函数
接受空指针；查询得到的指针/长度视图只在所属句柄存活期间有效，输入视图只借用于
同步调用。失败始终把拥有型输出设为空，不发布部分数组、PCM、字节或语义对象。

谱面只读语义快照直接复制 `ChartDraft` 的显式角色事件和三层关系；变长引用使用
拥有型扁平 ID 数组以及显式 offset/count 切片。C ABI 不解析 `NPC-C` 等简称、不
扫描 SLK 行重建角色或 Showtime 关联，也不暴露 `[r & 0]` 编辑对象。成品 SLK 仍
只能从 `FinishedChart` 句柄导出，音频导出仍只能编码核心捕获的不可变快照。

## 确定性规则

- 不得启用 `fast-math` 或等价变换。
- 固定对工具链敏感的依赖版本和配置。
- 显式规定整数位宽、溢出行为、限制和舍入规则。
- 使用独立于区域设置的解析与格式化。
- 不得在 GPU 上计算兼容波形数据。
- 固定 libVorbis 的 libm 边界：对平台数学函数结果执行统一的 binary64
低位量化，关闭浮点收缩和向量化，并在所有架构使用标量参考
`vorbis_ftoi` 路径；该配置必须由 ARM64 与 x86-64 的同一套黄金验证。
- 除非所有支持架构都通过相同黄金契约，否则不得静默引入 SIMD 路径。
- 如果原始浮点结果无法在不同架构间按位一致，允许容差必须被记录、足够小且有理由，并且不能改变任何候选顺序或兼容投影值。

## UI 适配契约

主编辑画布在所有平台上共享同一概念：

- 音频时间和谱面时间沿垂直方向移动
- 判定线保持固定
- 波形声道、谱面网格、原生谱面编辑对象和光标保持参考几何
- 手机竖屏布局保留垂直滚动波形行为

桌面端和移动端可以使用不同的工具栏、面板、快捷键和手势，但这些控件必须以相同参数发送相同领域命令。移动端适配只改变功能入口，不改变结果。

桌面键位映射是 Flutter 输入层状态，不进入 `av_core`、C ABI、`ChartDraft`、SLK、撤销历史或音频导出。`app/lib/src/ui/editor_keymap.dart` 定义可改绑操作（播放 / 暂停、撤销、重做、Zoom 增大 / 减小 / 重置）及每个操作的组合列表；画布 `CallbackShortcuts` 由该列表生成，组合只决定触发，回调仍直接调用既有控制器命令。默认值按 `defaultTargetPlatform` 构建：Windows/Linux 沿用 ArrowVortex 的 `Space`、`Ctrl+Z`、`Ctrl+Y`、`Numpad +/-`，Zoom 重置为未绑定；macOS 沿用既有 `Command` 组合。持久化按逻辑键 ID 与修饰键标志写入应用支持目录的 `ui-preferences.json`，与帮助提示标记同文件，写入时保留两者；无法解码的条目按操作回退默认值，空列表表示显式未绑定。`Esc`、`Enter`、复制和剪切属于固定输入，不进入可改绑集合。

谱面渲染层固定投影为从左到右的 NPC、Showtime、玩家三列。NPC 和玩家列由原生对象的显式角色决定，Showtime 相关对象投影到中间列；三列共享同一垂直时间坐标，以中间点击区的低对比度背景辅助辨认但不绘制内部纵向分割线。领域列常量和存储结构不因视觉顺序改变；Flutter 使用显式语义到视觉轴映射，列编号不作为独立领域字段重复保存，避免列位置与对象语义产生不一致。所有平台必须使用相同投影规则。

Flutter 从共享核心返回的 `channelWidth` 和 `channelSpacing` 计算统一的横向投影：双声道波形整体在画布居中，宽度足够时保持 `1.0` 比例且不向上拉伸，宽度不足时整体等比缩小。NPC 与玩家标记轴分别复用左、右声道波形中心，Showtime 标记轴使用画布横向中心；因此窗口变宽但波形保持原宽时，三条轴相对画布中心的偏移和彼此距离保持不变。相邻轴的中点形成列命中边界，最外侧命中区延伸到画布边缘；隐藏波形只跳过绘制，仍使用已加载波形几何，不改变标记或命中位置。没有已加载波形时回退为画布三等分的三个中心点。

三列画布区域、波形和时序校准网格使用完整画布宽度，从左边缘延伸到右边缘，不保留列布局自身的横向留白。撤销/重做控件与右侧导航播放条保持为画布手势层之上的独立悬浮覆盖层，因此不参与列宽计算。小节编号作为视图标签绘制在对应小节线略上方，不改变网格或谱面命中坐标。

右侧悬浮导航与播放控件是三列上方的独立 UI 覆盖层，不参与谱面列投影，也不产生领域对象。该覆盖层和撤销/重做控件必须作为画布 `Listener/GestureDetector` 的兄弟命中层置于其上方，使按钮悬浮状态不受画布手势竞争影响。波形按钮在暂停时打开与音效菜单同类的非模态锚定选择框；框外点击关闭，面板提供波形显示开关、高通/低通/关闭、滤波强度和叠加原始波形选项。Flutter 波形投影固定传入 ArrowVortex Vortex 预设的整流形状、均匀亮度与 2x 抗锯齿，不保存对应会话字段，也不提供这三项切换控件；其余默认值为滤波关闭、滤波强度 75%、叠加原始开启。原始波形使用青绿色，滤波波形使用不占用标记语义色或播放光标黄色的低彩度冷灰青。均匀亮度下，单独原始层使用 52% 不透明度；双层叠加时原始层使用 24%、滤波层使用 36%；仅显示滤波层时仍使用 36%。标记、网格、播放光标和标题在 painter 顺序中位于全部波形层之后。可调波形设置是会话视图状态，不进入 `ChartDraft`、正式草稿、SLK、撤销历史或音频导出；滤波只影响额外的显示缓存。播放开始时两个设置面板都关闭；播放中波形按钮退化为仅切换可见性，其他选项全部禁用。波形滤波不改变 BPM/Offset、播放音频源或时序网格。语言、舞步模式和切换音效开关是会话播放配置，不进入 `ChartDraft`、正式草稿、SLK、撤销历史或音频导出。Flutter 必须先命中控件自身及选择框的交互区域；未命中时，输入才继续交给下方谱面对象或空白时间线手势。控件操作不得修改列归属或谱面内容。

谱面直接点击使用由三列边界和最近 1/16 行共同确定的网格单元，而不依赖标记最终视觉轮廓。Flutter 把触点转换为规范列和连续 `rawRow`，以 `floor(rawRow + 0.5)` 得到非负目标行；中点并列时稳定选择时间更晚的行。以下按列处理仅适用于普通编辑模式：当前列目标行已有事件时发送该稳定事件 ID，三列全空时发送列与目标行，其他列已占用时发送后由核心返回稳定的位置占用结果。Flutter 不得把普通编辑中对其他列空白区域的点击重定向成对已有事件的删除命令。选择模式和长按入口仍透传实际列与同一目标行，由核心按行解析可选舞步，不在 UI 中改写列、重定向标记或扩展高亮跨度命中。

移动端和桌面端在所有受支持的 `rowSpacing` 下都允许发送谱面内容编辑命令。`rowSpacing` 只影响网格密度、标记重叠和编辑舒适度，不是输入门控条件；用户可以自行调整 Zoom。Flutter 不得因为显示密度低而阻止或改写 `ChartDraft` 命令。标记的具体色值、尺寸、辅助样式、`SHOW-D` 附加数值信息和范围带设计参数属于 Flutter 展示投影，不得反向影响核心事件类型、点击单元或展示范围计算。

Flutter 展示层使用与领域角色解耦的固定语义样式类：`NPC-C` 映射到蓝色菱形，`NPC-N` 映射到带白色顺序号的蓝色圆形，`NPC-S` 映射到红色圆形；`PLY-C` 映射到绿色菱形，`PLY-N` 映射到带白色顺序号的绿色圆形，`PLY-S` 映射到红色圆形；`SHOW-C` 映射到紫色菱形，`SHOW-D` 映射到带白色 `showNCount` 的紫色圆形。列投影继续承担角色区分，样式类不得覆盖或反推领域角色。标记内部不渲染可见的 C/N/S/D 字母，所有标记使用纯填充，不绘制深色外圈、N 内环、S 底部短横线或 D 上下纵向刻度；Flutter 语义树始终暴露完整类型、角色和数值。C 维持菱形，N/S/D 维持圆形，可见区分来自列、形状、颜色和数字有无。具体色值、边框和范围带参数属于 Flutter 设计令牌或展示参数，不进入 `av_core` 或项目序列化。

`NPC-N/PLY-N` 的可见数字由 `av_core` 提供确定性的派生查询，不由 Flutter 扫描绘制列表计算。对当前 N，核心只在同一角色中查找此前最近的 S，并返回该 S 之后、当前事件之前的同角色 N 数量加一；没有此前 S 时从谱面开头计数。C、另一角色事件和 Showtime 事件既不计数也不中断，C 因此允许拆分演练组继续编号。顺序号不持久化、不进入 SLK，也不形成独立历史；任一编辑完成后从新 `ChartDraft` 快照查询即可自然反映 N 插入、N 删除、N 转 S、S 删除和整块删除。删除 N 只使下一同角色 S 之前的后续编号按实际删除数量前移，不改变下一 S 之后的编号。该派生刷新不得修改其他事件、关系或 `SHOW-D.showNCount`。

标记尺寸由 Flutter 按平台固定投影：iOS/Android 的圆形与菱形边界框分别为 `28` 和 `36`，Windows/macOS 分别为 `22` 和 `28`，单位均为逻辑像素。菱形边界框约为圆形直径的 `1.25` 倍，用于补偿其较小的填充面积，使视觉重量不低于圆形。所有 N/S/D 共用圆形直径。尺寸不受 `Zoom`、`rowSpacing` 或 `Scale` 影响，不能产生非等比形变；低 Zoom 下相邻行标记允许按绘制顺序自然重叠，不做避让、隐藏或尺寸补偿。尺寸公式只属于确定性视图投影，不进入项目数据或点击命令参数。

N 派生顺序号和每个 `SHOW-D` 的 `showNCount` 使用相同数字排版规则，并在全部 Zoom 密度下显示无前后缀、无分组符的完整十进制数字。正常一至两位数字字号约为固定圆形直径的 `64%` 并限制在 `12..16` 逻辑像素，更长数字按字符数等比缩小以完整容纳。每个 `SHOW-D` 的 Flutter 投影还包含实际 `dRow` 上的紫色圆形锚点，以及直接使用核心 `[rangeStart, rangeEnd)` 结果绘制的低透明度紫色范围带。范围带横跨三列，位于波形之上、时序网格和所有锚点标记之下，不参与网格单元命中测试。Flutter 不得从圆形锚点像素位置重新推算范围；因此 `SHOW-D1` 位于小节线 +1 时，锚点与从小节线开始的范围带可以有一个 1/16 行偏移。

核心查询同时指出当前 `SHOW-D` 是否为连续序列最后一个事件。仅该事件的 `[rangeEnd - 8, rangeEnd)` 投影使用更浅背景，并在 `rangeEnd - 8` 增加虚线边界；常规区和最后半小节必须作为两个不重叠的填充区域绘制，不能把浅色再次叠加到常规底色上而使合成结果反向变深。这项视觉状态与 NPC-C 冲突例外使用同一核心判断，不能由 Flutter 另行扫描或猜测。范围带和开放区样式只提供视觉提示，不创建事件、不占用额外时间行，也不扩大紫色圆形锚点的点击语义。

ArrowVortex 的编辑画布只作为时间线、波形、固定光标、Zoom、Scale、播放跟随和导航交互的参考。核心不得引入 StepMania 轨道或 Step、Mine、Hold、Roll、Fake、Lift 作为本 App 的权威编辑模型。权威对象始终来自 `ChartDraft`，并保留显式角色、稳定对象标识和舞步、演练组、演练序列、Showtime 等谱面语义。

首版谱面位置命令以 1/16 音符为基础单位。谱面内容通过已经确认的三列上下文点击命令创建、转换或删除。在此之上，`ChartEditor` 拥有按 `(first_row, last_row, drill_id, group_id, role, part)` 稳定排序的单元选择集合、批量快照和复制/待剪切意图。选择单元是演练组内的一个角色段：`NPC-C`、`NPC-N/S`、`PLY-C`、`PLY-N/S` 四个单元，行跨度和事件数由该单元的成员事件派生。`ChartEditor::select(column, row)` 只按精确目标行查找标记，不要求 `current->column == column`：六类 NPC/玩家标记在三列任一列输入时解析到该标记所属的单元；若其所属舞步没有任何单元被选中，则选中该舞步的全部单元，否则切换该单元。`SHOW-D` 切换其引用舞步的全部单元：尚未全选时补齐，已全选时全部取消。`SHOW-C` 在三列均不可选；三列全空行、选择高亮跨度或 `SHOW-D` 展示范围内无标记行、悬空引用及零绑定事件舞步不改变已有选择。选择高亮和展示范围不产生额外命中区域。该行为只在 `av_core` 的 `ChartEditor::select` 中实现，列参数须为 NPC、玩家或 Showtime 三种合法值；普通编辑 `click(column, row)` 继续按列处理，保留其错列占用规则，并与显式 `1` 共用无起始组接管规则。`ChartEditor::select_all()` 在核心收集所有演练组的全部单元，按上述顺序排序，重复调用返回无变化，不修改草稿或历史；C ABI 以 `otgy_chart_editor_select_all` 暴露同一会话命令。`ChartEditor::select_range(first_row, last_row, mode)` 按闭区间内是否存在该单元的任一成员事件判定命中：单元跨度覆盖区间但区间内没有事件不算命中；`Replace`、`Add`、`Subtract` 分别替换、并集和移除选择集合，边界按升序归一，仍不修改草稿或历史，C ABI 以 `otgy_chart_editor_select_range` 暴露并把非法模式值拒绝为参数错误。`ChartEditor::delete_selection()` 先删除选中 NPC 动作段并触发既有 `synchronize_npc_edit` 玩家重算，再删除选中 `NPC-C` 与玩家单元，经 `finish()` 执行与其他编辑相同的空组、失效 Showtime 与空舞步清理，全部合并为一条历史；C ABI 以 `otgy_chart_editor_delete_selection` 暴露。`copy_selection` 和 `cut_selection` 只包含选中单元的事件、所在演练组和所属舞步，整批以最小源行归一化；整舞步全选时沿用源 `moveNCount`，部分选中时按片段内容重算，二者都只准备会话快照且不修改草稿。派生只按 `dance_move_drill_id` 绑定事件，不使用成品完整性判断，因此不完整草稿仍可选择。

`paste(anchor_row)` 是唯一批量落点事务。核心先在局部状态完成行溢出、闭区间占用、每个 `NPC-C` 的 Showtime 范围、完整 ID 映射和目标关系构造；复制粘贴直接以当前草稿为候选，首次剪切粘贴则先逐值验证准备时保存的源舞步、组和事件，在同一候选中删除全部源对象与存活关系，再经过统一完成阶段按空组、失效 Showtime、空舞步顺序清理后检查目标。目标因此可以与本次删除的源区域重叠，但不能与任何存活内容重叠。当片段首个事件所在的组不含 `NPC-C` 且包含 `NPC-N`，且目标锚点之前紧邻的标记是一个只含自身事件、未被 Showtime 引用的 `NPC-C` 时，该 `NPC-C` 并入本次粘贴生成的组和舞步；其原组在不再绑定事件时清理，原舞步在同样不再绑定事件时一并删除。该结果与先粘贴再放置 `NPC-C` 的接管结果在可见结构和舞步归属上一致，标识按操作顺序分配；前置标记不是 `NPC-C`、该组已有其他事件或片段首组自带 `NPC-C` 时不接管。全部成功后只调用一次 `commit()`，失败时草稿、历史、快照、意图和 ID 游标逐值不变。首次剪切提交成功即把意图永久转成普通复制；撤销只恢复草稿快照，不恢复待剪切意图，避免再次删除恢复出的源。后续每次落点都分配全新 ID 并形成独立历史。

核心另提供无副作用的粘贴预检，返回稳定结果码、整体目标区间、每个舞步跨度和按列/行/类型投影的幽灵标记；它与提交共用领域规划，不消耗 ID。选择、快照、意图和预览都是编辑器会话状态，与 `undo_`/`redo_` 同级但不进入 `ChartDraft`、项目文件或平台剪贴板。普通编辑、撤销和重做逐项重验证选择；待剪切源变化由首次落点的逐值验证拒绝。失效 Showtime 和零事件舞步/组仍按现有提交清理顺序处理，序列化、加载、只读查询和失败命令不执行清理。核心、C ABI 和 Flutter 仍不提供单个标记移动；框选按单元命中，`Delete` 删除选中单元。

C ABI 为选择集合和粘贴预览提供拥有式投影句柄及 `destroy/get_data/get_count`，指针只在句柄销毁前有效；Dart wrapper 立即复制为不可变值列表并在 `finally` 销毁句柄。选择调用的实际列、精确目标行及结果经 C ABI、FFI 和 UI 原样透传，不在适配层复制按行解析或列过滤；按行选择本身不改变公开签名、结构或枚举。应用操作门控仅按上述契约追加同宽度枚举常量，不改变 ABI 版本。会话命令结果只保留标量结果码和 `changed`，C 层不得重建多选、冲突、删除或 ID 算法。当前契约固定使用 `OTGY_C_API_VERSION = 6`、动态库 `VERSION 6.0.0` 与 `SOVERSION 6`，绑定由 ffigen 生成。

Flutter 的 `EditorController` 只持有普通、选择、待粘贴三态交互枚举，选择集合、快照数量、意图和预检直接查询 `NativeChartEditor`。画布普通点击发送按列上下文编辑，选择态点击透传实际列与精确目标行发送核心 toggle。桌面和移动端的长按入口同样透传列与行并调用同一 `select`：可选标记所在行的任一列长按都按单元规则选择或切换，`SHOW-C` 在三列均不可选；三列全空行或高亮跨度内无标记行的长按只进入模式且不改变已有选择，不在 Flutter 扩大范围命中。Windows/macOS 待粘贴态把指针悬停行投影为最近非负 1/16 行并请求核心预检，点击有效目标行才发送核心 `paste`；离开画布或失去有效行时清除悬停预览。iOS/Android 待粘贴态始终把固定判定线投影为最近非负 1/16 行并请求核心预检，时间线滚动、Zoom 或导航变化后刷新，显式“粘贴到当前判定线”按钮才发送核心 `paste`；平台入口按操作系统选择，iOS/Android 外接鼠标不切换为桌面语义；移动端画布轻触、marker 激活、语义激活和 Enter 不映射为粘贴提交，只有显式按钮调用该事务。移动端按钮在非播放且非忙碌时即使预览冲突也保持启用，点击仍把当前锚点发送核心并显示其稳定错误，不在 Flutter 拦截，核心提交结果是唯一权威。两类平台点击列或判定线覆盖的列都不改变片段列归属。桌面和移动端都用长按进入选择，不显示常驻选择按钮；待粘贴控件按平台显示对应提示、按钮或退出。画布分别绘制核心选择跨度和预检的整体区间、舞步跨度与幽灵标记，不扫描关系或复制冲突算法。会话切换、选择、复制/剪切准备、预检更新与清除和粘贴提交必须使用 `SelectionCommand` 门控，导航与视图使用 `NavigationOrView`，撤销/重做使用 `HistoryNavigation`；普通内容编辑和其他未列入白名单的入口继续使用 `General`。成功粘贴后保持待粘贴状态且不自动移动 `currentRow`，文本输入、菜单和对话框优先处理自身按键，诊断只记录模式、数量、意图和结果等安全标量，不记录选择 ID 或快照内容。

桌面光标导航是纯 Flutter 输入映射：箭头按固定 1/16 网格吸附到相邻网格线，`Page Up/Down` 与 `Shift+Page Up/Down` 按 16/4 行吸附，`Home/End` 取时间线范围端点，主修饰键加箭头在排序后的 `ChartMarker` 投影中查找严格早于/晚于判定线的最近行。全部访问都经由现有 `moveToRow`，因此自动继承 `NavigationOrView` 门控、时间线边界与播放限制，不产生核心命令或历史。桌面待粘贴态新增的粘贴快捷键只把当前判定线的最近非负 1/16 行交给既有核心 `paste` 事务，与点击落点共用同一预检、错误码和会话状态；平台入口仍由操作系统选择，移动端不注册这些键。

`Ctrl/Command+A` 只把核心 `select_all` 的结果放进会话，并在普通模式下切到选择模式；`Ctrl/Command+Home/End` 只读取现有选择跨度的最小首行和最大末行。Windows/macOS 画布把超过触摸阈值的左键拖动解释为区间选择，按修饰键选择 `Replace`、`Add` 或 `Subtract` 调用核心 `select_range`，命中按单元计算，松手提交，期间绘制只读预览带；选择模式内 `Shift`/`Alt` 点击同样换算为对应模式的单行区间。`Delete` 调用核心 `delete_selection`，只在选择会话内可用。文件、帮助、导出与 Scale 快捷键与对应按钮/菜单共用门控：画布只持有屏幕传入的打开草稿、保存、另存为和导出回调，不复制保存、导出或对话框流程；`F1` 复用帮助按钮的门控与提示生命周期；Scale 键调用已有的 `setScale`。会话内右键只在有选择时调用核心 `clear_selection` 并保留选择模式，空选择或待粘贴时退出会话，普通编辑状态下不响应。

Zoom 只改变纵向时间密度，Scale 只改变波形和谱面对象区域的横向显示比例，两者都不得修改 PCM、BPM、Offset 或谱面位置。Flutter 初始使用自动编辑 Zoom：枚举核心支持的 `1..19`、`0.25` Zoom 级别，消费核心返回的 `pixelsPerSecond`，选择使当前非零 BPM 下 1/16 行距最接近 `46` 逻辑像素的级别；并列时选择较小 Zoom。初始空工作区和成功替换音频启用自动状态，自动状态下 BPM 变化重新选择；BPM 为零保留当前值。用户显式调整 Zoom 后转为当前音频的手动状态，后续 BPM 变化保留用户值，成功替换另一音频才恢复自动状态。Flutter 不得复制核心 Zoom 到 `pixelsPerSecond` 的指数公式。

Scale 不参与本 App 圆形和菱形标记尺寸计算。Windows/macOS 可以在 `1..10` 内调整 Scale；iOS/Android 固定为 `4.0` 并只做屏幕宽度响应式排布。该平台输入差异只影响视图，不能改变序列化后的 `ChartDraft` 或任何导出结果。

Flutter 的移动端画布为右侧纵向控制栏保留 `24` 逻辑像素投影留量：波形、三条标记轴和标题在 `[canvasLeft, canvasRight - 24]` 内共同居中和等比适配，三列命中边界继续取相邻标记轴中点，最外侧边界仍延伸到完整画布左右边缘。桌面投影不使用该留量。该规则只改变 Flutter 视图坐标和命中列宽，不改变核心列语义、谱面时间行、Scale、波形 Alpha 数据或编辑命令结果。

时序校准网格与波形生成相互独立。共享核心时序转换根据权威 BPM、Offset 和拍号数据确定行/时间位置；Flutter 把这些位置绘制为小节、拍和音符细分引导线。小节线使用 `2` 逻辑像素、约 `52%` 中性色，拍线使用 `1.25` 逻辑像素、约 `30%` 中性色，音符细分线维持 `1` 逻辑像素、约 `7.5%` 中性色。播放期间 Flutter 仅绘制小节线，隐藏拍线和音符细分线；暂停、自然结束或输出中断后恢复完整网格。该显示条件只属于 Flutter 视图层，不改变核心网格集合、行命中、吸附或任何编辑结果。时序更新只重算网格位置，不修改 PCM 或波形图块。BPM 为零时不生成网格线，但画布使用上述非持久化投影继续提供确定性的 1/16 行命中。

播放位置由固定的当前时间光标/播放判定线表示。该线的绘制和行命中共享同一 Flutter 投影，并固定在编辑画布高度 `25%` 处。开始播放时使用该光标时间；暂停时保留精确播放位置。平台适配层约每 `33ms` 查询一次实际播放帧，`EditorController.currentRow` 只由这些设备帧更新，并继续作为暂停、自然结束和输出中断位置的唯一权威来源。

Flutter 底部状态栏把 `EditorController.currentRow` 通过共享核心 `sixteenth_row_to_seconds` 换算为播放光标对应的歌曲时间，只在展示层按 ArrowVortex 的 `mm:ss.SSS`、累计分钟、负号和毫秒截断规则格式化；不得在 Flutter 复制 BPM/Offset 换算公式。BPM 为零时使用与画布行命中相同的非持久化 `100` BPM 投影，不能改变权威 BPM。未播放时点击歌曲时间由 Flutter 收集非负整数分钟和 `[0, 60)` 秒数，`EditorController.moveToSongSeconds` 再调用共享核心 `seconds_to_sixteenth_row`，最后复用统一的 `moveToRow` 时间线边界；取消、校验失败或播放门控拒绝均不改变当前位置。

播放期间，Flutter 视图层以最后确认的设备行作为锚点，使用屏幕 `Ticker` 按显示刷新帧投影仅供绘制的连续行，让波形、时序网格、Showtime 范围和标记在固定判定线下同步移动。视觉投影最多前推 `50ms`，且重复收到同一设备帧不能重置这个窗口；新的有效设备位置重新校准锚点，暂停、自然结束或输出中断则立即停止 `Ticker` 并把绘制行恢复为 `EditorController.currentRow`。该投影不得写回控制器、核心、项目或音频输出。逐帧更新只通知画布 `CustomPainter` 并限制在其 `RepaintBoundary` 内，不能按屏幕帧率重建整个编辑界面。到达音频末尾时暂停并保留末尾位置；从末尾再次开始播放时，先跳到开头。

播放画布另使用 `min(editPixelsPerSecond, 100)` 逻辑像素/秒的时间投影，并以同一 BPM 时间转换得到播放态 `rowSpacing`；因此编辑密度不低于该上限时，5 秒时间范围恒为 `500` 逻辑像素，较慢的手动编辑 Zoom 不会被放大。`100` 是共享核心 UI Zoom `6.73` 返回的整数 `pixelsPerSecond`。音频加载事务同时通过共享核心生成并缓存 Zoom `6.73` 的 Alpha 波形；播放速度被限制时，Flutter 直接选择该缓存，使源波形 `pixelsPerSecond` 与实际播放投影完全一致，不在 Dart 中重采样更高密度的编辑波形。编辑速度低于上限时继续使用编辑波形；Scale 变化重建两套缓存，编辑 Zoom 或 BPM 变化只重建编辑缓存。该切换不写回 Zoom，也不进入草稿或撤销历史；播放期间状态栏显示实际播放视图速度，停止播放后恢复编辑波形、`rowSpacing` 与用户 Zoom 显示。

共享核心返回时间线范围，不由 Flutter 扫描绘制对象猜测。基础编辑范围固定为四小节，即 16 拍、64 个 1/16 行；默认 BPM `100`、Offset `0` 时对应 9.6 秒。时间线末尾取基础范围末尾、已加载音频结尾、最后一个谱面事件位置以及全部 `SHOW-D.rangeEnd` 中的最大值。没有音频和谱面事件时只使用四小节基础范围；“跳到末尾”和向后翻页的边界都消费同一结果。

应用操作协调层把 `Playing` 作为显式的全局交互门控状态。该状态下只接受暂停播放与切换波形可见性；音效设置、音频、谱面、BPM/Offset、导入导出、草稿持久化、撤销/重做、导航、时间线、Zoom 和 Scale 的其他请求统一返回 `PlaybackActive`，且不调用后续领域命令。Flutter 的按钮、手势、键盘、鼠标和无障碍入口都必须查询同一门控状态并展示禁用样式，不能各自维护不一致的白名单。切换波形可见性是纯视图操作，不进入核心项目数据；暂停仍由共享播放状态执行并保留精确位置。

当前纯查询入口 `coordinate_application_operation` 接收互斥的 `Idle = 0`、`Playing = 1`、
`ExportingAudio = 2` 或 `ChartInteraction = 3` 模式；`ChartInteraction` 统一表示选择与
待粘贴会话。操作类为默认保守的 `General = 0`、`PausePlayback = 1`、
`ToggleWaveformVisibility = 2`、`CancelAudioExport = 3`、`SelectionCommand = 4`、
`NavigationOrView = 5` 和 `HistoryNavigation = 6`。门控真值表固定如下：

- `Idle` 允许全部合法操作类。
- `Playing` 只允许 `PausePlayback` 与 `ToggleWaveformVisibility`，其余返回 `PlaybackActive`。
- `ExportingAudio` 只允许 `CancelAudioExport`，其余返回 `AudioExportActive`。
- `ChartInteraction` 允许 `SelectionCommand`、`NavigationOrView`、`HistoryNavigation`、
`ToggleWaveformVisibility` 与 `PausePlayback`；`General` 和 `CancelAudioExport`
必须返回 `ChartInteractionActive = 5`，稳定消息为 `chart interaction is active`。

必须先校验操作类：未知操作类在任何模式（包括未知模式）下都返回 `InvalidOperation`；
操作类合法后才校验模式，未知模式返回 `InvalidMode`。该入口不执行命令，也不得修改
项目、播放、导出、视图或编辑器会话状态。

Flutter `EditorController` 的私有模式阶梯必须按播放 &gt; 导出音频 &gt; 选择/待粘贴会话 &gt; 空闲
的优先级投影到上述模式。`generalActionsEnabled` 查询 `General` 并作为非白名单入口的
统一禁用条件，会话中必须为假；`selectionCommandEnabled`、`navigationEnabled` 和
`historyNavigationEnabled` 分别查询对应操作类。标记输入能力必须使用
`markerInteractionEnabled = generalActionsEnabled || selectionCommandEnabled`，再由当前
交互模式路由普通编辑、选择或平台允许的粘贴操作，不能把选择态标记激活变成普通编辑。
各命令继续叠加既有 `isBusy` 和自身前置条件；Flutter 不得自行重写核心真值表。

进入选择或待粘贴会话要求非播放且非忙碌，开始普通播放、节奏测试和音频导出要求一般
操作门控允许，因此会话与播放、音频导出互斥。`PausePlayback` 在会话态只作为防御性
逃生口保留，右侧播放/暂停入口仍禁用，不得据此在会话内启动播放。播放按钮与节奏测试
按钮的会话禁用 tooltip，以及空格或直调尝试播放时的提示，统一为
`请先退出选择或粘贴模式`。按钮、手势、键盘、无障碍、已打开菜单或浮窗与控制器直调
必须共同消费相同门控，被拒绝的请求不得调用后续命令或改变状态。

`NavigationOrView` 包含时间线移动、Zoom、Scale、翻页与首尾导航、歌曲时间跳转和波形
设置面板，因此会话中可用、播放中禁用。波形显示/隐藏必须单独分类为
`ToggleWaveformVisibility`，因为播放期仍允许该纯视图开关，但不得打开设置面板或修改
滤波等其他选项。音效设置、音频与谱面操作栏、恢复、诊断导出及开始播放均属于
`General`，会话中必须禁用；撤销/重做只使用 `HistoryNavigation`，不得随一般编辑入口
一起禁用。

macOS 触控板手势只在 Flutter 输入适配层解释。系统滚动事件和 `PointerPanZoom` 事件中以纵向为主的增量转换为现有时间线像素移动，以水平方向为主的增量不映射到时间线、Scale 或其他功能；系统已提供的动量事件可继续消费，Flutter 不另建惯性模型。捏合手势把起始 Zoom 与系统累积缩放量投影为临时纵向显示比例，仅用于 Flutter 连续预览；固定当前时间光标不动，手势结束后才通过现有 Zoom 视图命令提交最近的 `0.25` 级别并重建波形。临时预览不进入核心、项目数据或撤销历史，不改变 Scale。旋转、横向滑动和 macOS 保留的多指系统手势不建立 App 命令。

移动端单指拖动空白画布时，更新时间线位置且不使用惯性。双指缩放围绕固定当前时间光标连续预览，并在松开时提交到最近的 `0.25` 参考缩放级别。播放期间由全局门控拒绝时间线拖动和双指缩放命令。谱面编辑对象的命中测试优先于时间线手势。

翻页导航通过共享时序转换，从当前精确视口位置计算目标。移动距离为可见拍数减一拍，从而精确保留一拍重叠；结果只在越界时限制到开头或末尾，不能再执行拍线、小节线或 1/16 行吸附。每次离散激活只执行一次翻页，长按不重复触发。位于边界或继续翻页不能改变位置时，对应的“到开头/上翻页”或“到末尾/下翻页”禁用。悬浮控件条固定在右侧、不允许重定位、不自动隐藏；Windows 和 macOS 由 Flutter 在编辑画布内垂直居中，移动端由 Flutter 避让平台安全区域并保持现有顶部起始位置。播放期间四个导航操作全部由全局门控拒绝，只保留波形开关和暂停可用。

平台生命周期或音频焦点中断事件必须转换为暂停请求；核心/播放协调状态保存中断时的精确位置。回到前台或重新取得音频焦点不能自动恢复播放。音频自然播放到末尾后状态立即转换为暂停，随即解除播放态交互门控。

播放期间的屏幕常亮属于 Flutter 层的平台显示电源偏好：普通播放或节奏测试开始成功后启用，暂停、自然结束、播放失败、进入后台或离开编辑器时关闭。该偏好不进入 `av_core`、`ChartDraft`、项目文件或撤销历史，也不参与播放门控判定；平台实现不可用或设置失败必须被忽略，不能中断或阻止播放。

## 开发顺序

1. 建立 CMake、CTest、依赖版本固定和许可证声明。
2. 导入最小必要音频解码代码并定义规范 PCM。
3. 捕获 ArrowVortex 解码器黄金夹具。
4. 提取波形生成并捕获 Alpha 图块黄金数据。
5. 提取起音、BPM 和 Offset 检测并捕获结果黄金数据。
6. 添加可直接调用的命令行工具。
7. 添加稳定 C ABI 和一致性测试。
8. 搭建 Flutter 脚手架并连接已测试 ABI。
9. 逐步添加时序、SLK、原生项目和编辑功能，并为每步增加测试。

不要一开始就移植完整 ArrowVortex。每次只导入一个行为切片，先建立一致性，再移除 UI 和全局状态依赖；不要把这些工作与算法行为变更合并。
