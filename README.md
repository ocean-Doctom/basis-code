# Basis Code

源码仓库：https://github.com/ocean-Doctom/basis-code · 许可证：[Apache-2.0](LICENSE)。第三方依赖保留各自许可证。

1.4.4 修复资源管理器删除取消后仍删除目录的问题；移动、重命名同步已打开文档路径与语法类型，保留未保存内容。禁止递归复制到自身子目录，跨磁盘移动交给系统文件管理器。

1.4.3 优化增量构建：仅修改版本号时只需重新编译启动文件并链接，避免整个主程序重编译；运行功能沿用 1.4.2。

1.4.2 修复编译完成到程序启动之间的短暂状态交接：保持忙碌、防止重复构建覆盖待启动信息，并正确处理此时的停止操作。

1.4.1 修复跨数据块的编译诊断解析，避免把普通运行输出误报为编译问题；问题项可定位到源文件行列，并改善 Python 中文与表情输出的分块解码。

1.4.0 增加自动大文件模式：超过约 20 Mi 个 UTF-16 单元或 10 万行时暂停语法高亮，在状态栏明确提示；文档缩小后恢复高亮。编辑、行号和光标状态仍可使用。

1.3.1 优化查找替换和项目搜索的原生界面，显示源文档、匹配上下文，并明确提示哪些文件内容因过大或权限问题未参与搜索。已检查实际亮色与暗色窗口截图。

1.3.0 增加 Find and Replace 文档查找替换（Ctrl+F / Ctrl+H），支持整词、大小写、循环查找和一次撤销批量替换；扩展记住最后编辑文档，并在源文档关闭后停用操作。Agent 的非交互命令输入与输出截断处理也已补强。

1.2.1 修复了真实 Windows 目录联接测试发现的工作目录边界问题，并修正 Agent 子目录列表深度、原生命令退出码和扫描资源限制。建议使用这一版本或更高版本。

1.2.0 增加可取消的 Project Search 后台项目搜索、聊天服务连接检查，修复多文档插件绑定与超长行注释状态，限制 Git 输出内存。应用和安装包版本统一读取 APP_VERSION.txt。

1.1.0 增加可用的 Markdown Preview、JSON Tools、Whitespace Cleaner、Git Changes 原生扩展。在扩展列表点击打开；格式化作用于打开扩展时绑定的文档，可撤销。它们提供常见编辑器扩展能力，不支持直接安装 VS Code VSIX。完整开发、性能和验证记录见 [DEVELOPMENT.md](DEVELOPMENT.md)。

使用 C++17 和 Qt 6 Widgets 的轻量代码编辑器：最左侧图标栏、左侧项目文件树、右侧多选项卡代码区。
文件/编辑/视图/帮助和保存入口合并进窗口标题栏，与最小化、最大化、关闭按钮同一行。
标题栏空白处可拖动窗口，双击最大化/还原；Windows 支持边缘缩放。

## 新对话交接

实际项目目录是 `C:\Users\26320\.vscode\projects\Qt app`。请在这个目录阅读和修改代码。同名的 `Documents\ChatGPT\Qt app` 是另一个项目目录。

