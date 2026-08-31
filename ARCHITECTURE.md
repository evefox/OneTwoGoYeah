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
`validate_slk_position`。三者都返回完整 `SlkPosition { measure, beat, sixteenth,
total_sixteenth }`，导出器不能自行拼装第四列。输入与结果使用非负 `uint64`；负号、
空字段和非十进制文本属于后续解析器错误。组合入口先校验两个分量 `0..3`，再用
显式上界检查避免 `measure * 16 + ...` 溢出。反向入口对任意 `uint64` 总位置都能
唯一分解，包括最大值。严格校验入口先执行同一组合逻辑，再逐整数比较第四列；
不一致返回 `InconsistentTotal`，不返回修复值。

当前底层记录入口为 `parse_slk_records(bytes, byteCount)`。它把输入视为不透明字节，
只按 ASCII 控制记录扫描，不把标题或格式表解码为 Unicode。文件必须以
`ID;PWXL;N;E\r\n` 开始、全部使用 CRLF，并以唯一末行 `E\r\n` 结束。解析器接受
两份兼容示例实际使用的 `P/F/B/O/NN/C` 前导记录族，但要求工作表第 2 行的六列类型
声明逐记录匹配 `int/int/int/int/enum(n,s,c,d,r)/int`。未知记录、裸 LF、截断行或
不完整单元格组都返回稳定来源行错误。

谱面数据必须从 sheet row `3` 连续递增，每个底层行严格由 X1..X6 六条相邻 `C`
记录组成；数据行之间只允许参考文件实际使用的 `F` 格式记录，最终数据行恰好位于 sheet row `162/314` 时也允许紧随对应固定 `F` 记录。四个位置整数调用
`validate_slk_position`，位置必须严格递增。类型只接受带双引号的小写 `n/s/c/d/r`；
`n/s/r` 只允许值 `0`，`c` 接受非负整数，`d` 要求非零。唯一 `[r & 0]` 必须是最后
一个谱面数据行；除上述最终固定 `F` 记录外，之后只能出现容器 `E`。成功结果拥有包含边界行在内的有序
`SlkParsedRow` 数组，不引用输入字节；复制为深复制，移动转移所有权。失败返回稳定
错误、物理行号、sheet row 和列号且不暴露部分数组。此层只保证记录与底层行合法，
不推导 NPC/玩家角色、演练组、舞步或 Showtime 关联。

SLK 只保存有序的底层行值，不显式保存 NPC/玩家角色、`DrillGroup` 标识、`DanceMoveDrill` 标识或 Showtime 关联。因此语义模型包含的信息严格多于交换格式。

SLK 块识别和角色分配属于 `av_core` 的语义分析，不属于底层行解析器。在成品谱面中，共享谱面模型把聚合对象命名为 `DrillGroup`（演练组）。一个成品 `DrillGroup` 由两个紧邻且完全相同的块组成；每个块包含一个 `[c & 非0]`、一个或多个 `[n & 0]`，以及零个或一个结尾 `[s & 0]`。整份谱面的分析结果显式表示以下有序角色：

- 第一个块：NPC 演示
- 第二个块：玩家练习

成功解释成品谱面后，已识别块中的每个条目都继承该块的角色和 `DrillGroup` 标识。同一种局部 `[c & 非0] + [n & 0]...` 结构会因整份谱面的上下文不同而属于不同角色，因此严格 SLK 导入器只有在整份谱面通过校验后才推导角色。Flutter 和其他适配层必须使用核心返回的分组和显式角色，不得根据行顺序自行推断。兼容导出必须保持 NPC 在前、玩家在后的顺序。原生 `ChartDraft` 仍可表示不完整或不匹配的演练组，因为所有已经存在的角色敏感对象都会保留显式角色，与草稿当前能否导出无关。

当前第一层入口为 `analyze_slk_drill_groups(records, beginRowIndex)`。它只分析一个普通演示/练习序列，并在第一个 `[c & 0]` 或 `[r & 0]` 之前停止，返回未消费的结束行索引和结束类型，供后续 Showtime 层继续解释。块的类型和值逐条相同；动作位置则以每块首个 `n` 为原点比较全部 `n/s` 相对位置。`c` 只作为块锚点，NPC/玩家各自从 `c` 到首个 `n` 的间隔不参与内容匹配，这是兼容 `谎话成品.slk` 所必需的现有格式行为。

分析成功时，第一个块固定为 NPC、第二个块固定为玩家；`DrillGroup` 标识确定为 NPC 起始 `c` 的 sheet row，在同一已解析文档中唯一且可复现。每个自有结果条目同时保存来源底层行索引、块内索引、角色和组标识，不引用输入数组。孤立块、缺少 `n`、块长度/类型/值/动作相对位置不一致或没有 Showtime/文件边界都原子失败，只返回稳定错误和来源 sheet row，不暴露部分分组。此层不解释 `c` 数量编码、不拼接 `DanceMove`、不关联 Showtime，也不构造 `ChartDraft`。

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

`Showtime` 识别和舞步关联同样属于 `av_core`。在成品谱面中，`[c & 0]` 标记结束紧邻其前的有序演示/练习阶段，并开始对应的 Showtime。随后连续出现的非零 `d` 条目按顺序与该阶段的 `DanceMoveDrill` 一一对应。每个 `d` 值必须等于所引用 `DanceMove` 的 `moveNCount`。共享模型为每个 `SHOW-D` 显式保留关联和自身的 `showNCount`：严格导入时它来自已校验的 `d` 值，自动补全时它是生成当下所引用 `DanceMove.moveNCount` 的快照。关联只用于来源追踪和成品校验，不是实时数据绑定；Flutter、C ABI 和其他适配层不得通过自行扫描 SLK 行重新建立关联。兼容导出先写 Showtime 标记，再写有序 `d` 条目。

当前完整语义入口为 `analyze_slk_showtimes(records)`。它从底层第一个谱面行开始，对每个普通阶段调用上述唯一分组和舞步演练入口；普通阶段必须非空且必须紧随 `[c & 0]`。其后连续 `d` 的数量必须与该阶段的有序 `DanceMoveDrill` 数量相同，且逐项值等于 `moveNCount`。Showtime 标识取 `c0` 的 sheet row，`SHOW-D` 标识取自身 sheet row；每个自有 `SHOW-D` 同时保存来源底层行、阶段内索引、所引用演练标识和来自已校验 `d` 的 `showNCount`。

