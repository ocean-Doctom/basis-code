#include "notepad.h"
#include <QAbstractButton>
#include <QAction>
#include <QListWidget>
#include <QLineEdit>
#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileSystemModel>
#include <QFontMetricsF>
#include <QThread>
#include <QSplitter>
#include <QTreeView>

#include <QMessageBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QSettings>
#include <QLabel>
#include <QInputDialog>
#include <QTextBrowser>
#include <QScrollArea>
#include <QPushButton>
#include <QMenu>
#include <QTextDocument>
#include <QPlainTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QMouseEvent>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif
#include <cstdlib>
#include <iostream>
#include <stdexcept>

static int checks = 0;
static void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
    ++checks;
}
static void answerMessage(QMessageBox::StandardButton answer) {
    QTimer::singleShot(0, [answer] {
        auto *box =
            qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box || !box->button(answer)) {
            std::cerr << "Expected message box\n";
            std::exit(2);
        }
        box->button(answer)->click();
    });
}
static void cancelFileDialog() {
    QTimer::singleShot(0, [] {
        auto *dialog =
            qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            std::exit(2);
        dialog->reject();
    });
}
static QByteArray readText(const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly | QIODevice::Text), "read saved file");
    return file.readAll();
}
static void writeText(const QString &path, const QByteArray &text) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly | QIODevice::Text),
          "create test fixture");
    check(file.write(text) == text.size(), "write fixture");
}
int main(int argc, char *argv[]) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    if (!app.arguments().contains("--webview-smoke")) qputenv("BASIS_DISABLE_WEBVIEW", "1");
    QTimer::singleShot(app.arguments().contains("--benchmark") ? 60000 : 20000, [] {
        std::cerr << "Timeout\n";
        std::exit(2);
    });
    try {
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary directory");
        qputenv("BASIS_CODE_HOME", temporary.filePath("app-data").toUtf8());
        QSettings history(temporary.filePath("app-data/settings.ini"), QSettings::IniFormat);
        history.setValue("agents/default", "chatgpt");
        history.setValue("projects/recent", QStringList{temporary.path(), temporary.filePath("missing-project")});
        history.sync();
        {
            QTemporaryDir firstUse;
            qputenv("BASIS_CODE_HOME", firstUse.path().toUtf8());
            {
                Notepad onboarding;
                onboarding.show(); app.processEvents();
                check(onboarding.findChild<QWidget *>("defaultAgentChooser"), "first launch asks for default agent");
                auto *choice = onboarding.findChild<QPushButton *>("chooseDeepSeek");
                check(choice && !choice->icon().isNull(), "DeepSeek has a native icon on chooser");
                if (qEnvironmentVariableIsSet("BASIS_AGENT_PREVIEW_PATH"))
                    check(onboarding.grab().save(qEnvironmentVariable("BASIS_AGENT_PREVIEW_PATH")), "render agent chooser");
                choice->click(); app.processEvents();
                check(onboarding.findChild<QWidget *>("deepseekPage"), "choosing DeepSeek opens native chat");
                check(onboarding.findChild<QAction *>("actionDefaultDeepSeek")->isChecked(), "default agent menu reflects selection");
                if (qEnvironmentVariableIsSet("BASIS_DEEPSEEK_PREVIEW_PATH")) {
                    onboarding.findChild<QAction *>("actionGitHubDarkTheme")->trigger(); app.processEvents();
                    check(onboarding.grab().save(qEnvironmentVariable("BASIS_DEEPSEEK_PREVIEW_PATH")), "render DeepSeek");
                }
            }
            {
                Notepad returning;
                check(returning.findChild<QWidget *>("deepseekPage") && !returning.findChild<QWidget *>("defaultAgentChooser"),
                      "restart remembers selected default agent");
                returning.findChild<QAction *>("actionDefaultChatGPT")->trigger();
                check(returning.findChild<QWidget *>("chatgptPage"), "preferences switch default agent");
            }
        }
        qputenv("BASIS_CODE_HOME", temporary.filePath("app-data").toUtf8());
        Notepad window;
        window.show();
        app.processEvents();
        if (qEnvironmentVariableIsSet("BASIS_CHAT_PREVIEW_PATH")) {
            window.findChild<QAction *>("actionGitHubDarkTheme")->trigger();
            app.processEvents();
            check(window.grab().save(qEnvironmentVariable("BASIS_CHAT_PREVIEW_PATH")), "render native ChatGPT welcome");
            window.findChild<QAction *>("actionLightTheme")->trigger();
        }
        auto *titleBar = window.findChild<QFrame *>("windowTitleBar");
        auto *minimize = window.findChild<QToolButton *>("windowMinimize");
        auto *maximize = window.findChild<QToolButton *>("windowMaximize");
        auto *closeWindow = window.findChild<QToolButton *>("windowClose");
        check(titleBar && minimize && maximize && closeWindow,
              "integrated title bar has window controls");
        check(window.windowFlags().testFlag(Qt::FramelessWindowHint),
              "native title bar replaced by integrated header");
        check(titleBar->mapTo(&window, QPoint()).y() == 0 &&
                  titleBar->width() == window.centralWidget()->width(),
              "title bar spans the full window at the top");
        maximize->click();
        app.processEvents();
        check(window.isMaximized() && window.mask().isEmpty(), "maximized window uses full rectangular area");
        maximize->click();
        app.processEvents();
        check(!window.isMaximized() && window.mask().isEmpty(),
              "restored window keeps native compositor smoothing");
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(250, 20),
                                QPointF(titleBar->mapToGlobal(QPoint(250, 20))),
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(titleBar, &doubleClick);
        app.processEvents();
        check(window.isMaximized(), "double click title bar maximizes window");
        maximize->click();
        minimize->click();
        app.processEvents();
        check(window.isMinimized(), "minimize button minimizes window");
        window.showNormal();
        app.processEvents();
#ifdef Q_OS_WIN
        if (QApplication::platformName() == "windows") {
            const HWND handle = reinterpret_cast<HWND>(window.winId());
            DWORD cornerPreference = 0;
            const HRESULT cornerResult = DwmGetWindowAttribute(handle, 33, &cornerPreference, sizeof(cornerPreference));
            if (SUCCEEDED(cornerResult)) {
                check(cornerPreference == 2, "native compositor rounds restored window");
                const COLORREF noBorder = 0xfffffffe;
                check(SUCCEEDED(DwmSetWindowAttribute(handle, 34, &noBorder, sizeof(noBorder))),
                      "compositor accepts border suppression");

            }
            RECT frame;
            check(GetWindowRect(handle, &frame), "query native window frame");
            const LRESULT edge =
                SendMessage(handle, WM_NCHITTEST, 0,
                            MAKELPARAM(frame.left + 1, frame.top + 1));
            check(edge == HTTOPLEFT, "native window corner supports resizing");
        }
#endif
        auto *tabs = window.findChild<QTabWidget *>("documentTabs");
        auto *tree = window.findChild<QTreeView *>("projectTree");
        auto *model = window.findChild<QFileSystemModel *>("projectModel");
        auto *newAction = window.findChild<QAction *>("actionNew");
        auto *closeAction = window.findChild<QAction *>("actionClose");
        auto *saveAction = window.findChild<QAction *>("actionSave");
        auto *saveAsAction = window.findChild<QAction *>("actionSaveAs");
        auto *undoAction = window.findChild<QAction *>("actionUndo");
        auto *redoAction = window.findChild<QAction *>("actionRedo");
        auto *wrapAction = window.findChild<QAction *>("actionWrap");
        check(tabs && tree && model && newAction && closeAction && saveAction &&
                  saveAsAction && undoAction && redoAction && wrapAction,
              "workspace and actions exist");
        auto activeEditor = [tabs] {
            return qobject_cast<QPlainTextEdit *>(tabs->currentWidget());
        };
        check(tabs->count() == 1 && tabs->tabText(0) == "ChatGPT" &&
                  !activeEditor() && !newAction->isEnabled(),
              "startup opens ChatGPT and new files require a project");
        auto *extensionsList = window.findChild<QListWidget *>("extensionList");
        const auto chat = extensionsList->findItems("ChatGPT", Qt::MatchExactly);
        check(chat.size() == 1, "ChatGPT is a preinstalled plugin");
        extensionsList->itemClicked(chat.first());
        check(tabs->count() == 1, "opening ChatGPT reuses its tab");
        newAction->trigger();
        check(tabs->count() == 1, "new action cannot create a document without a project");
        check(window.findChild<QScrollArea *>("chatgptMessages") &&
                  window.findChild<QPlainTextEdit *>("chatgptInput") &&
                  !window.findChild<QPushButton *>("chatgptSend")->isEnabled(),
              "ChatGPT provides native Qt chat controls and requires login");
        closeAction->trigger();
        auto *recent = window.findChild<QListWidget *>("recentProjects");
        check(recent && recent->count() == 2 &&
                  !(recent->item(1)->flags() & Qt::ItemIsEnabled),
              "recent projects include usable and unavailable paths");
        recent->itemClicked(recent->item(0));
        QElapsedTimer projectClick;
        projectClick.start();
        while (!tree->isVisible() && projectClick.elapsed() < 1500) {
            app.processEvents(); QThread::msleep(10);
        }
        check(newAction->isEnabled() && tree->isVisible(),
              "clicking a recent project opens the project folder");
        check(tabs->count() == 0, "opening a project does not create an untitled document");
        window.findChild<QToolButton *>("backToProjects")->click();
        check(recent->isVisible(), "project explorer returns to recent projects");
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            if (!dialog) std::exit(2);
            dialog->setTextValue(QString::fromUtf8("我的示例项目"));
            dialog->accept();
        });
        recent->itemDoubleClicked(recent->item(0));
        check(recent->item(0)->text().startsWith(QString::fromUtf8("我的示例项目")) &&
                  QFileInfo::exists(temporary.path()), "project alias preserves its folder name");
        check(window.openProject(temporary.path()), "return to renamed project alias");
        window.findChild<QAction *>("actionDarkTheme")->trigger();
        check(window.palette().color(QPalette::Text).name() == "#c0caf5", "dark theme selected");
        window.findChild<QAction *>("actionLightTheme")->trigger();
        check(window.palette().color(QPalette::Text).name() == "#26334d", "light theme selected");
        window.findChild<QAction *>("actionGitHubDarkTheme")->trigger();
        check(window.palette().color(QPalette::Base).name() == "#0d1117" &&
                  window.palette().color(QPalette::Text).name() == "#e6edf3", "GitHub Dark Default palette selected");
        check(!window.findChild<QToolButton *>("openProjectButton") &&
                  window.findChild<QToolButton *>("backToProjects")->parentWidget() ==
                      window.findChild<QLabel *>("projectName")->parentWidget(),
              "back arrow shares the project-name row and old open-folder button is removed");
        window.findChild<QAction *>("actionLightTheme")->trigger();
        newAction->trigger();
        check(tabs->count() == 1, "new document can be created inside project");
        auto *firstEditor = activeEditor();
        auto *extensions = window.findChild<QToolButton *>("extensionsButton");
        auto *list = window.findChild<QListWidget *>("extensionList");
        auto *search = window.findChild<QLineEdit *>("extensionSearch");
        check(extensions && list && search, "extension marketplace exists");
        extensions->click();
        auto *cpp = list->item(0);
        for (const QString &language : {"C/C++", "Java", "Python"}) {
            const auto matches = list->findItems(language, Qt::MatchContains);
            check(matches.size() == 1 && matches.first()->text() == language &&
                      !matches.first()->icon().pixmap(22, 22).isNull(),
                  "catalog shows the language name and icon");
            search->setText(language.toLower());
            check(!matches.first()->isHidden(), "language toolkit matches search");
            list->itemClicked(matches.first());
            check(tabs->tabText(tabs->currentIndex()) == matches.first()->text(),
                  "language opens its own details");
            check(tabs->currentWidget()->findChild<QWidget *>("toolchainConfiguration") &&
                      !tabs->currentWidget()->findChild<QDialog *>(),
                  "language configuration is embedded in its detail page");
            if (language == "Java" && qEnvironmentVariableIsSet("BASIS_CONFIG_PREVIEW_PATH")) {
                window.findChild<QAction *>("actionGitHubDarkTheme")->trigger();
                app.processEvents();
                check(window.grab().save(qEnvironmentVariable("BASIS_CONFIG_PREVIEW_PATH")), "render inline runtime configuration");
                window.findChild<QAction *>("actionLightTheme")->trigger();
            }
            closeAction->trigger();
        }
        search->setText("Python");
        check(cpp->isHidden(), "search hides unrelated packages");
        search->setText("c++");
        check(!cpp->isHidden(), "case insensitive search restores package");
        list->itemClicked(cpp);
        check(tabs->count() == 2 && tabs->tabText(1) == cpp->text() &&
                  !activeEditor() && list->isVisible() && !saveAction->isEnabled(),
              "details open beside code while catalog stays visible");
        check(window.findChild<QSplitter *>("codeOutputSplitter")->parentWidget()->objectName() == "mainArea" &&
                  window.findChild<QWidget *>("runOutputDock")->parentWidget() == window.findChild<QSplitter *>("codeOutputSplitter"),
              "run output belongs to the right code area");
        if (qEnvironmentVariableIsSet("BASIS_PREVIEW_PATH"))
            check(window.grab().save(qEnvironmentVariable("BASIS_PREVIEW_PATH")),
                  "render toolkit page");
        tabs->setCurrentWidget(firstEditor);
        list->itemActivated(cpp);
        check(tabs->count() == 2 && tabs->currentIndex() == 1,
              "reopening focuses existing toolkit tab");
        closeAction->trigger();
        check(tabs->count() == 1 && activeEditor() == firstEditor,
              "closing toolkit returns to editor");
        check(window.openProject(temporary.path()), "open project directory");
        check(tree->isVisible() && !extensions->isChecked(),
              "opening a project switches back to explorer");
        check(model->filePath(tree->rootIndex()) == temporary.path(),
              "file tree rooted at chosen project");
        check(!window.openProject(temporary.filePath("missing")),
              "invalid folder leaves current project intact");
        check(firstEditor->lineWrapMode() == QPlainTextEdit::NoWrap,
              "code editor starts without line wrapping");
        check(firstEditor->font().pointSize() == 12 &&
                  QFontMetricsF(firstEditor->font()).horizontalAdvance('i') == QFontMetricsF(firstEditor->font()).horizontalAdvance('W'),
              "editor restores the original monospace font");
        auto *zoomIn = window.findChild<QAction *>("actionZoomIn");
        auto *zoomOut = window.findChild<QAction *>("actionZoomOut");
        zoomIn->trigger();
        check(firstEditor->font().pointSize() == 13 && window.font().pointSize() == 11,
              "zoom increases editor and application font size");
        zoomOut->trigger();
        check(firstEditor->font().pointSize() == 12 && window.font().pointSize() == 10,
              "zoom decreases editor and application font size");
        const QString text =
            QString::fromUtf8("中文文件测试 😀\nsecond line\n");
        const QString first =
            temporary.filePath(QString::fromUtf8("中文文件.txt"));
        firstEditor->insertPlainText(text);
        answerMessage(QMessageBox::Cancel);
        closeWindow->click();
        check(window.isVisible() && firstEditor->toPlainText() == text,
              "title bar close button respects unsaved cancellation");
        check(window.isWindowModified(), "typing marks active window modified");
        check(window.saveFile(first), "save Unicode text and path");
        check(readText(first) == text.toUtf8(), "Unicode round trip");
        check(!firstEditor->document()->isModified(),
              "save resets dirty state");
        firstEditor->insertPlainText("extra");
        saveAction->trigger();
        const QString savedText = firstEditor->toPlainText();
        check(readText(first) == savedText.toUtf8(),
              "Save updates current file");

        firstEditor->insertPlainText("first unsaved");
        const QString firstDirty = firstEditor->toPlainText();
        newAction->trigger();
        auto *secondEditor = activeEditor();
        check(tabs->count() == 2 && secondEditor != firstEditor,
              "New creates an independent tab");
        check(firstEditor->toPlainText() == firstDirty &&
                  firstEditor->document()->isModified(),
              "New preserves other dirty document");
        check(!window.isWindowModified() && window.currentFilePath().isEmpty(),
              "new tab starts clean and pathless");
        const QString second = temporary.filePath("second.txt");
        secondEditor->insertPlainText("second document");
        QTimer::singleShot(0, [second] {
            auto *dialog =
                qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                std::exit(2);
            dialog->selectFile(second);
            QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
        });
        saveAsAction->trigger();
        check(window.currentFilePath() == second,
              "Save As associates active tab only");
        check(readText(second) == "second document",
              "Save As writes active content");
        check(firstEditor->document()->isModified(),
              "saving second does not clear first dirty state");
        tabs->setCurrentWidget(firstEditor);
        check(window.currentFilePath() == first && window.isWindowModified(),
              "switching tabs keeps paths and dirty markers independent");
        tabs->setCurrentWidget(secondEditor);
        check(!window.isWindowModified(), "clean tab has no dirty marker");
        const QModelIndex firstIndex = model->index(first);
        check(firstIndex.isValid(), "project model exposes saved file");
        QMetaObject::invokeMethod(tree, "doubleClicked",
                                  Q_ARG(QModelIndex, firstIndex));
        check(activeEditor() == firstEditor && tabs->count() == 2,
              "double clicking a tree file focuses its open tab");
        check(model->filePath(tree->currentIndex()) == first,
              "tree selection follows active file");
        check(window.loadFile(first) && activeEditor() == firstEditor &&
                  tabs->count() == 2,
              "opening an existing file focuses its tab without duplicate");
        check(firstEditor->toPlainText() == firstDirty,
              "reopening does not discard unsaved content");
        const QString third = temporary.filePath("third.txt");
        writeText(third, "third document");
        check(window.loadFile(third) && tabs->count() == 3,
              "opening new file creates another tab");
        auto *thirdEditor = activeEditor();
        check(thirdEditor->toPlainText() == "third document",
              "newly opened contents displayed");
        check(firstEditor->document()->isModified(),
              "opening another file preserves unsaved work");
        tabs->tabBar()->moveTab(2, 0);
        check(tabs->widget(0) == thirdEditor && activeEditor() == thirdEditor,
              "tab reorder preserves current editor");
        check(model->filePath(tree->currentIndex()) == third,
              "project tree tracks active file after tab reorder");
        wrapAction->setChecked(true);
        tabs->setCurrentWidget(secondEditor);
        check(!wrapAction->isChecked(), "wrap setting follows each file");
        tabs->setCurrentWidget(thirdEditor);
        check(wrapAction->isChecked(), "wrap setting retained in original tab");
        answerMessage(QMessageBox::Ok);
        check(!window.saveFile(first),
              "Save As cannot overwrite a different open document");
        check(window.currentFilePath() == third &&
                  readText(first) == savedText.toUtf8(),
              "conflicting save preserves paths and file data");
        answerMessage(QMessageBox::Ok);
        check(!window.loadFile(temporary.filePath("missing.txt")),
              "failed open is reported");
        check(tabs->count() == 3 && activeEditor() == thirdEditor &&
                  thirdEditor->toPlainText() == "third document",
              "failed open leaves workspace intact");
        thirdEditor->insertPlainText("edit");
        const QString thirdDirty = thirdEditor->toPlainText();
        check(undoAction->isEnabled(), "undo enabled for active edit");
        undoAction->trigger();
        check(thirdEditor->toPlainText() == "third document" &&
                  redoAction->isEnabled(),
              "undo affects active document");
        redoAction->trigger();
        check(thirdEditor->toPlainText() == thirdDirty, "redo restores edit");
        tabs->setCurrentWidget(secondEditor);
        undoAction->trigger();
        check(secondEditor->toPlainText().isEmpty() &&
                  thirdEditor->toPlainText() == thirdDirty,
              "undo history is independent in each tab");
        redoAction->trigger();
        check(secondEditor->toPlainText() == "second document",
              "second tab redo restores its own content");
        tabs->setCurrentWidget(thirdEditor);
        answerMessage(QMessageBox::Cancel);
        closeAction->trigger();
        check(tabs->count() == 3 && thirdEditor->toPlainText() == thirdDirty,
              "Cancel tab close preserves dirty content");
        cancelFileDialog();
        saveAsAction->trigger();
        check(window.currentFilePath() == third &&
                  thirdEditor->document()->isModified(),
              "Cancel Save As preserves active document");
        answerMessage(QMessageBox::Save);
        closeAction->trigger();
        check(tabs->count() == 2 && readText(third) == thirdDirty.toUtf8(),
              "Save before closing writes correct document");
        tabs->setCurrentWidget(firstEditor);
        QCloseEvent cancelClose;
        answerMessage(QMessageBox::Cancel);
        QApplication::sendEvent(&window, &cancelClose);
        check(!cancelClose.isAccepted() && tabs->count() == 2,
              "Cancel exit preserves all tabs");
        check(firstEditor->toPlainText() == firstDirty,
              "Cancel exit preserves modified text");
        answerMessage(QMessageBox::Ok);
        check(!window.saveFile(temporary.filePath("missing/failed.txt")),
              "failed save is reported");
        check(firstEditor->document()->isModified() &&
                  window.currentFilePath() == first,
              "failed save preserves dirty state and path");
        answerMessage(QMessageBox::Discard);
        closeAction->trigger();
        check(tabs->count() == 1 && activeEditor() == secondEditor,
              "Discard closes only selected document");
        check(readText(first) == savedText.toUtf8(),
              "Discard does not save discarded changes");
        closeAction->trigger();
        check(tabs->count() == 0 && tabs->isHidden() && !activeEditor() &&
                  !saveAction->isEnabled(),
              "closing last tab leaves an empty workspace");
        check(window.loadFile(first) && !tabs->isHidden(),
              "opening a file restores the tab area");
        closeAction->trigger();
        newAction->trigger();
        check(tabs->count() == 1 && saveAction->isEnabled(),
              "new document restores editing actions");
        activeEditor()->insertPlainText("unsaved initial document");
        QTimer::singleShot(0, [] {
            auto *box =
                qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box)
                std::exit(2);
            cancelFileDialog();
            box->button(QMessageBox::Save)->click();
        });
        closeAction->trigger();
        check(tabs->count() == 1 && activeEditor()->document()->isModified(),
              "Cancel first-save dialog aborts tab closure");
        newAction->trigger();
        activeEditor()->insertPlainText("another unsaved document");
        int prompts = 0;
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, [&prompts] {
            if (auto *box = qobject_cast<QMessageBox *>(
                    QApplication::activeModalWidget())) {
                ++prompts;
                box->button(QMessageBox::Discard)->click();
            }
        });
        timer.start(0);
        QCloseEvent discardClose;
        QApplication::sendEvent(&window, &discardClose);
        timer.stop();
        check(discardClose.isAccepted() && prompts == 2,
              "exit checks every dirty document");

        {
            Notepad renameWindow;
            renameWindow.show();
            const QString before = temporary.filePath("rename-before");
            const QString after = temporary.filePath("rename-after");
            check(QDir().mkpath(before), "rename fixture");
            writeText(before + "/main.cpp", "int main() {}");
            check(renameWindow.openProject(before) && renameWindow.loadFile(before + "/main.cpp"), "open rename fixture");
            auto *renameTabs = renameWindow.findChild<QTabWidget *>("documentTabs");
            auto *renameEditor = qobject_cast<QPlainTextEdit *>(renameTabs->currentWidget());
            renameEditor->insertPlainText("// pending\\n");
            QTimer::singleShot(0, [] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu) std::exit(2);
                QAction *rename = nullptr;
                for (auto *action : menu->actions())
                    if (action->text() == QString::fromUtf8("修改文件夹名称…")) rename = action;
                if (!rename) std::exit(2);
                menu->close();
                QTimer::singleShot(0, [] {
                    auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                    if (!dialog) std::exit(2);
                    dialog->setTextValue("rename-after");
                    dialog->accept();
                });
                rename->trigger();
            });
            auto *projectName = renameWindow.findChild<QLabel *>("projectName");
            projectName->customContextMenuRequested(QPoint(1, 1));
            check(QFileInfo::exists(after + "/main.cpp") && !QFileInfo::exists(before) &&
                      renameWindow.currentFilePath() == after + "/main.cpp" &&
                      renameEditor->document()->isModified(), "folder rename preserves unsaved document and updates its path");
            check(renameWindow.saveFile(after + "/main.cpp"), "save document after folder rename");
        }
        const QString demoRoot = temporary.filePath("tiny-editor");
        check(QDir().mkpath(demoRoot + "/src") &&
                  QDir().mkpath(demoRoot + "/include"),
              "create nested sample project");
        const QByteArray code =
            "#include <iostream>\n#include <string>\n\nstruct Project {\n    "
            "std::string name;\n    int fileCount = 0;\n};\n\nint main() {\n   "
            " const Project project{\"tiny-editor\", 3};\n\n    std::cout << "
            "\"Hello, \" << project.name << '\\n';\n    std::cout << \"Files: "
            "\" << project.fileCount << '\\n';\n\n    return 0;\n}\n";
        writeText(demoRoot + "/src/main.cpp", code);
        writeText(demoRoot + "/include/project.h",
                  "#pragma once\n#include <string>\n\nstruct Project {\n    "
                  "std::string name;\n    int fileCount = 0;\n};\n");
        writeText(demoRoot + "/CMakeLists.txt",
                  "cmake_minimum_required(VERSION 3.16)\nproject(tiny-editor "
                  "LANGUAGES CXX)\n");
        writeText(demoRoot + "/README.md",
                  "# tiny-editor\n\nA minimal C++ project.\n");
        Notepad preview;
        preview.show();
        check(preview.openProject(demoRoot), "open sample project");
        check(preview.loadFile(demoRoot + "/src/main.cpp"),
              "open nested source file");
        check(preview.loadFile(demoRoot + "/include/project.h"),
              "open second source tab");
        auto *previewTabs = preview.findChild<QTabWidget *>("documentTabs");
        previewTabs->setCurrentIndex(1);
        auto *previewModel =
            preview.findChild<QFileSystemModel *>("projectModel");
        QElapsedTimer loaded;
        loaded.start();
        while (loaded.elapsed() < 2000 &&
               previewModel->rowCount(previewModel->index(demoRoot)) < 4) {
            app.processEvents();
            QThread::msleep(5);
        }
        check(previewModel->rowCount(previewModel->index(demoRoot)) == 4,
              "file tree asynchronously loads project children");
        auto *previewTree = preview.findChild<QTreeView *>("projectTree");
        previewTree->expand(previewModel->index(demoRoot + "/src"));
        previewTree->expand(previewModel->index(demoRoot + "/include"));
        app.processEvents();
        auto *runPanel = preview.findChild<QWidget *>("runOutputDock");
        runPanel->show();
        app.processEvents();
        check(runPanel->mapTo(&preview, QPoint()).x() == previewTabs->mapTo(&preview, QPoint()).x() &&
                  runPanel->width() == previewTabs->width() &&
                  runPanel->mapTo(&preview, QPoint()).y() >= previewTabs->mapTo(&preview, QPoint()).y() + previewTabs->height(),
              "run panel is aligned underneath code tabs");
        check(preview.grab().save("notepad-preview.png"),
              "render code editor preview");
        if (qEnvironmentVariableIsSet("BASIS_THEME_PREVIEW_PREFIX")) {
            const QString prefix = qEnvironmentVariable("BASIS_THEME_PREVIEW_PREFIX");
            preview.findChild<QAction *>("actionGitHubDarkTheme")->trigger();
            app.processEvents();
            check(preview.grab().save(prefix + "-dark.png"), "render dark preview");
            preview.findChild<QAction *>("actionLightTheme")->trigger();
            app.processEvents();
            check(preview.grab().save(prefix + "-light.png"), "render light preview");
        }
        {
            Notepad pluginWindow;
            check(pluginWindow.openProject(temporary.path()), "open plugin test project");
            const QString a = temporary.filePath("format-a.json"), b = temporary.filePath("format-b.json");
            writeText(a, "{\"a\":1}"); writeText(b, "{\"b\":2}");
            check(pluginWindow.loadFile(a), "open first JSON source");
            auto *pluginTabs = pluginWindow.findChild<QTabWidget *>("documentTabs");
            auto *first = qobject_cast<QPlainTextEdit *>(pluginTabs->currentWidget());
            auto *catalog = pluginWindow.findChild<QListWidget *>("extensionList");
            auto *json = catalog->findItems("JSON Tools", Qt::MatchExactly).first();
            catalog->itemClicked(json);
            QWidget *firstPlugin = pluginTabs->currentWidget();
            check(pluginWindow.loadFile(b), "open second JSON source");
            auto *second = qobject_cast<QPlainTextEdit *>(pluginTabs->currentWidget());
            catalog->itemClicked(json);
            check(pluginTabs->currentWidget() != firstPlugin, "format extensions bind independently to documents");
            pluginTabs->currentWidget()->findChild<QPushButton *>("applyDocumentFormat")->click();
            check(second->toPlainText().contains("    \"b\": 2") && first->toPlainText() == "{\"a\":1}", "formatter updates selected source only");
            second->undo();
        }
        if (app.arguments().contains("--benchmark")) {
            const QString large = temporary.filePath("large-benchmark.cpp");
            const QByteArray lines = QByteArray("int value = 123; // benchmark line\n").repeated(10000);
            writeText(large, lines);
            Notepad bench;
            QElapsedTimer elapsed; elapsed.start();
            check(bench.loadFile(large), "load performance fixture");
            app.processEvents();
            const qint64 load = elapsed.elapsed();
            auto *code = qobject_cast<QPlainTextEdit *>(bench.findChild<QTabWidget *>("documentTabs")->currentWidget());
            elapsed.restart();
            for (int i = 0; i < 1000; ++i) {
                auto cursor = code->textCursor(); cursor.setPosition((i * 307) % lines.size()); code->setTextCursor(cursor);
            }
            std::cout << "BENCHMARK bytes=" << lines.size() << " lines=10000 load_ms=" << load
                      << " cursor_moves=1000 cursor_ms=" << elapsed.elapsed() << '\n';
        }
        std::cout << "Passed " << checks << " checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