- **学习方式**　用户从零学习 C++ 和 Qt Widgets。“一次一个小步骤”指一次发送的需求整体完成，不要把同一条消息的多项要求拆到多个回合。结合当前代码解释原理。本轮已获用户授权提交和发布 GitHub，仓库 ocean-Doctom/basis-code，许可证 Apache-2.0；按已验证的提交生成安装包。
- **最近验证**　当前构建和 5 组 CTest 通过。新增临时真实 Git 仓库差异、后台工具 UI 心跳、JSON 多文档绑定、Markdown 刷新、后台搜索和超长行注释状态测试。原生 Qt 登录元数据 HTTPS 检查返回 HTTP 200，未代用户完成 OAuth 登录或付费推理。工具链下载继续保持用户选择，仅显式传 LanguageTests.exe --download 才执行下载安装测试。最新验证细节见 DEVELOPMENT.md。
- **最新界面**　ChatGPT 现在是 Qt 原生消息区、模型选择和输入框，通过官方 Sign in with ChatGPT 动态客户端 PKCE 流程登录。授权在系统浏览器进行，回调为本机 127.0.0.1 的 /auth/callback。验证 ID token 的 RS256/JWKS、issuer、audience、nonce 和账号身份后，使用公开 Responses API 流式聊天；账号需要具备对应计划使用授权。凭据保存在 extensions/basis.chatgpt/credentials.bin，使用 Windows DPAPI 用户级加密和跨窗口文件锁，支持刷新、账号切换和撤销退出。开发者未代用户完成登录或发消息，这部分需用户实测。旧 WebView2 网页依赖不再参与构建。
- **DeepSeek 与默认 Agent**　首次启动在代码区选择 ChatGPT 或 DeepSeek，选择后持久化；文件→首选项或左下角设置可更改默认 Agent。DeepSeek 使用官方 API，首次从账户菜单配置 API Key，通过 /models 动态获取模型、/chat/completions 流式回复。密钥与聊天历史独立保存到 extensions/basis.deepseek/credentials.bin，使用 Windows DPAPI 加密；可切换密钥、移除本机密钥。已用本地数据验证分片 SSE、密钥加密恢复和移除，未使用真实 API Key 或付费推理，需用户实测。
- **项目管理**　资源管理器项目名称前的箭头返回历史列表；侧栏不再单独显示打开文件夹按钮，文件菜单仍可打开文件夹。单击项目后打开，双击修改独立显示名称，右键可修改实际文件夹名；重命名会同步打开文件路径并保留未保存内容。文件菜单参照 VS Code 分组，含最近文件、工作区保存/打开、文件夹切换、全部保存、自动保存、还原和关闭文件夹。工作区使用 .code-workspace JSON，多个根通过左侧文件夹列表切换。
- **内嵌配置和终端**　语言包详情直接嵌入开发工具配置，不再弹独立配置窗口。编译器/JDK/Python 仍由用户选择下载或本机路径；运行支持是随软件提供、按语言选择安装的内置组件，安装后应用内 F5 才启用。终端使用 UTF-8 PowerShell 命令输入框（每条命令独立执行并保留工作目录，不保留任意会话变量，不是完整 PTY），可以不安装运行组件直接编译运行，已配置的工具目录会注入新终端 PATH。右侧输出区分为运行与调试、终端两个标签。Ctrl+反引号打开终端，命令行通过输入框 Enter 提交。
- **主题**　查看、设置和文件→首选项均可切换亮色、Tokyo Night、GitHub Dark Default。保留 Tokyo Night，另加入依据 primer/github-vscode-theme 官方配色移植的 GitHub Dark Default，亮色提高文字及边界对比度，设置持久化；语法高亮与行号随主题切换。
- **当前进度**　已有多文档编辑、行号、项目树、扩展详情、项目与应用数据目录管理，以及 C/C++ 编译器管理和单文件编译运行。
- **存储**　默认项目位于“文档/Basis Code/Projects”，可以在设置菜单的“存储与项目”中更改。应用数据位于 Qt 的 AppLocalDataLocation（通常是 `%LOCALAPPDATA%/Basis Code`），其下 `extensions` 存插件清单、`toolchains` 存完整编译器、`downloads` 存下载包、`cache/builds` 存按源码路径区分的构建产物、`settings.ini` 存设置和最近项目。环境变量 `BASIS_CODE_HOME` 可指定独立数据目录；自动测试使用临时目录。
- **C/C++ 工具包**　编译器项可选择本机 g++/clang++，或从 MinGW-w64 官网列出的 WinLibs（GCC）与 LLVM-MinGW（Clang）发行版维护者 GitHub 仓库查询 Windows x64 UCRT ZIP 包。用户选定版本后才下载；流式写入 `.part`，有 SHA-256 摘要时核验，检查包内路径，使用 Windows `tar.exe` 解压到临时目录后提交安装，再持久化当前编译器。支持取消、错误提示和切换已安装版本。Java 已支持 Temurin JDK 21 下载、本机 JDK 选择和 javac/java/jdb；Python 已支持 Python 官方 NuGet 包下载、本机解释器选择和 pdb。
- **运行入口**　左侧“调试与运行”按钮或 F5，先保存当前 C/C++ 文件，然后用 QProcess 异步编译；仅在成功后启动产物。支持输出、标准输入、结束输入和停止。Java / Python 使用 Shift+F5 启动 JDB / PDB 命令调试，输出区有继续、单步、进入、变量按钮，断点可在输入框用调试器命令设置。运行输出区在右侧代码区内部下方，可拖动调整高度。当前按单个入口源文件构建。
- **接手入口**　`codeeditor.h/.cpp` 是行号编辑器；`devtools.h/.cpp` 管理存储、编译器下载及运行；`notepad.cpp` 连接界面。`tests/devtools_tests.cpp` 使用本机编译器实际检查 C/C++、中文路径、输入、编译失败和停止流程。`DevToolsTests.exe --network` 额外验证两个发布仓库的 Qt HTTPS 版本查询；默认回归不依赖网络。大型安装包完整下载解压尚未端到端实测。