分析器继续从 `d` 后第一行解释下一普通阶段，最终只接受唯一末行 `[r & 0]` 并把它作为已消费的格式边界返回，不创建领域事件。空谱面、空普通阶段、缺少 Showtime、分组或舞步分析失败、`d` 数量/值不匹配、缺少或非末行边界均原子失败。失败携带绝对来源行、sheet row、Showtime/阶段组/`SHOW-D` 索引及下层稳定错误，不暴露任何部分 Showtime。

严格字节事务入口为 `import_finished_slk(bytes, byteCount)`。它不增加第三套格式逻辑，只顺序调用 `parse_slk_records` 和 `analyze_slk_showtimes`；底层记录仅作为事务内临时值存在。两层全部成功后才发布自有 `FinishedSlkDocument`，其中完整语义不引用输入字节或临时记录。失败明确区分 `RecordParseFailed` 与 `SemanticAnalysisFailed`，只填充对应下层结构化错误，另一层错误保持 `None`，且结果不包含部分文档。

每个 Showtime 舞步引用的派生时长由该 `SHOW-D` 自身的 `showNCount` 计算：`1..6` 对应一小节，`7` 及以上对应两小节。在固定 4/4、每拍四个 1/16 的谱面坐标中分别为 16 和 32 个 1/16 位置。该计算只在 `av_core` 实现，UI、C ABI 和导出器只消费结果；原生项目不得再保存一个可能与 `showNCount` 冲突的可编辑时长副本。修改所关联舞步的当前 `moveNCount` 不得回写已有 `SHOW-D.showNCount`，因此也不得自动改变其位置或展示范围。生成时无法取得舞步数量则不能生成对应的 `SHOW-D`；生成后的不匹配由成品校验报告。

`SHOW-D1` 和 `SHOW-D+` 是从 `SHOW-C` 后连续 `SHOW-D` 序列派生的查询角色，不进入持久化事件类型。核心根据 `dRow` 所在小节起点与派生一/二小节时长返回左闭右开的展示范围 `[rangeStart, rangeEnd)`。位置验证只允许 `SHOW-D1` 位于小节线或小节线 +1；所属 `SHOW-C` 不在小节线时进一步限制为小节线。`SHOW-D+` 始终位于小节线，并且不能落入前一个 `SHOW-D` 的展示范围。后续位置统一等于前一展示范围的 `rangeEnd`。

核心格式适配层把最后一个 Showtime 之后的 `[r & 0]` 作为兼容 SLK 文件边界，并在兼容导出末尾自动写出该行；它与 SYLK 容器结束行 `E` 不同。严格 SLK 导入会验证并消费该边界，但不得为它创建 `ChartDraft` 事件。领域模型、C ABI 编辑接口和 Flutter 投影均不暴露对应类型，编辑器的相邻查询、上下文点击和撤销历史都把它视为不存在。

导出器不保存或复用导入文档的边界位置。规范模板的 `1920` 是最小边界；导出时计算最后一个 `SHOW-D` 的 `[rangeStart, rangeEnd)`，并固定使用 `max(1920, rangeEnd)` 作为 `r` 的总 1/16 行。该位置再调用共享 `decompose_slk_position` 生成四列。最后一个展示范围超过模板边界时自然延伸，未超过时保持模板字节往返所需的 `1920`。

## SLK 规范字节导出契约

成品 SLK 导出只有一个规范容器模板：`example/z3116_lv5.slk`，其当前 SHA-256 为 `8d203601373b3be426d220f002d8d945c7d94b8c44670cbee4ceab82dd723a12`。`example/谎话成品.slk` 只用于导入和语义测试，不能作为另一种导出外壳或按来源选择的模板。生产实现不得在运行时依赖 `example` 目录；脚手架建立时应把经哈希校验的模板字节或等价生成资源固定在 `av_core` 中，并由同一核心序列化器使用。

模板固定部分按不透明字节处理，不能先按 UTF-8、CP949、系统 ANSI 或其他字符集解码后再编码。以下字节级属性固定：

- 文件以 `ID;PWXL;N;E\r\n` 开始，不带 BOM。
- Excel/SYLK 前导、格式表、列宽、筛选定义、标题行、类型行及固定记录的字节、顺序和引号形式来自规范模板。
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

兼容成品 SLK 序列化只接受已经成功校验的 `FinishedChart` 快照。角色和关系通过规定的成品顺序隐式编码，不添加非标准 SLK 角色字段。正式草稿序列化接受 `ChartDraft`、权威 binary64 BPM 和权威 binary64 Offset，并且必须在不要求成品有效性的情况下，精确往返不完整谱面内容、对象标识、显式角色、语义关系以及两个时序值。它不保存音频、缓存、播放位置、撤销历史或视图状态。因此正式草稿仍能保存 SLK 不包含的必要语义，但其负载范围严格限于谱面数据、BPM 和 Offset。App 可以持续校验以提供反馈，但校验结果不得阻止继续编辑或保存正式草稿。

当前工作区来源使用显式状态表示，不能根据名称或文件扩展名反推：

```text
WorkspaceSource = Untitled
                | ImportedSlk { sourceName }
                | SavedDraft { draftId, draftName }

WorkspaceState  = { source, dirty, chartDraft, audioTiming, viewState }
```

App 启动时直接创建 BPM 为精确 `100`、Offset 为精确 `0` 的 `Untitled` 空草稿。`SavedDraft` 的主保存操作使用稳定 `draftId` 原子覆盖对应草稿；`Untitled`、`ImportedSlk` 以及任何 `另存为` 操作都会创建或明确覆盖一个草稿，并在成功后切换为对应 `SavedDraft`。`dirty` 表示当前 `ChartDraft`、权威 BPM 或权威 Offset 中至少一项与当前正式草稿的已保存负载不同；BPM/Offset 不进入撤销历史，但其修改仍会产生未保存状态。导出 SLK 不改变 `source` 或 `dirty`。清空只修改内存中的谱面数据并设置 `dirty`，不得直接删除或覆盖正式草稿。

加载正式草稿时，只以事务方式替换 `ChartDraft`、权威 BPM、权威 Offset 和工作区来源；已加载音频、规范 PCM、波形、分析缓存、来源元数据、品质上限及其他音频会话状态保持不变。严格导入 SLK 时，文件不提供 BPM/Offset，因此成功事务只替换 `ChartDraft` 和工作区来源，当前音频、BPM、Offset 及音频派生状态全部保留。两种事务失败时都保持完整当前状态不变。

