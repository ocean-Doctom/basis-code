#include "builtinextensions.h"
#include "agenttools.h"
#include <QApplication>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QPushButton>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
#include <stdexcept>
static void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir root, outside;
        check(root.isValid() && outside.isValid(), "temporary roots");
        AgentTools tools; tools.setWorkRoot(root.path());
        check(tools.resolve("..").isEmpty(), "reject exact parent traversal");
        check(tools.resolve("../escape.txt").isEmpty(), "reject traversal write");
        check(tools.resolve(root.path()) == root.path(), "accept absolute project root");
        check(tools.resolve(outside.filePath("escape.txt")).isEmpty(), "reject outside absolute path");
        tools.setWritePolicy(AgentTools::WritePolicy::Direct);
        auto written = tools.run("a", "write_file", {{"path", "nested/test.txt"}, {"content", "hello"}});
        check(!written.needsConfirm && QFileInfo::exists(root.filePath("nested/test.txt")), "write inside project");
        tools.setWritePolicy(AgentTools::WritePolicy::Confirm);
        auto pending = tools.run("b", "write_file", {{"path", "blocked.txt"}, {"content", "no"}});
        check(pending.needsConfirm, "write requires consent");
        tools.setWorkRoot(outside.path());
        tools.resolvePending(pending.pending, "b", true);
        check(!QFileInfo::exists(outside.filePath("blocked.txt")), "changing root invalidates pending writes");
        tools.setCommandPolicy(AgentTools::CommandPolicy::Allowlist);
        auto denied = tools.run("c", "run_command", {{"command", "echo safe; echo unsafe"}});
        check(QJsonDocument::fromJson(denied.payload["content"].toString().toUtf8()).object().contains("error"), "reject shell composition");
        QPlainTextEdit editor;
        editor.setPlainText("{\"answer\":42}");
        auto *json = createBuiltinExtension("JSON Tools", &editor, "test.json", root.path(), nullptr);
        json->findChild<QPushButton *>("applyDocumentFormat")->click();
        check(editor.toPlainText().contains("    \"answer\": 42"), "JSON formats actual source document");
        editor.undo(); check(editor.toPlainText() == "{\"answer\":42}", "formatting is one undo step");
        editor.setPlainText("invalid JSON");
        json->findChild<QPushButton *>("applyDocumentFormat")->click();
        check(editor.toPlainText() == "invalid JSON", "invalid JSON preserves source");
        delete json;
        editor.setPlainText("hello  \n\n");
        auto *clean = createBuiltinExtension("Whitespace Cleaner", &editor, "test.txt", root.path(), nullptr);
        clean->findChild<QPushButton *>("applyDocumentFormat")->click();
        check(editor.toPlainText() == "hello\n", "whitespace cleanup applies"); delete clean;
        editor.setPlainText("# Title");
        auto *markdown = createBuiltinExtension("Markdown Preview", &editor, "test.md", root.path(), nullptr);
        auto *preview = markdown->findChild<QTextBrowser *>("markdownPreview");
        check(preview->toPlainText().contains("Title"), "Markdown renders");
        editor.setPlainText("# Updated");
        QElapsedTimer timer; timer.start();
        while (!preview->toPlainText().contains("Updated") && timer.elapsed() < 1500) { app.processEvents(); QThread::msleep(5); }
        check(preview->toPlainText().contains("Updated"), "Markdown refreshes after edit"); delete markdown;
        auto *missing = new QPlainTextEdit;
        auto *closed = createBuiltinExtension("JSON Tools", missing, "gone.json", root.path(), nullptr);
        delete missing; closed->findChild<QPushButton *>("applyDocumentFormat")->click();
        check(!closed->findChild<QPushButton *>("applyDocumentFormat")->isEnabled(), "closed source handled safely"); delete closed;
        std::cout << "Passed native extension and agent boundary checks\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
