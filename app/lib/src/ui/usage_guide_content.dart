import 'package:flutter/material.dart';

import 'editor_keymap.dart';

/// One image in the help carousel. The image already contains its title,
/// direct labels and straight leader lines, so the Flutter layer only shows
/// the picture.
class UsageGuideSlide {
  const UsageGuideSlide({
    required this.desktopAsset,
    required this.mobileAsset,
    required this.semanticLabel,
    required this.tip,
  });

  final String desktopAsset;
  final String mobileAsset;
  final String semanticLabel;
  final String tip;
}

/// One text detail page reachable from below the carousel.
class UsageGuideTopic {
  const UsageGuideTopic({
    required this.title,
    required this.icon,
    required this.blocks,
  });

  final String title;
  final IconData icon;
  final List<UsageGuideBlock> blocks;
}

sealed class UsageGuideBlock {
  const UsageGuideBlock();
}

final class GuideParagraph extends UsageGuideBlock {
  const GuideParagraph(this.text);

  final String text;
}

final class GuideBullets extends UsageGuideBlock {
  const GuideBullets(this.items);

  final List<String> items;
}

final class GuideSteps extends UsageGuideBlock {
  const GuideSteps(this.steps);

  final List<String> steps;
}

final class GuideShortcuts extends UsageGuideBlock {
  const GuideShortcuts();
}

/// Gesture and fixed-control rows for a desktop player. Rebindable keyboard
/// actions are rendered from the live keymap above these rows, so this list
/// only holds combinations the editor never remaps.
List<(String, String)> desktopFixedShortcutRows(TargetPlatform platform) {
  final primary = primaryModifierLabel(platform);
  return [
    ('$primary + C', '复制选中的舞步（选择模式）'),
    ('$primary + X', '剪切选中的舞步（选择模式）'),
    ('Esc', '退出选择或粘贴'),
    ('滚轮', '移动时间线'),
    ('$primary + 滚轮', '调整 Zoom'),
    ('Ctrl + Alt + 滚轮', '调整 Scale'),
    ('鼠标中键', '跳到最近的吸附行'),
  ];
}

/// Touch rows for a mobile player. Desktop-only keyboard and mouse rows are
/// deliberately absent; the desktop list omits these in the same way.
const mobileShortcutGestureRows = <(String, String)>[
  ('单指拖动', '移动时间线'),
  ('双指捏合', '调整 Zoom'),
  ('长按', '进入选择模式'),
];

/// Six slides: a whole-interface overview, four area slides, and a gesture
/// slide for the blank editing grid. Desktop and mobile use separate
/// landscape/portrait captures.
const usageGuideSlides = <UsageGuideSlide>[
  UsageGuideSlide(
    desktopAsset: 'assets/guide/desktop/guide_overview.webp',
    mobileAsset: 'assets/guide/mobile/guide_overview.webp',
    semanticLabel: '主界面总览：音频操作区、SLK 操作区、舞步编辑区、播放控制区',
    tip: '左右滑动查看各区域',
  ),
  UsageGuideSlide(
    desktopAsset: 'assets/guide/desktop/guide_audio.webp',
    mobileAsset: 'assets/guide/mobile/guide_audio.webp',
    semanticLabel: '音频操作区：导入音频、分析 BPM 与 Offset、节奏测试、导出音频',
    tip: '先导入音频，再做分析与节奏测试',
  ),
  UsageGuideSlide(
    desktopAsset: 'assets/guide/desktop/guide_slk.webp',
    mobileAsset: 'assets/guide/mobile/guide_slk.webp',
    semanticLabel: 'SLK 操作区：谱面菜单、保存与另存为、清空、导出成品 SLK',
    tip: '保存草稿不会覆盖成品；导出前会校验',
  ),
  UsageGuideSlide(
    desktopAsset: 'assets/guide/desktop/guide_edit.webp',
    mobileAsset: 'assets/guide/mobile/guide_edit.webp',
    semanticLabel: '舞步编辑区：三列标记、点击放置、撤销与重做',
    tip: '点击对应位置放置标记，部分标记自动生成',
  ),
  UsageGuideSlide(
    desktopAsset: 'assets/guide/desktop/guide_gestures.webp',
    mobileAsset: 'assets/guide/mobile/guide_gestures.webp',
    semanticLabel: '舞步编辑区手势：双指放大或缩小调整网格间距，长按选择后复制或剪切标记',
    tip: '双指调整网格间距；长按选择后可复制或剪切',
  ),
  UsageGuideSlide(
    desktopAsset: 'assets/guide/desktop/guide_playback.webp',
    mobileAsset: 'assets/guide/mobile/guide_playback.webp',
    semanticLabel: '播放控制区：使用指南、波形设置、音效切换、导航与播放',
    tip: '播放期间大部分编辑操作会被禁用',
  ),
];

/// The three remaining detail entries, shown below the carousel.
const usageGuideTopics = <UsageGuideTopic>[
  UsageGuideTopic(
    title: '如何复制与粘贴',
    icon: Icons.content_copy_outlined,
    blocks: [
      GuideSteps([
        '在画布内长按，进入选择模式。',
        '点击任意标记所在行，切换该完整舞步；再次点击取消。',
        '点「复制」或「剪切」。',
        '桌面端移动鼠标预览落点，点击目标行放置，或按粘贴快捷键（默认 Ctrl+V）在判定线放置；移动端点击「粘贴到当前判定线」。',
        '按 Esc 或点「退出」结束。',
      ]),
      GuideParagraph('选择或待粘贴期间只保留导航、视图、撤销/重做和会话自身操作，其他入口会禁用。'),
    ],
  ),
  UsageGuideTopic(
    title: '快捷键与手势',
    icon: Icons.keyboard_outlined,
    blocks: [GuideShortcuts()],
  ),
  UsageGuideTopic(
    title: '常见问题',
    icon: Icons.help_outline,
    blocks: [
      GuideBullets([
        '点击画布没反应：同一时间行已有标记，或当前位置不合法。',
        '保存和清空按钮是灰的：谱面还没有任何标记。',
        '导出成品 SLK 被拦下：成品校验未通过，按弹窗里的问题补齐。',
        '播放时没有舞步音效：检查右侧「音效切换」里的语言、4K/8K 和开关。',
        '没有保存就退出：应用会保留恢复快照，下次启动可以恢复。',
      ]),
    ],
  ),
];