`[c & value]`、`[n & 0]`、`[s & 0]` 等原始 SLK 组合只是序列化边界表示，不足以作为原生语义标识。原生模型使用带角色的块和事件；查询任何角色敏感对象时，无需重新扫描类似 SLK 的相邻数据就必须能得到 NPC 或玩家角色。即使两个对象会序列化为相同原始组合，只要角色不同，它们仍是不同对象。拆分 `cValue` 和 Showtime `[c & 0]` 在导入/导出边界与显式语义结构相互转换或由其派生；`[r & 0]` 仅由格式适配层消费或生成，不进入原生草稿模型。

当前原生只读导入投影为 `project_finished_slk_to_chart_draft(document)`。`ChartDraft` 分开拥有带 `ChartRole` 的 `BlockStart/Note/MoveEnd` 事件、Showtime 起点事件、带 `showNCount` 和演练引用的 Showtime 舞步事件，以及 `DrillGroup`、`DanceMoveDrill`、Showtime 三层关系。导入生成的各类初始稳定标识取对应来源 sheet row，但投影后所有查询只使用原生字段和关系，不再读取 SLK 类型字母、相邻顺序或来源记录。

每个角色事件直接保存角色、连续 1/16 行、演练组标识和舞步演练标识；每个演练组分别列出 NPC/玩家事件标识；每个舞步演练列出有序演练组；每个 Showtime 列出有序舞步演练和 `SHOW-D` 事件。投影保持全局时间顺序并深复制全部数据，格式边界 `[r & 0]` 不生成任何事件、标识或关系。当前 `ChartDraft` 容器不在构造时施加成品约束，因此后续编辑仍可保存不完整状态；只有这个导入投影的输入因为来自 `FinishedSlkDocument` 而必然完整。

当前成品边界为 `validate_finished_chart(draft)`。它只读校验事件 ID 与全局时间行唯一性、角色块结构与匹配、演练组/舞步演练/Showtime 引用、完整 `moveNCount`、结尾位置和 `SHOW-D.showNCount`，并报告带对象、关联对象和时间行上下文的稳定诊断。草稿不会被修复、重排或修改；只有零诊断时才深复制生成 `FinishedChart`，分配失败与领域诊断分开返回。

`FinishedChart` 不能由调用方直接构造，只能由上述零诊断校验边界创建。校验还要求每个 `SHOW-C/SHOW-D` 事件恰好被一个 Showtime 关系拥有，并要求按照 Showtime、舞步演练、演练组、NPC 块、玩家块、`SHOW-C`、有序 `SHOW-D` 展平后的全部事件位置严格递增；最后一个 `SHOW-D` 的展示范围计算不能溢出 `uint64`。因此兼容导出只消费快照和关系顺序，不重新修复或猜测草稿结构。

规格与 UI 使用固定语义简称 `NPC-C`、`NPC-N`、`NPC-S`、`PLY-C`、`PLY-N`、`PLY-S`、`SHOW-C` 和 `SHOW-D`。这些简称是一对一的展示标签，不是持久化枚举字符串，也不能替代原生事件类型和显式角色。核心 API 应返回稳定类型和角色字段，由适配层映射展示简称；不得通过解析简称字符串执行领域操作。SLK 文件边界没有编辑简称或事件类型。

上下文放置规则需要由核心提供确定性的相邻事件查询。“上一个标记”在全部三列中查询位置严格小于点击位置的最近事件；“上一个本列标记”先按点击列过滤，再执行相同查询；“下一个标记”在全部三列中查询位置严格大于点击位置的最近事件。查询基于规范谱面时间位置，不依赖屏幕坐标、滚动方向或 UI 绘制顺序。当前位置不计入相邻查询；没有候选时返回显式空结果。

当前只读入口 `project_chart_markers(draft)` 把三个底层事件容器统一投影为按行和稳定事件 ID 排序的 `ChartMarker` 自有数组；列和 `NPC-C/N/S`、`PLY-C/N/S`、`SHOW-C/D` 类型均从权威角色/事件类型派生，不保存第二份列状态。`previous_chart_marker`、`previous_chart_marker_in_column` 和 `next_chart_marker` 只消费该数组，底层容器顺序不影响结果。`project_showtime_moves` 从相同全局顺序派生连续 `SHOW-D` 的首尾状态，并从自身 `showNCount` 生成展示范围；`chart_note_ordinal` 按同角色最近 `S` 边界派生 N 编号。所有结果拥有独立内存，不引用 `ChartDraft` 容器。

`ChartDraft` 对规范谱面时间行施加全局唯一约束：一个时间行在 NPC、玩家和 Showtime 三列合计最多拥有一个事件。该约束必须由 `av_core` 的所有上下文放置和自动生成命令统一维护，不能仅由 Flutter 命中测试或成品校验阻止。点击某列的空白区域但同一时间行已被其他列占用时，核心返回稳定的“位置已占用”结果，不替换已有事件，也不提交部分修改。

所有自动生成器都是一次性编辑命令，不是响应式约束系统。命令只使用开始执行时的 `ChartDraft` 快照计算结果，并把生成事件、必要数值快照、来源关系和一条历史原子提交。提交之后，来源关系仅服务于来源追踪与校验，不能触发观察器、级联命令或隐式历史；后续编辑不得自动移动、增加、删除、转换或重新生成其他存活事件，也不得因为会造成演示、练习、舞步或 Showtime 不匹配而拒绝。删除源对象时可以清理悬空关系，但必须保留其他存活事件。原生草稿序列化与恢复必须原样往返这些允许不匹配的内容，只有 `FinishedChart` 构造和兼容 SLK 导出要求它们重新一致。

NPC 列上下文点击是 `av_core` 中的领域命令，不是 Flutter 条件分支。空白行命令接收规范时间位置，按功能规格的固定优先级查询前后事件并返回放置 `NPC-C`、`NPC-N`、`NPC-S` 或稳定错误码。无上一个事件时放置 `NPC-C`。`NPC-N` 的自动结束分支只统计点击位置之前、最近 `NPC-S` 之后的 NPC 列事件，跳过 `NPC-C` 和其他列事件；少于 11 个时放置 `NPC-N`，达到或超过 11 个时放置 `NPC-S`。若全局下一个事件是 `NPC-N` 或 `NPC-S`，则优先放置 `NPC-N` 并跳过数量限制。

点击已有 `NPC-C` 的领域命令以该事件为起点，在全局时间顺序中删除随后连续的 `NPC-N/NPC-S`，遇到任何其他事件停止。事件删除、失效关系清理和历史记录必须形成一个原子事务；玩家和 Showtime 的存活事件不得级联删除，失去完整关系的内容继续作为草稿保存。点击已有 `NPC-N` 只改变其类型为 `NPC-S`，点击已有 `NPC-S` 只删除该事件。Flutter 只负责命中事件、发送事件稳定 ID 或空白行位置、展示结果与本地化错误文本，不得重复实现判断或扫描算法。

