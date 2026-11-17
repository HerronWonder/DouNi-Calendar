# 豆泥日历 / DouNi Calendar

透明、低占用的桌面记事月历。Windows 版使用原生 C++ / Win32，macOS 版使用 Swift / AppKit。

## 下载与使用

请在本仓库的 **Releases** 下载对应平台的使用包：

- Windows 10 / 11 x64：`DouNiCalendar-版本-Windows-x64.zip`，解压后运行 `DouNiCalendar.exe`。
- macOS 13 及以上：`DouNiCalendar-版本-macOS-universal.zip`，支持 Apple 芯片和 Intel Mac。解压后先用 Finder 将整个文件夹移到“文稿”等可写位置，再运行 `DouNi Calendar.app`。

源码仓库不存放安装包、编译输出、个人记事或设置；每个平台的使用包只在 Releases 提供一份。GitHub 自动生成的 Source code 下载项是源码快照，无需与使用包一起下载。

默认透明并穿透鼠标。左键托盘或菜单栏图标进入编辑，再点一次保存并完成；右键打开菜单。支持编辑模式、移动与缩放、手绘风格、字体大小、显示节假日、登录启动和退出。

日期固定七列六行，补全上月和下月，非本月日期淡显。记事在日期格内透明编辑，Enter 保存，Shift+Enter 换行；点击右上角“完成”或日历外部，保存并退出编辑。年月支持原位输入，左右箭头切月，闹钟图标回到今天，旁边显示今天的星期。非法输入还原，不显示对话框。“字体大小”以数字输入，Enter 确认，仅调整记事文字，支持 8—32。

所有数据保存在程序旁的 `data` 文件夹。Mac 的 `data` 位于 `.app` 同级，不在应用包内部。记事为 UTF-8 文本，保存采用原子替换，并保留最近一版备份。迁移前退出程序，复制应用和整个 `data`；更新只替换应用，保留原有 `data`。

```
src/windows/     Windows C++17 源码与资源
src/macos/       macOS Swift 源码与应用资源
tests/           日期、存储和 Windows 内部窗口测试
tools/           Windows 构建、测试和使用包脚本
docs/            两个平台的使用说明和发布说明
.github/         两个平台构建及 Release 发布流程
```

运行时只依赖各平台系统框架。空闲时等待系统事件，无持续绘制或逐秒轮询；样式和节日标注均离线处理。

农历节日和清明支持 1901—2100 年，固定公历节日不受此范围限制。仅显示节日名称，不代表放假或调休安排。离线日期表由 [Microsoft ChineseLunisolarCalendar](https://learn.microsoft.com/en-us/dotnet/api/system.globalization.chineselunisolarcalendar) 生成，并参考[香港天文台公历与农历日期对照表](https://www.hko.gov.hk/tc/gts/time/conversion.htm)核对。

## 从源码构建

Windows 在 PowerShell 中运行：

```powershell
./tools/build.ps1 -ZigPath 'C:\path\to\zig.exe'
./tools/test.ps1 -ZigPath 'C:\path\to\zig.exe'
./tools/package-windows.ps1 -Version '1.0.0'
```

构建使用 Zig 0.15.2；省略路径时查找已有编译器，需要下载时校验官方 SHA-256。中间文件位于 TEMP，成品输出到 `dist/豆泥日历`，使用包输出到 `release`。构建前先退出日历，脚本验证 Windows GUI 子系统后原子替换 exe。打包脚本只允许主程序和使用说明进入压缩包，排除个人 `data`。

macOS 需要 Xcode Command Line Tools，在终端运行：

```sh
DOUNI_VERSION=1.0.0 bash src/macos/build.sh "$PWD/dist/DouNi-Calendar-macOS"
"dist/DouNi-Calendar-macOS/DouNi Calendar.app/Contents/MacOS/DouNiCalendar" --self-test
```

脚本分别编译 arm64 和 x86_64，再合并为 Universal 应用，并进行本地签名检查。未配置 Apple Developer ID 签名及公证，macOS 可能要求按系统流程确认开发者来源。

Windows 的 `main.cpp` 管理窗口、绘制与托盘，`inline_editor.cpp` 提供无边框原位编辑控件，`dates.cpp` 管理日期格和节日标注，`storage.cpp` 管理本地文件。测试使用隐藏自有窗口和独立临时数据，不修改个人记事或自启动项。Mac 的 `--self-test` 使用独立临时目录验证日期和存储，不启动日历界面。

“编辑模式”启用年月导航和日期格记事，完成后恢复点击穿透。内部测试不等同于真实桌面鼠标验收；穿透、托盘或菜单栏、显示桌面及多屏热插拔仍需在目标电脑实际检查。

## 发布

推送 `v版本号` 标签触发 `.github/workflows/release.yml`，也可对已存在的标签手动运行。两个平台构建成功后，流程创建或更新对应 Release，每个平台保留一个同名使用包，重跑会替换同名资产，不额外上传 exe、app 或源码压缩包。
