<div align="center">
  <a href="https://github.com/Over9rowth">
    <img src="./assets/suiren-badge.svg" alt="燧人计划 · OVER9ROWTH" width="460" />
  </a>

  # LiteIME

  **极简、纯净、零遥测的 Windows 中英文双语 TSF 输入法（C++20 + Direct2D）**

  `#燧人计划` · [![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11%20x64-2563EB?style=flat-square)](#) [![Core](https://img.shields.io/badge/Core-C%2B%2B20%20%7C%20TSF%20%7C%20Direct2D-0F766E?style=flat-square)](#) [![Manual](https://img.shields.io/badge/Docs-%E8%AF%A6%E5%B0%BD%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E%E4%B9%A6%20PDF-D97706?style=flat-square)](./docs/LiteIME_User_Manual.pdf)

  **[📘 阅读完整 PDF 使用说明书 (`docs/LiteIME_User_Manual.pdf`)](./docs/LiteIME_User_Manual.pdf)**
</div>

---

## ✨ 项目简介与「燧人计划」初心

> 「星星之火，始于你我。」—— 本项目隶属个人主页 [**Over9rowth**](https://github.com/Over9rowth) 的 **`#燧人计划`** 独立工程系列，专注于长期打磨轻量、克制、可完全离线掌控的实用工具。

**LiteIME** 放弃 Electron、WebView 与臃肿的后台服务，完全采用原生 **Windows Text Services Framework (TSF)**、**Named Pipe 双进程 IPC** 与 **Direct2D / DirectWrite** 高 DPI 自绘渲染从零打造，所有词库、自学习记忆与隐私短语均 100% 存储在本地。

---

## 🚀 核心特性一览

- **⚡ 双进程低延迟架构**：
  - 应用内仅加载轻量无词库 COM 组件 **`LiteIME64.dll`**（支持 UWP / Edge / Windows Terminal AppContainer 只读最小权限）。
  - 独立守护进程 **`LiteIMEHost.exe`** 常驻词典索引、维特比整句解码、自学习与 Direct2D 候选 UI。
- **🔄 流畅的中英文混合输入**：
  - **`Shift` 单击秒切**：纯粹点按 `Shift` 立即切换中/英文并提交当前原串；组合键（如 `Ctrl+Shift`）不误触发。
  - **多维度辅助模式**：`v` 临时英文补全模式（上屏自动剥离前导 `v`）、URL/邮箱半角锁定、`3.14` 数字小数点智能识别、`rq / sj / xq` 动态日期星期、`dan'g` 显式单引号分隔符保留与多步部分上屏撤回。
  - **配对引号防误判**：中文引号 `“”` / `‘’` 自动配对，退格误删即时回滚、应用失焦自动复位。
- **🧠 可控个性化与隐私守护**：
  - **阶梯词频 + 右键固位 `[固1..固5]`**：右键直接固定首选或删除误学词；打字中支持 `Ctrl+Delete` / `Ctrl+Shift+1..9` 现场删词。
  - **`vvv` 脱敏快输入**：录入常用邮箱、学号/工号、手机号或地址后，打字框仅展示掩码预览（如 `zhang***`），确认上屏才输出原文。
- **🎨 原生视觉与可定制生态**：
  - **4 套 Direct2D 主题**：`云雾琉璃 CloudMist` · `苍翠竹青 MatchaJade` · `樱粉柔雾 SakuraCream` · `星曜玄夜 ObsidianInk`，支持方框/胶囊切换、横/竖排与 `5~8` 候选数量。
  - **3 种文本框内预编辑模式**：首选汉字预览 / 已确认字 + 未确认拼音 / 框内零干扰不出现字。
  - **悬浮状态栏 & 热编译用户词库**：状态栏四区单点切换、跨屏拖拽持久化；将 `.yaml / .tsv / .txt` 放入 `%APPDATA%\LiteIME\dicts\` 点击托盘「重新加载词库」即自动编译加载。

---

## 🛠️ 从源码构建与安装

### 1. 构建准备
需要安装 **MSYS2 / MinGW-W64 (`g++` C++20)**、**CMake**、**Ninja** 与 **Python 3**。

```powershell
Set-Location D:\LiteIME

# 拉取并编译基础词库与各扩展词库包
python scripts/fetch_and_compile_dict.py
python scripts/build_common_en_pack.py
python scripts/build_domain_packs.py

# 全量编译二进制与测试套件
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

### 2. 运行测试与 IPC 冒烟校验

```powershell
ctest --test-dir build --output-on-failure
python tests/host_ipc_smoke.py
```

### 3. 安装与卸载（需管理员权限 PowerShell）

将 `build/` 下的 `LiteIME64.dll`、`LiteIMEHost.exe` 及编译好的 `data/` 整理至 `dist/` 后执行：

```powershell
# 注册 TSF 组件、赋予 UWP AppContainer 权限并激活输入法
powershell -ExecutionPolicy Bypass -File .\scripts\install.ps1

# 卸载输入法（保留 %APPDATA%\LiteIME 个人词库与配置）
powershell -ExecutionPolicy Bypass -File .\scripts\uninstall.ps1
```

按 <kbd>Win</kbd> + <kbd>Space</kbd> 选择 **LiteIME** 即可开始使用。更详尽的功能说明、快捷键全表与 `config.ini` 字段请查阅：👉 **[`docs/LiteIME_User_Manual.pdf`](./docs/LiteIME_User_Manual.pdf)**

---

## 📜 开源词库声明

- 为保持源码仓库精简并严格遵守协议隔离，本仓库不包含大体积第三方二进制词库或原始字典源码；
- `scripts/fetch_and_compile_dict.py` 与 `scripts/build_domain_packs.py` 从开源项目 **[iDvel/rime-ice](https://github.com/iDvel/rime-ice)**（GPLv3）及清华大学开放中文词库 **THUOCL** 下载词表并在本地编译生成二进制索引文件。

---

<div align="center">
  <sub>Made with C++20 & Direct2D by <a href="https://github.com/Over9rowth">Over9rowth</a> · <b>#燧人计划</b></sub>
</div>