玩家列上下文点击和自动生成同样属于 `av_core`。只有上一个全局事件为 NPC 列事件时，空白行点击才创建 `PLY-C`；上一个事件为 `PLY-C` 时，点击位置成为首个 `PLY-N` 的目标锚点；其他情况返回稳定的不可放置错误。生成器以当前 `PLY-C` 之前最近的 `NPC-C` 为源块开始，以两者之间的第一个 `NPC-N` 为源锚点，并读取从该锚点开始的全局连续 `NPC-N/NPC-S`。各目标行通过源行相对源锚点的整数 1/16 行差平移得到，事件类型按 `NPC-N -> PLY-N`、`NPC-S -> PLY-S` 映射。找不到源锚点时返回稳定错误，保留 `PLY-C`，不生成部分结果。

自动生成器必须先计算完整目标事件列表及首尾闭区间，再检查该区间内三列的任何已有事件。区间内任一行已占用时，整个生成事务失败；不得仅检查目标落点，也不得提交无冲突的子集。成功时，事件创建、来源关系建立和单条历史记录一次提交。该来源关系不建立实时联动，后续 NPC 修改不得改变已经生成的 `PLY-N/PLY-S`。失败时不改变事件、关系或历史，但此前独立存在的 `PLY-C` 保持不变。

点击已有 `PLY-C` 会原子删除自身及随后全局连续的 `PLY-N/PLY-S`。点击上一个全局事件为 `PLY-C` 的首个 `PLY-N` 时保留 `PLY-C`，原子删除从该事件开始的连续 `PLY-N/PLY-S`，用于重新定位；其他 `PLY-N` 和所有 `PLY-S` 返回自动生成事件不可编辑错误。多事件删除必须清理失效关系，但不得级联删除 NPC、Showtime 或其他存活事件。Flutter 不得自行复制间隔、检查生成区间或判断首个 `PLY-N`。

Showtime 列点击、位置验证、舞步收集和自动补全全部属于 `av_core` 领域命令。`PLY-S` 后的空白点击创建 `SHOW-C`；小节线上的 `SHOW-C` 使用下一 1/16 位置作为自动补全起点，非小节线 `SHOW-C` 使用后续第一条小节线。已有 `SHOW-C/SHOW-D` 后的空白点击只在位置验证成功后作为补全起点。其他上下文返回稳定不可放置错误。

补全器从所属 `SHOW-C` 后已有连续 `SHOW-D` 数量确定舞步索引，并取得上一 Showtime 阶段结束后到当前 `SHOW-C` 前、按时间正序排列的完整 `DanceMoveDrill`。不完整演练不进入候选。扣除已有数量后，剩余列表为空时返回稳定错误；否则为所有剩余舞步创建显式引用，把生成当下的 `moveNCount` 复制为各自的 `showNCount`，并由该快照派生 `d` 值、时长、展示范围和后续行位置。提交后不再从关联舞步刷新这些结果。

补全在提交前计算完整事件与从首个 `rangeStart` 到最后一个 `rangeEnd` 的半开范围。除所属 `SHOW-C`，以及最终连续 `SHOW-D` 的最后半小节 `[rangeEnd - 8, rangeEnd)` 中合法存在的 `NPC-C` 外，范围内任何已有事件都使整个补全事务失败。后一例外只能应用于补全结果的最后一个 `SHOW-D`。成功生成一次提交事件、关系和历史；新建 `SHOW-C` 后补全失败时只保留该 `SHOW-C`。Flutter 只绘制核心返回的范围与结果，不得重新计算候选、位置或冲突例外。

NPC 空白点击命令在创建 `NPC-C` 前查询核心维护的 `SHOW-D` 展示范围。通常命中范围即拒绝；只有命中某个连续 `SHOW-D` 序列最后一个事件的 `[rangeEnd - 8, rangeEnd)` 时允许继续执行普通 NPC-C 上下文规则。范围端点 `rangeEnd` 本身不需要例外。该判断不能由 UI 像素命中代替。

点击已有 `SHOW-C/SHOW-D` 的命令原子删除自身及随后全局连续的 `SHOW-D`，遇到其他事件停止，并同步清理失效引用。

当前可撤销领域入口为拥有单一 `ChartDraft` 的 `ChartEditor`。`click(column, row)` 先消费统一 `ChartMarker` 投影执行全局单行占用检查，再在候选草稿副本中完成 NPC、玩家或 Showtime 的全部事件与关系变化；只有完整结果可提交时才替换权威草稿。每次成功点击、整批生成、整段删除或清空把修改前的完整草稿作为一条历史，撤销/重做因此逐值恢复事件、稳定标识、显式角色和全部关系；失败不创建历史，新修改清空重做分支。历史和会话内标识分配状态不进入项目持久化。

编辑器创建 `NPC-C` 时同时创建或延续显式 `DrillGroup`/`DanceMoveDrill`：前一舞步尚未出现 NPC `S` 且未被 Showtime 使用时，新组延续该舞步；否则开始新舞步。NPC `N/S` 与玩家生成内容直接加入对应关系。NPC 修改只重算当前舞步的 NPC `N` 总数，不移动或重建既有玩家/Showtime；删除只从关系数组移除已删除事件引用，其他角色内容和允许不完整的关系对象继续保留。

Showtime 补全只选择上一 Showtime `SHOW-D` 边界之后、当前 `SHOW-C` 之前按关系顺序完整且 NPC/玩家块匹配的 `DanceMoveDrill`。已有 `SHOW-D` 必须对应候选前缀，生成器从余下候选复制当前 `moveNCount` 为独立 `showNCount`，预先计算全部锚点和展示范围并一次提交。新建 `SHOW-C` 后没有候选或范围冲突时只提交 `SHOW-C` 的单条历史；手动补全的相同失败不修改草稿。

Flutter 根据 `ChartDraft` 和校验结果生成展示投影。每个被绘制、选中或编辑的角色敏感项目都必须从模型取得显式角色。视觉样式可以区分 NPC 和玩家，但颜色、轨道、顺序或组件位置不得成为角色真值来源。展示投影不是第二份谱面模型，也不得重复导入、分组或导出算法。

SLK 导入比原生项目加载更严格。它会拒绝文件级解析错误、两种位置表示不一致、不支持的第 5/6 列组合，以及任何违反成品谱面语义规范的情况。它绝不会把可解析但不完整的 SLK 行转换成原生草稿。未来出现的未知行类型，在规格明确采纳之前一律不支持。