## 功能

- 首次启动选择默认 Agent（ChatGPT / DeepSeek），后续启动打开所选的原生聊天插件标签页。项目侧栏显示最近项目，点击打开项目文件夹后才启用新建文件。
- 新建、打开、保存、另存为；另存为默认补上 `.txt` 后缀。
- 图标栏切换项目树和扩展。扩展按语言组织，搜索框过滤语言包列表，点击条目即可在代码区打开独立的工具包详情标签页。左下角的设置菜单可以调整当前文档字体、自动换行，以及侧栏可见性。
- 项目文件树支持展开目录、双击打开文件；支持项目显示名称和项目文件夹重命名。
- 顶部选项卡支持新建、切换、拖动排序和关闭，项目树选中项随当前文件同步。
- 代码区显示行号，随滚动、字体及行数变化更新；自动换行仍按原始文本行编号。
- 界面恢复 Microsoft YaHei UI 10pt，编辑区恢复 Cascadia Code / Cascadia Mono / Consolas 等宽字体 12pt；Ctrl++（或 Ctrl+=）放大显示，Ctrl+- 缩小显示。代码区使用 QPlainTextEdit，Tab 显示宽度为 4 个字符，默认不自动换行。
- 编辑区按扩展名自动高亮 C/C++、Java、Python、JavaScript/TypeScript、JSON、HTML/XML、CSS/QSS、Markdown 和 CMake 的常见语法。当前没有代码补全；C/C++ 支持单文件编译运行，Java / Python 支持命令调试。
- 每个文档独立保存路径、内容、撤销记录和换行设置。
- 新建和打开在独立选项卡中进行，保留其他文档。重复打开同一文件会切到已有选项卡。
- 关闭文档时提示保存；退出时逐个检查未保存文档，取消后保留全部选项卡。
- UTF-8 文本读写（支持 BOM 自动检测），使用 QSaveFile 提交文件，避免写入失败破坏原文件。
- 撤销、重做、剪切、复制、粘贴、全选，以及标准快捷键。
- 顶部文件/编辑/视图/帮助菜单和保存按钮、文件名与未保存标记、行列与字符数状态栏。
- 自动换行、字体选择、打印（包括系统提供的打印到 PDF）、帮助菜单。

字体仅影响编辑器显示和打印。文件保存为纯文本，不保存字体、颜色或加粗等格式。
未带 BOM 的输入文件按 UTF-8 解码；旧的 GBK/ANSI 文件请先转换为 UTF-8。

## 文件结构

- `main.cpp`：创建应用和窗口，可接收文件或项目文件夹路径作为启动参数。
- `notepad.h`：Notepad 类声明及文档状态。
- `codeeditor.h/.cpp`：代码编辑器和行号绘制。
- `devtools.h/.cpp`：项目与插件目录、工具链下载管理、异步编译与运行。
- `notepad.cpp`：界面、信号连接、读写、保存提示和打印逻辑。
- `windowtitlebar.h/.cpp`：集成标题栏、拖动和窗口控制按钮。
- `theme.qss`、`close.svg`、`icons/`：界面样式和图标，构建时嵌入程序资源。
- `tests/notepad_tests.cpp`：使用临时文件的集成检查，不改用户文件。
- `.clang-format`：统一 C++ 代码风格。

## 构建和运行

## Windows 安装包

`installer/package.ps1` 使用独立 Release 构建和 NSIS 3 生成安装程序。先从 https://nsis.sourceforge.io/Download 获取并解压 NSIS ZIP，再在项目目录执行：

```powershell
.\installer\package.ps1 -MakeNsis 'C:\path\nsis\makensis.exe'
```

