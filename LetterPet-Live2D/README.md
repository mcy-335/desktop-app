# LetterPet Live2D 正式版

此文件夹是完整项目副本，与仓库根目录的旧版独立。

## 运行

双击本文件夹的 `启动桌宠.bat` 或 `LetterPet.exe`。保留旁边的 `assets` 和 `live2d-runtime`。
需要 Windows 10/11 x64；如缺少依赖，请安装 Microsoft Visual C++ x64 运行库。
在其他程序打字即可驱动动作；左键拖动人物，右键打开缩放和退出菜单。
本版沿用正式桌宠的用户设置和单实例机制，不建议同时运行旧版。

## 本版内容

- Cubism joints-v2 模型，B 版 85% 键盘及原信封键帽。
- 真实键鼠输入驱动肩、肘、腕；保留眨眼。
- 86 个逻辑键、60 个可见键帽；无可见键帽的按键不误亮其他键。
- 自然可达范围内跟随，不保证全键精准触碰，不拉长手臂。
- 最新按键优先，组合键分别反馈；鼠标整手轻点。

## 源码与工程

- `src/`：现有 C 主程序和 C++ Live2D 接口。
- `live2d-preview/arm/`：正式手臂、键帽和输入逻辑。
- `assets/live2d/runtime/pet/`：运行模型、纹理、眨眼动作。
- `assets/live2d/arm-rig-v1/pet-arm-rig-joints-v2.cmo3`：最终可编辑工程。
- `live2d-runtime/licenses/`：随附的 SDK/Core 许可说明。

编译需另行安装 MinGW-w64、Visual Studio C++ 工具和 Cubism SDK for Native；完整 SDK 不包含在此包。
在本文件夹执行 `./build.ps1 -SdkPath '你的 Cubism SDK 路径'`。
`live2d-preview` 同时包含历史独立预览源码，正式桌宠入口为根目录 `LetterPet.exe`。

## 检查

本版本程序级集成检查已通过。运行 `./LetterPet.exe --integration-test` 可重新执行，不截屏。
版本及运行文件 SHA256 见 `live2d-release.json`，本包完整文件清单见 `SHA256SUMS.json`。
未包含本地备份、临时测试版、编译缓存、日志或个人设置。