谱面校验不属于音频操作的前置条件。即使谱面草稿为空或无效，音频解码、波形生成、时序调整、播放、分析和 Ogg 导出仍然可以工作。

## 原生草稿存储与异常恢复契约

正式草稿和异常恢复快照都使用版本化的原生项目序列化容器，但采用不同负载、身份和生命周期：

```text
FormalDraftPayload = { chartDraft, bpmBinary64, offsetBinary64 }
RecoveryPayload    = { chartDraft }
```

正式草稿中的 `ChartDraft` 必须完整保留谱面事件、稳定对象标识、显式角色、语义关系和 `SHOW-D.showNCount` 等谱面字段。BPM 和 Offset 保存各自权威的有限 IEEE-754 binary64 值，序列化后加载必须精确往返，不能使用界面的三位小数投影。正式草稿不包含音频引用或访问凭据、规范 PCM、波形、分析缓存、来源音频元数据、品质上限、当前时间光标、播放状态、Zoom、Scale、波形可见性、撤销/重做历史、`WorkspaceSource` 或其他会话与视图状态。

正式草稿使用稳定 `draftId`，显示名称不是存储主键。`draftId`、显示名称、格式版本和完整性校验属于容器或索引元数据，不是额外工作区负载。同名检查使用规范化后的显示名称；名称规范化、最大长度和平台文件名映射的具体实现必须集中定义，不能由各平台分别决定。

正式草稿保存通过“完整临时文件写入、刷新、原子替换”的方式发布。任何失败都必须保留上一份有效正式草稿和当前内存工作区。草稿列表读取正式草稿索引，默认按最后修改时间降序排列；恢复快照不进入该索引。

App 私有存储中最多存在一个恢复快照。每次已提交的谱面修改都会调度 `500` 毫秒防抖更新；进入后台和正常退出回调会请求立即刷新。恢复快照只保存版本化的 `ChartDraft` 谱面数据，明确不使用正式草稿的 BPM/Offset 负载。写入通过临时文件发布新的不可变记录，再删除旧代，因此崩溃最多丢失尚未刷新的最新修改，不能同时破坏上一份有效快照。

恢复快照不保存 `WorkspaceSource`、正式草稿 ID、音频引用、平台授权凭据、BPM、Offset、规范 PCM、波形图块、分析缓存或视图状态。异常恢复只还原谱面数据，不恢复任何音频或界面会话状态。

会话状态使用独立的启动/正常结束标记。上次会话未正常结束且存在有效恢复快照时，App 才提示恢复。恢复操作把谱面数据加载为 `source = Untitled`、`dirty = true` 的当前工作区，避免自动覆盖任何正式草稿；放弃恢复删除快照。正式保存成功且工作区已干净、或用户在正常退出流程中明确放弃修改时，删除恢复快照。损坏或版本不支持的恢复快照不得替换当前工作区。

恢复快照写入和文件系统原子发布属于 App/平台存储层；`ChartDraft` 的版本化编码、解码和完整性校验由共享核心实现。任何平台都不得自行定义另一套恢复数据模型。

当前共享核心编码入口为 `encode_formal_draft` 与 `encode_recovery_snapshot`，对应严格解码入口为 `decode_formal_draft` 与 `decode_recovery_snapshot`。两者使用内部 `OTGYPRJ\0` version 1 二进制信封：固定小端版本、负载类型、零 flags、精确负载长度和负载 CRC32，之后按六个原生容器的当前顺序编码全部事件、稳定标识、显式角色、关系与 `showNCount`。解码器先校验信封类型、长度和完整性，再检查枚举/计数/剩余字节；成功值拥有全部内存且不引用输入。该内部信封不决定未来用户可见文件扩展名。

正式负载在 `ChartDraft` 前逐位保存 BPM 和 Offset 的 IEEE-754 binary64 表示，并拒绝非有限或超出 App 权威范围的值；恢复负载结构中没有这两个字段。编码/解码只验证容器安全和字段表示，不调用成品校验器，因此零关系、缺失块、角色不匹配或其他允许的不完整草稿仍可逐值往返。名称、`draftId`、修改时间等索引元数据由上层正式草稿容器管理；音频、授权、PCM、波形、缓存、来源、播放/视图状态、历史及 `ChartEditor` 的会话内下一标识均不进入任一负载。

当前 `LocalProjectStore` 在 App 私有支持目录使用 version 1 JSON 外层和 `.otgydata` 不可变记录保存索引元数据，核心二进制负载以 Base64 携带。每次保存先完整写入并刷新临时文件，再在同目录发布新代；读取时忽略截断、损坏或未知版本记录，并保留仍有效的上一代。该外层只是平台存储适配，不解释或重写核心负载。

## 平台边界

平台代码仅负责：

- 获得授权的文件访问和文件选择器集成
- 音频设备输出
- 窗口、键盘、指针和触控输入
- 生命周期与应用存储路径
- 向 C ABI 传入和取出数据

平台播放适配层消费共享核心生成的帧，不得重新实现速度、混音、裁剪、节拍时序或重采样规则。

Flutter 与 Runner 使用三个轻量 MethodChannel：`io.onetwogoyeah/playback` 只传递采样率、交错 PCM16LE 和已播放帧数；`io.onetwogoyeah/files` 只在同目录把完整临时文件以原子、不替换硬链接发布到目标；`io.onetwogoyeah/lifecycle` 只用于 Windows/macOS 窗口关闭前请求 Flutter 完成未保存确认。Android 使用 `AudioTrack`/`Os.link`，iOS 和 macOS 使用 `AVAudioEngine`/POSIX `link`，Windows 使用 `waveOut`/`CreateHardLinkW`。这些适配层不得解释音频时序、文件名或项目语义。

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

替换音频必须是事务性的，并且只能在非播放状态开始。播放中到达应用操作协调层的替换请求返回稳定的 `PlaybackActive` 拒绝结果，不打开选择器、不启动解码，也不修改状态。所选文件先解码到临时、未发布状态，再从临时规范 PCM 生成完整波形；两个步骤都不得修改当前音频、文件名、缓存、波形、BPM、Offset 或其他编辑器状态。取消平台文件选择、解码失败或波形生成失败时，丢弃全部临时工作并保持当前状态不变。

只有解码和波形全部成功后，才取消并等待上一音频版本的分析任务结束或以安全方式解除关联，然后原子发布新音频、规范 PCM 和波形。提交事务必须清除上一音频版本的分析任务与候选、来源码率、品质上限、元数据以及其他派生缓存，再仅从新来源建立新音频对应的信息；同时把权威 BPM 重置为精确 `100`，把权威 Offset、当前时间光标和播放位置重置为精确的零，并保持暂停。默认 BPM 和 Offset 立即重建时序网格。每个异步结果都携带音频版本；即使协作取消较晚才完成，也必须丢弃过期结果。提交后绝不能复用上一音频的缓存。

