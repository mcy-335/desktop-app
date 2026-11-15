# LetterPet 正式版

Windows 桌宠客户端与账号、商城服务端。发布整理日期：2026-11-15。

- [客户端](客户端/)：可运行程序、四个 Live2D 角色、五套主题、悬挂配饰、C/C++ 源码和构建脚本。
- [服务端](服务端/)：Python 账号服务、管理后台、商城、配饰素材及测试。
- [旧版分支](https://github.com/mcy-335/desktop-app/tree/legacy-before-client-server-2026-11-15)：保留改版前代码与旧版归档压缩包。

## 启动

1. 下载仓库 ZIP 并完整解压。
2. 服务端电脑安装 Python 3.10 或更新版本，运行 `服务端/启动服务端.cmd`。
3. Windows 10/11 x64 上运行 `客户端/启动桌宠.bat`，保留旁边的 assets 和 live2d-runtime。
4. 同机测试填写 `http://127.0.0.1:8765`；跨电脑填写已配置的 HTTPS 服务地址。
5. 填写账号和密码，勾选「自动登录」后注册或登录，桌宠随即启动。

服务端新部署时管理员初始化及联机设置见 [服务端说明](服务端/server/README.md)。升级已有服务时保留原数据库和启动参数。本仓库不包含真实用户数据库、密码或登录缓存。

## 本次专注规则

阅读、听课无需持续操作键鼠。白名单外有 3 秒宽限，超时暂停有效计时，返回允许应用自动继续。亲密度每 10 分钟有效专注 +1，零头跨轮保存；中途结束保留收益，完成目标才增加轮次。代币每分钟 1 枚，需服务端在线确认，每天最多计入 180 分钟。

## 构建与检查

客户端已附正式版 EXE。重编 C 客户端：在客户端目录运行 `build-msvc.cmd`，需 Visual Studio C++ Build Tools；附带的 Live2D DLL 可直接使用。重编 Live2D 桥接库需另行安装 Cubism SDK for Native，并运行 `live2d-preview/build-preview.ps1 -SdkPath <SDK目录>`；第三方许可保留在 `live2d-runtime/licenses`。

在客户端目录运行 `LetterPet.exe --companion-ui-test` 或 `LetterPet.exe --integration-test` 可执行自检；在服务端目录运行 `python -m unittest discover -s tests -p test_*.py -v` 可运行服务端测试。

本次对应程序已通过 207 条原生自检记录、6 条输入/渲染集成记录、8 项服务端测试及真实客户端联机回归。每个文件夹内的 SHA256SUMS.txt 可用于核对文件完整性。
