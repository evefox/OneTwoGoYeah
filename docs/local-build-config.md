# 本机构建配置

本仓库不保存账户、签名身份、密钥或本机绝对路径。每台开发机在首次构建前补齐下面的本地文件；这些文件都已被 `.gitignore` 忽略，不得提交。

## 1. Git 作者身份

只对本仓库设置，不影响其他项目：

```sh
git config --local user.name evefox
git config --local user.email 22185605+evefox@users.noreply.github.com
```

## 2. iOS 真机签名

1. 复制模板：

   ```sh
   cp app/ios/Flutter/Local.xcconfig.example app/ios/Flutter/Local.xcconfig
   ```

2. 在 `app/ios/Flutter/Local.xcconfig` 填入自己的 Team ID：

   ```
   OTGY_DEVELOPMENT_TEAM = XXXXXXXXXX
   ```

3. `Debug.xcconfig` 和 `Release.xcconfig` 通过 `#include? "Local.xcconfig"` 读取该值，`project.pbxproj` 使用 `$(OTGY_DEVELOPMENT_TEAM)`；仓库里不保存实际 Team ID。

4. 未配置 Team ID 时，无签名构建仍然可用：

   ```sh
   flutter build ios --release --no-codesign
   ```

5. 如果 Xcode 图形界面无法从变量识别自动签名，可在命令行显式覆盖：

   ```sh
   xcodebuild -workspace app/ios/Runner.xcworkspace -scheme Runner \
     -configuration Release -destination 'id=<设备 UDID>' \
     DEVELOPMENT_TEAM=XXXXXXXXXX -allowProvisioningUpdates build
   ```

Team ID 可在 Apple Developer 账号的 Membership 页面查看，或用 `security find-identity -v -p codesigning` 查看本机证书。

## 3. Android

- `app/android/local.properties` 由 Flutter 自动生成，保存本机 SDK 路径，已忽略。
- 当前 Release 使用 debug 签名，仅用于本机测试。
- 对外分发时提供外部 release keystore，并通过不提交的 `app/android/key.properties` 引用：

  ```properties
  storeFile=/absolute/path/to/your-release.jks
  storePassword=********
  keyAlias=********
  keyPassword=********
  ```

  `*.jks`、`*.keystore` 和 `key.properties` 都不得提交。

## 4. macOS

- 本机开发使用 Ad Hoc 签名。
- 对外分发需要 Developer ID 证书和 Apple notarization 凭据；凭据保存在钥匙串或 CI secret 中，不进入仓库。

## 5. Windows 验收机

- SSH 别名 `otgy-win` 和私钥路径定义在本机 `~/.ssh/config`，不在仓库中。
- 远端是指向验收机的裸仓库；Git 通过本机 `core.sshcommand` 脚本访问它。
- 新机器接入时自行配置别名和密钥，不要把私钥或密码写进仓库。

## 6. 提交前检查

```sh
# 确认没有把实际 Team ID 写回工程文件
git grep -nE 'DEVELOPMENT_TEAM = [A-Z0-9]{10};' || echo clean

# 确认本机配置处于忽略状态
git status --ignored --porcelain | grep Local.xcconfig
```