音频替换不修改 `ChartDraft`、谱面撤销/重做历史、Zoom、Scale、波形可见性或其他纯视图偏好。BPM/Offset 重置不进入谱面撤销历史；如果当前来源是正式草稿且重置值与已保存值不同，则根据正式草稿负载比较规则把工作区标记为 `dirty`。

## Ogg 音频导出契约

叠加 Offset 的音频编码只在 `av_core` 中实现一次。输入为当前规范平面双声道 PCM16、原始采样率、权威 Offset 和所选品质。MP3 与 Ogg 来源只要得到相同规范 PCM，在相同品质下就必须经过同一转换和编码路径。原始来源元数据只影响 UI 和公开导出流程允许选择的品质；一旦品质有效，就不能影响编码器输出。

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

导出任务在确认时捕获不可变 PCM 所有权或稳定 PCM 快照、采样率、Offset、品质、文件名和元数据。任务开始后绝不能读取可变的实时项目值。核心编码器接受协作取消信号，并报告阶段与进度。平台保存过程先写入临时目标，只在成功时原子发布最终文件；取消或失败都要删除临时输出。如果负 Offset 会删除全部输入帧，必须在编码前拒绝。

可移植文件基础名规范化由共享核心纯函数实现。默认输入为来源音频去除扩展名后的名称，缺失时为 `audio`。合法 Unicode 保留；Unicode 控制字符以及 Windows、macOS 和常用文档提供器不接受的 `<>:"/\\|?*` 替换为 `_`；去除首尾空白和末尾句点；大小写不敏感匹配 `CON`、`PRN`、`AUX`、`NUL`、`COM1..COM9` 或 `LPT1..LPT9` 时在前方添加 `_`；结果为空时回退为 `audio`。平台层传入设备本地时间，生成 `<base>_<yyyyMMdd_HHmmss>.ogg`。目标已存在时不得覆盖，依次尝试在扩展名前追加 `_2`、`_3` 等最小可用序号。

桌面平台通过原生保存界面取得目标授权，移动平台通过系统文档保存界面取得目标授权，二者都使用核心生成的规范建议名称。用户取消目标选择时不得启动编码。导出期间应用进入后台或平台撤销继续执行资格时，应用协调层发送取消信号并等待任务停止，平台层删除临时输出且不发布最终文件；恢复前台后不自动重试。

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

C ABI 的 `otgy_audio_export_cancel_flag` 是线程安全原子取消句柄，可创建、请求、查询并销毁。Dart 在主 isolate 持有拥有句柄，把地址借给编码 isolate；原生编码回调只查询该句柄。编码完成后文件保存仍保持 `ExportingAudio` 全局门控，并在每个写入块之间查询同一取消状态。平台文件层把完整临时文件以不替换硬链接原子发布；若目标在发布瞬间已存在，则不触碰旧文件，并继续调用核心命名规则尝试最小 `_2/_3` 后缀。

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
`AllocationFailure`。`WaveformTile` 深复制两个平面，移动则转移所有权。当前入口
有意按已实现的形状/亮度/采样组合分别命名，且不接受通用配置或抗锯齿参数；
过滤波形和缓存仍属于后续独立切片。

## BPM 与 Offset 兼容契约

- 移植 ArrowVortex 的起音、BPM 和 Offset 算法，并保持操作顺序、常量、线程语义和候选顺序不变。
- 检测器输出使用 `double`，并保留未舍入结果。
- BPM 候选排序和前三名行为保持兼容。
- 主界面分析操作使用 ArrowVortex 默认的“未选择区间”范围：从音频时间零开始，最多分析 600 秒，并限制到实际音频长度。
- BPM 与 Offset 联合分析按已提交音频版本延迟执行且单次在途。专用分析操作第一次请求时运行一次共享核心检测器；并发请求复用该任务，之后的请求复用已完成结果。
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

BPM 文本输入只接受十进制数字和一个独立于区域设置的小数点。App BPM 范围为 `0.0..100000.0`：保留 ArrowVortex 的最大值，不允许其负值范围，并允许零表示未设置/无网格状态。无效或越界输入不发送领域命令。步进操作在边界处停止。短按立即提交；长按重复值作为 UI 临时状态累积，在松开时只发送一次最终 BPM 命令，只触发一次网格重算。BPM 和 Offset 修改（包括应用检测候选）都不创建撤销历史。

Offset 微调同样通过共享核心领域命令执行。合法的三位小数直接输入会用独立于区域设置的解析值替换权威 Offset。按拍移动使用当前权威 BPM 和 ArrowVortex 方向约定：

```text
secondsPerBeat = 60 / bpm
forwardOne     = offset + secondsPerBeat
forwardHalf    = offset + secondsPerBeat / 2
backwardHalf   = offset - secondsPerBeat / 2
backwardOne    = offset - secondsPerBeat
```

每次合法 Offset 修改都会立即提交，并使时序转换和时序网格派生数据失效，同时保留 PCM、波形图块和音频分析结果。显示和导出投影都从最终权威 binary64 值派生。

App 权威 Offset 范围为 `-30.0..30.0` 秒。超出范围的直接输入会被拒绝；按拍移动限制在最近边界。BPM 为零时不能使用按拍 Offset 命令，时序网格也不生成小节、拍或细分引导线，但 `ChartDraft` 的抽象 1/16 行坐标、上下文点击命令和撤销历史仍然可用。BPM 为零时，画布仅为行布局和范围比较使用不可持久化的 `100.0` 投影 BPM；它不生成可见网格、不替换权威零值，也不参与保存或导出。

当前高精度时序换算入口接收按值传入的 `TimingParameters { bpm,
offset_seconds }` 快照，不持有或修改工作区权威值。App Offset 表示音乐开始相对第一
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

当前时序校准网格入口为 `generate_timing_grid(timing, beginSeconds,
endSeconds)`。输入秒范围是半开区间 `[beginSeconds, endSeconds)`；相邻范围可以直接
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

当前共享导航入口为 `calculate_timeline_navigation(range, currentRow,
visibleBeats)`。它直接消费 `TimelineRange` 的连续 1/16 行边界，一次返回到开头、
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