默认输出 `build/installers/BasisCode-Setup-<VERSION>-x64.exe` 和 SHA-256 校验文件。安装包只收集主程序、Qt/MinGW 运行库和插件、第三方声明；不包含测试程序、API Key、聊天记录、项目或语言编译器。当前用户安装，无需管理员权限，带桌面/开始菜单快捷方式和卸载入口；卸载逐项删除发布文件，保留个人数据。发布前请在另一台 Windows x64 电脑上试装。当前安装包未进行代码签名。

### 开发构建

VS Code 打开本目录后，使用 **CMake: 生成** 和 **CMake: 运行但不调试**。
项目现有 `.vscode` 为本机配置，已被 Git 忽略。

本机 PowerShell 命令：

```powershell
$env:PATH = "C:\Qt\6.12.0\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;$env:PATH"
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" -S . -B build -G Ninja `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build build
.\build\QtApp.exe
```

Windows 构建会自动执行 `windeployqt`，将匹配的运行库和插件放到 `build`，
避免 PATH 中其他软件自带的同名 Qt DLL 冲突。移动程序时需一起复制部署的 DLL 和插件目录。

## 验证

```powershell
& "C:\Qt\Tools\CMake_64\bin\ctest.exe" --test-dir build --output-on-failure
```

自动检查覆盖项目文件树加载、双击文件打开、微软雅黑和显示缩放、Unicode 文件往返、独立文档保存、选项卡排序、重复打开、关闭提示、失败路径，以及每个文档的撤销和重做。
打印依赖系统打印机驱动，可手动选择 Microsoft Print to PDF 验证。

常用快捷键：Ctrl+N 新建，Ctrl+O 打开文件，Ctrl+K、Ctrl+O 打开文件夹，Ctrl+S 保存，Ctrl+Shift+S 另存为，Ctrl+W 关闭当前文档，Ctrl+Tab / Ctrl+Shift+Tab 切换文档，Ctrl+\ 切换侧栏。

## C/C++ 使用方法

1. 左下角设置 → **存储与项目**，新建 C++ 项目或查看存储目录；现有项目仍可用“打开项目文件夹”打开。
2. 扩展 → **C/C++** → **选择、下载和安装 GCC / Clang**。
3. 点击“安装 MinGW”或“安装 Clang”获取版本，选中版本后下载并安装。各按钮右侧的箭头可选择电脑中已安装的对应编译器。
4. 打开 `.c` / `.cpp` / `.cc` / `.cxx` 源文件，点击左侧调试与运行图标，或按 F5。未保存文件会先提示保存。
5. 在底部查看编译信息和程序输出，输入框按回车发送输入；“结束输入”发送 EOF，“停止”终止当前编译或程序。

编译器来源说明：[MinGW-w64 官方发行版列表](https://www.mingw-w64.org/downloads/)、[WinLibs](https://winlibs.com/)、[LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw)。软件显示发行版名称，不把这些维护者构建标为 LLVM 官方安装器。

Java 和 Python 在对应语言扩展页中配置。F5 运行当前文件，Shift+F5 调试。PDB 可输入 b 行号、n、c、p 变量；JDB 可输入 stop at 类名:行号、next、cont、locals。Java 需要完整 JDK，选择其中的 bin/javac.exe。

本机 Python/PDB 和完整 JDK/中文 JDB 的实际运行、断点、单步、变量查看与继续执行均已验证，Java 编译失败会阻止运行旧产物。可通过 `BASIS_TEST_PYTHON` 和 `BASIS_TEST_JAVAC` 指向已有工具，运行 `build/LanguageTests.exe` 复验；不带 `--download` 时不下载安装包，配置与源码使用临时目录。

ChatGPT 接入依据为 OpenAI 官方文档 https://developers.openai.com/siwc/token-sharing-open-source/sign-in 与 https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference 。主题配色参考 https://github.com/tokyo-night/tokyo-night-vscode-theme 。ChatGPT 图标取自官网原始资源，保留原图形。

ChatGPT 原生界面采用居中欢迎语、限宽消息列表、右侧用户气泡和圆角输入框；新对话时输入框居中，开始聊天后在底部显示。账号登录、切换和退出收纳到右上角账户菜单。

DeepSeek API 接入依据：[模型列表](https://api-docs.deepseek.com/api/list-models/)和[流式聊天接口](https://api-docs.deepseek.com/api/create-chat-completion/)。未自动安装工具链或发起付费模型调用。
