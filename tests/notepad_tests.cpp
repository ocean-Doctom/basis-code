#include "notepad.h"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileSystemModel>
#include <QFontMetricsF>
#include <QThread>
#include <QTreeView>

#include <QMessageBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
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
    QTimer::singleShot(20000, [] {
        std::cerr << "Timeout\n";
        std::exit(2);
    });
    try {
        QTemporaryDir temporary;
        check(temporary.isValid(), "temporary directory");
        Notepad window;
        window.show();
        app.processEvents();
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
        check(window.isMaximized(), "maximize button maximizes window");
        maximize->click();
        app.processEvents();
        check(!window.isMaximized(), "maximize button restores window");
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
        check(tabs->count() == 1, "initial single document");
        auto *firstEditor = activeEditor();
        check(window.openProject(temporary.path()), "open project directory");
        check(model->filePath(tree->rootIndex()) == temporary.path(),
              "file tree rooted at chosen project");
        check(!window.openProject(temporary.filePath("missing")),
              "invalid folder leaves current project intact");
        check(firstEditor->lineWrapMode() == QPlainTextEdit::NoWrap,
              "code editor starts without line wrapping");
        const QFontMetricsF metrics(firstEditor->font());
        check(qAbs(metrics.horizontalAdvance('i') -
                   metrics.horizontalAdvance('W')) < 0.1,
              "code font has equal-width Latin characters");
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
        previewTabs->setCurrentIndex(0);
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
        check(preview.grab().save("notepad-preview.png"),
              "render code editor preview");
        std::cout << "Passed " << checks << " checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