`render_playback_pcm(pcm, timelineStartSeconds)` 是唯一设备缓冲生成入口。它把有限
时间线起点按当前采样率投影到最近整数帧，半帧远离零：负位置生成对应数量的前置
双声道静音，非负位置跳过已播放来源帧；输出固定为交错 stereo PCM16LE，并保留
采样率和帧数。它拒绝晚于音频末尾、非有限、`int64` 边界和尺寸溢出，不修改规范
平面 PCM。C ABI 只提供拥有句柄及采样率、帧数、字节视图。

Flutter 把该核心缓冲交给 Android `AudioTrack`、Apple `AVAudioEngine` 或 Windows
`waveOut`，并以约 33 毫秒轮询平台已播放帧数。暂停、自然结束、音频焦点丢失、
输出中断或 App 进入后台都先保留平台最后已播放帧，再调用共享帧推进/暂停入口；
恢复焦点或返回前台不自动播放。平台不得用墙钟时间或缓冲总长度猜测中断位置。

正式草稿序列化必须精确往返其中每个有限 binary64 值。可以使用独立于区域设置的 17 位有效十进制数字，也可以显式保存十六进制/位表示。可以附带便于阅读的数值，但它们不是权威值。只含 `ChartDraft` 的异常恢复快照不保存 BPM 或 Offset。

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

当前 ABI 版本为 `1`，公共纯 C 头为
`native/av_c_api/include/otgy/av_c_api.h`，动态库目标为 `otgy_c_api`，共享库主版本
固定为 `1`。状态码只描述 ABI 调用边界；核心拒绝通过拥有型 `otgy_error` 返回稳定的
领域、领域内错误码和静态诊断文本。任何 C++ 异常都在最外层捕获并转换，不能越过
`extern "C"`。所有长度在 ABI 中使用 `uint64_t`，转换到本机 `size_t` 前检查范围；
布尔值使用只接受 `0/1` 的 `uint8_t`，枚举使用固定宽度整数和公开常量。

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
- 除非所有支持架构都通过相同黄金契约，否则不得静默引入 SIMD 路径。
- 如果原始浮点结果无法在不同架构间按位一致，允许容差必须被记录、足够小且有理由，并且不能改变任何候选顺序或兼容投影值。

## UI 适配契约

主编辑画布在所有平台上共享同一概念：

- 音频时间和谱面时间沿垂直方向移动
- 判定线保持固定
- 波形声道、谱面网格、原生谱面编辑对象和光标保持参考几何
- 手机竖屏布局保留垂直滚动波形行为

桌面端和移动端可以使用不同的工具栏、面板、快捷键和手势，但这些控件必须以相同参数发送相同领域命令。移动端适配只改变功能入口，不改变结果。

谱面渲染层固定投影为从左到右的 NPC、玩家、Showtime 三列。NPC 和玩家列由原生对象的显式角色决定，Showtime 相关对象投影到第三列；三列共享同一垂直时间坐标。列编号只是视图投影，不作为独立领域字段重复保存，避免列位置与对象语义产生不一致。所有平台必须使用相同投影规则。

右侧悬浮导航与播放控件是三列上方的独立 UI 覆盖层，不参与谱面列投影，也不产生领域对象。Flutter 必须先命中控件自身的交互区域；未命中控件时，输入才继续交给下方谱面对象或空白时间线手势。控件操作不得修改列归属或谱面内容。

谱面直接点击使用由三列边界和最近 1/16 行共同确定的网格单元，而不依赖标记最终视觉轮廓。Flutter 把触点转换为规范列和连续 `rawRow`，以 `floor(rawRow + 0.5)` 得到非负目标行；中点并列时稳定选择时间更晚的行。当前列目标行已有事件时发送该稳定事件 ID，三列全空时发送列与目标行，其他列已占用时发送后由核心返回稳定的位置占用结果。Flutter 不得把对其他列空白区域的点击重定向成对已有事件的删除命令。

移动端 `rowSpacing < 40` 逻辑像素时，Flutter 禁止发送谱面内容编辑命令并提供“请放大后编辑”的非阻塞反馈，但仍允许导航和缩放；达到或超过阈值立即恢复编辑。该阈值是输入适配规则，不改变 `ChartDraft`、缩放值或核心命令语义。桌面指针不应用该阈值。标记的具体色值、尺寸、辅助样式、`SHOW-D` 附加数值信息和范围带设计参数属于 Flutter 展示投影，不得反向影响核心事件类型、点击单元或展示范围计算。

Flutter 展示层使用与领域角色解耦的固定语义样式类：`NPC-C/PLY-C/SHOW-C` 映射到同一黄色菱形；`NPC-N/PLY-N` 映射到带内环的蓝色圆形；`NPC-S/PLY-S` 映射到靠下带短横线的红色圆形；`SHOW-D` 映射到上下外缘带短刻度的紫色圆形。列投影继续承担角色区分，样式类不得覆盖或反推领域角色。标记内部不渲染可见的 C/N/S/D 字母，但 `SHOW-D` 在尺寸允许时显示其权威 `showNCount` 十进制数字；Flutter 语义树始终暴露完整类型、角色和数值。这些非文字辅助线索保持共同圆形外形。具体色值、边框和范围带参数属于 Flutter 设计令牌或展示参数，不进入 `av_core` 或项目序列化。

`NPC-N/PLY-N` 的可见数字由 `av_core` 提供确定性的派生查询，不由 Flutter 扫描绘制列表计算。对当前 N，核心只在同一角色中查找此前最近的 S，并返回该 S 之后、当前事件之前的同角色 N 数量加一；没有此前 S 时从谱面开头计数。C、另一角色事件和 Showtime 事件既不计数也不中断，C 因此允许拆分演练组继续编号。顺序号不持久化、不进入 SLK，也不形成独立历史；任一编辑完成后从新 `ChartDraft` 快照查询即可自然反映 N 插入、N 删除、N 转 S、S 删除和整块删除。删除 N 只使下一同角色 S 之前的后续编号按实际删除数量前移，不改变下一 S 之后的编号。该派生刷新不得修改其他事件、关系或 `SHOW-D.showNCount`。

标记尺寸从 Flutter 当前 `rowSpacing` 直接投影：iOS/Android 的圆形与菱形边界框分别为 `min(28, rowSpacing * 0.70)` 和 `min(32, rowSpacing * 0.80)`；Windows/macOS 分别为 `min(22, rowSpacing * 0.70)` 和 `min(26, rowSpacing * 0.80)`，单位均为逻辑像素。所有 N/S/D 共用圆形直径。尺寸不受 `Scale` 影响，不能产生非等比形变；边框在正常编辑尺寸使用约 `1.5..2.0` 逻辑像素并对齐物理像素，极小概览尺寸按比例缩减。尺寸公式只属于确定性视图投影，不进入项目数据或点击命令参数。

