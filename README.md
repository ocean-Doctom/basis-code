# Basis Code

使用 C++17 和 Qt 6 Widgets 的轻量代码编辑器：最左侧图标栏、左侧项目文件树、右侧多选项卡代码区。
文件/编辑/视图/帮助和保存入口合并进窗口标题栏，与最小化、最大化、关闭按钮同一行。
标题栏空白处可拖动窗口，双击最大化/还原；Windows 支持边缘缩放。

## 功能

- 新建、打开、保存、另存为；另存为默认补上 `.txt` 后缀。
- 图标栏提供项目树显示/隐藏、打开文件夹、新建文件和保存入口。左下角的设置菜单可以调整当前文档字体、自动换行，以及侧栏可见性。
- 项目文件树支持展开目录、双击打开文件；仅浏览，不提供删除、重命名等文件管理操作。
- 顶部选项卡支持新建、切换、拖动排序和关闭，项目树选中项随当前文件同步。
- 代码区使用 QPlainTextEdit，优先选择本机 Cascadia Code / Cascadia Mono / Consolas 等等宽字体，字号 12pt，Tab 显示宽度为 4 个字符，默认不自动换行。
- 暂不实现语法高亮、代码补全、运行或调试。
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
- `notepad.cpp`：界面、信号连接、读写、保存提示和打印逻辑。
- `windowtitlebar.h/.cpp`：集成标题栏、拖动和窗口控制按钮。
- `theme.qss`、`close.svg`、`icons/`：界面样式和图标，构建时嵌入程序资源。
- `tests/notepad_tests.cpp`：使用临时文件的集成检查，不改用户文件。
- `.clang-format`：统一 C++ 代码风格。

## 构建和运行

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

自动检查覆盖项目文件树加载、双击文件打开、等宽字体、Unicode 文件往返、独立文档保存、选项卡排序、重复打开、关闭提示、失败路径，以及每个文档的撤销和重做。
打印依赖系统打印机驱动，可手动选择 Microsoft Print to PDF 验证。

常用快捷键：Ctrl+N 新建，Ctrl+O 打开文件，Ctrl+K 打开文件夹，Ctrl+S 保存，Ctrl+Shift+S 另存为，Ctrl+W 关闭当前文档，Ctrl+Tab / Ctrl+Shift+Tab 切换文档，Ctrl+\ 切换侧栏。
