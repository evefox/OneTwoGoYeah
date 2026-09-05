# 第三方声明

OneTwoGoYeah 将以下第三方代码随源码仓库固定并用于构建。版本、来源、源码
摘要和构建边界以 [`native/third_party/README.md`](native/third_party/README.md)
为准。发布源码或构建产物时，应同时保留本文件以及表中列出的版权和许可证文件。

## 随仓库编译的音频组件

| 组件 | 固定版本/来源 | 许可证 | 版权与许可证文本 |
| --- | --- | --- | --- |
| libmad | 0.15.1 beta，来自已检查的 ArrowVortex 参考快照 | GPL-2.0-or-later | [`native/third_party/libmad/COPYING.txt`](native/third_party/libmad/COPYING.txt)、[`COPYRIGHT.txt`](native/third_party/libmad/COPYRIGHT.txt)、[`CREDITS.txt`](native/third_party/libmad/CREDITS.txt) |
| libogg | ArrowVortex 随附快照，`ogg.h` revision 18044，源文件最高 revision 19149 | 3-clause BSD | [`native/third_party/libogg/COPYING`](native/third_party/libogg/COPYING)、[`AUTHORS`](native/third_party/libogg/AUTHORS) |
| libVorbis | 1.3.5，固定模式和码本表 | 3-clause BSD | [`native/third_party/libvorbis/COPYING`](native/third_party/libvorbis/COPYING)、[`AUTHORS`](native/third_party/libvorbis/AUTHORS) |

这些组件只从仓库内静态源码构建。运行时不使用系统编解码器、平台音频解码器、
包管理器下载或子进程。libmad 的 GPL 条款和 libogg/libVorbis 的 BSD 条款仍分别
适用于对应代码；本项目整体许可证见 [`LICENSE`](LICENSE)。

## Flutter 和 Dart 依赖

App 使用仓库锁定的 Flutter `3.47.2` / Dart `3.13.2` 工具链，以及
`app/pubspec.lock` 中固定版本的 Flutter 官方插件和 Dart 包。Flutter 官方插件
（包括 `file_selector`、`path_provider` 及其平台实现）采用 BSD 3-Clause 许可证，
对应的版权与许可证文本保存在 [`LICENSES/FLUTTER-BSD-3-CLAUSE.txt`](LICENSES/FLUTTER-BSD-3-CLAUSE.txt)。
各依赖的版本、来源 URL 和校验摘要以 `app/pubspec.lock` 为准；构建时不得绕过
锁定文件改用未审核的系统或网络版本。

Flutter SDK 自身及 Dart SDK 的分发声明由其 SDK 随附的许可证文件提供。发布包
应保留 Flutter SDK 要求的相关许可证页面或文本；SDK 不属于本仓库运行时网络依赖。

## 源码和二进制分发要求

- 源码包必须包含 `LICENSE`、`LICENSES/GPL-3.0.txt`、本文件，以及各组件源码旁的
  版权和许可证文件。
- 二进制归档应在归档目录中同时放置本文件和 `LICENSES/` 目录，或在应用的离线
  关于/许可证位置提供等价的完整文本。
- `example/` 下的用户音频、用户提供的音效参考副本和导出文件不属于可分发夹具；
  它们的再分发许可未确认，不得因为本地构建而纳入发布包。
- 发布前必须确认所有新增资源的版权和许可证状态；不能以第三方网页链接代替
  已随分发物提供的离线许可证文本。

## 离线构建说明

依赖源码、测试夹具和工具链版本均应在构建开始前准备好。产品运行时不请求网络、
不使用账号、不发送遥测、不检查更新，也不下载音频或许可证内容。