N 派生顺序号和每个 `SHOW-D` 的 `showNCount` 使用相同数字排版规则：圆形直径至少 `18` 逻辑像素时显示无前后缀、无分组符的完整十进制数字；更小时只隐藏可见数字，不改变语义树、派生值或领域值。正常一至两位数字字号约为直径的 `45%` 并限制在 `9..13` 逻辑像素，更长数字等比缩小以完整容纳。每个 `SHOW-D` 的 Flutter 投影还包含实际 `dRow` 上的紫色圆形锚点，以及直接使用核心 `[rangeStart, rangeEnd)` 结果绘制的低透明度紫色范围带。范围带横跨三列，位于波形之上、时序网格和所有锚点标记之下，不参与网格单元命中测试。Flutter 不得从圆形锚点像素位置重新推算范围；因此 `SHOW-D1` 位于小节线 +1 时，锚点与从小节线开始的范围带可以有一个 1/16 行偏移。

核心查询同时指出当前 `SHOW-D` 是否为连续序列最后一个事件。仅该事件的 `[rangeEnd - 8, rangeEnd)` 投影使用更浅背景，并在 `rangeEnd - 8` 增加虚线边界；这项视觉状态与 NPC-C 冲突例外使用同一核心判断，不能由 Flutter 另行扫描或猜测。范围带和开放区样式只提供视觉提示，不创建事件、不占用额外时间行，也不扩大紫色圆形锚点的点击语义。

ArrowVortex 的编辑画布只作为时间线、波形、固定光标、Zoom、Scale、播放跟随和导航交互的参考。核心不得引入 StepMania 轨道或 Step、Mine、Hold、Roll、Fake、Lift 作为本 App 的权威编辑模型。权威对象始终来自 `ChartDraft`，并保留显式角色、稳定对象标识和舞步、演练组、演练序列、Showtime 等谱面语义。

首版谱面位置命令以 1/16 音符为基础单位。谱面内容只通过已经确认的三列上下文点击命令创建、转换或删除；首版核心、C ABI 和 Flutter 均不提供选择、框选、多选、标记移动、剪切、复制、粘贴或通用删除命令。撤销和重做作用于这些上下文命令，所有 `ChartDraft` 修改进入谱面历史，BPM 和 Offset 修改继续排除在该历史之外。未来若扩展选择、剪切、复制和粘贴，必须先补充独立规格和测试，不能预留含义未定的占位 API。

Zoom 只改变纵向时间密度，Scale 只改变波形和谱面对象区域的横向显示比例，两者都不得修改 PCM、BPM、Offset 或谱面位置。Scale 不参与本 App 圆形和菱形标记尺寸计算。Windows/macOS 可以在 `1..10` 内调整 Scale；iOS/Android 固定为 `4.0` 并只做屏幕宽度响应式排布。该平台输入差异只影响视图，不能改变序列化后的 `ChartDraft` 或任何导出结果。

时序校准网格与波形生成相互独立。共享核心时序转换根据权威 BPM、Offset 和拍号数据确定行/时间位置；Flutter 把这些位置绘制为小节、拍和音符细分引导线。小节线视觉最强，拍线较浅，音符细分线最浅。时序更新只重算网格位置，不修改 PCM 或波形图块。BPM 为零时不生成网格线，但画布使用上述非持久化投影继续提供确定性的 1/16 行命中。

播放位置由固定的当前时间光标/判定线表示。开始播放时使用该光标时间；暂停时保留精确播放位置。播放期间，视图根据核心位置自动跟随，让波形、时序网格和编辑层在固定判定线下移动。到达音频末尾时暂停并保留末尾位置；从末尾再次开始播放时，先跳到开头。Flutter 和平台输出适配层负责展示状态，共享核心生成的播放帧是时序权威来源。

共享核心返回时间线范围，不由 Flutter 扫描绘制对象猜测。基础编辑范围固定为四小节，即 16 拍、64 个 1/16 行；默认 BPM `100`、Offset `0` 时对应 9.6 秒。时间线末尾取基础范围末尾、已加载音频结尾、最后一个谱面事件位置以及全部 `SHOW-D.rangeEnd` 中的最大值。没有音频和谱面事件时只使用四小节基础范围；“跳到末尾”和向后翻页的边界都消费同一结果。

应用操作协调层把 `Playing` 作为显式的全局交互门控状态。该状态下只接受暂停播放与切换波形可见性；音频、谱面、BPM/Offset、导入导出、草稿持久化、撤销/重做、导航、时间线、Zoom 和 Scale 的其他请求统一返回 `PlaybackActive`，且不调用后续领域命令。Flutter 的按钮、手势、键盘、鼠标和无障碍入口都必须查询同一门控状态并展示禁用样式，不能各自维护不一致的白名单。切换波形可见性是纯视图操作，不进入核心项目数据；暂停仍由共享播放状态执行并保留精确位置。

当前纯查询入口 `coordinate_application_operation` 接收互斥的 `Idle`、`Playing`
或 `ExportingAudio` 模式，以及默认保守的 `General` 操作分类。`Playing` 只放行
`PausePlayback` 和 `ToggleWaveformVisibility`，其余返回 `PlaybackActive`；
`ExportingAudio` 只放行 `CancelAudioExport`，其余返回 `AudioExportActive`。
该入口不执行命令也不修改项目、播放、导出或视图状态。

移动端单指拖动空白画布时，更新时间线位置且不使用惯性。双指缩放围绕固定当前时间光标连续预览，并在松开时提交到最近的 `0.25` 参考缩放级别。播放期间由全局门控拒绝时间线拖动和双指缩放命令。谱面编辑对象的命中测试优先于时间线手势。

翻页导航通过共享时序转换，从当前精确视口位置计算目标。移动距离为可见拍数减一拍，从而精确保留一拍重叠；结果只在越界时限制到开头或末尾，不能再执行拍线、小节线或 1/16 行吸附。每次离散激活只执行一次翻页，长按不重复触发。位于边界或继续翻页不能改变位置时，对应的“到开头/上翻页”或“到末尾/下翻页”禁用。悬浮控件条固定在右侧、不允许重定位、不自动隐藏，并由 Flutter 避让平台安全区域。播放期间四个导航操作全部由全局门控拒绝，只保留波形开关和暂停可用。

平台生命周期或音频焦点中断事件必须转换为暂停请求；核心/播放协调状态保存中断时的精确位置。回到前台或重新取得音频焦点不能自动恢复播放。音频自然播放到末尾后状态立即转换为暂停，随即解除播放态交互门控。

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
