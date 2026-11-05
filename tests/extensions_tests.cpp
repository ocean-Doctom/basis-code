#include "builtinextensions.h"
#include "agenttools.h"
#include "projectsearch.h"
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
#include <QProcess>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QLineEdit>
#include <QLabel>
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
        if (!QStandardPaths::findExecutable("git").isEmpty()) {
            auto git = [&](const QStringList &args) {
                QProcess process; process.setWorkingDirectory(root.path());
                process.start("git", args);
                check(process.waitForFinished(5000) && process.exitCode() == 0, "Git fixture command succeeds");
            };
            git({"init"});
            QFile fixture(root.filePath("tracked.txt"));
            check(fixture.open(QIODevice::WriteOnly), "create Git fixture"); fixture.write("original\n"); fixture.close();
            git({"add", "tracked.txt"});
            git({"-c", "user.name=BasisCodeTest", "-c", "user.email=test@example.invalid", "-c", "commit.gpgsign=false", "-c", "core.hooksPath=", "commit", "-m", "test fixture"});
            check(fixture.open(QIODevice::WriteOnly | QIODevice::Truncate), "modify Git fixture"); fixture.write("updated\n"); fixture.close();
            auto *changes = createBuiltinExtension("Git Changes", nullptr, fixture.fileName(), root.path(), nullptr);
            auto *refresh = changes->findChild<QPushButton *>("refreshGitChanges");
            timer.restart();
            while (!refresh->isEnabled() && timer.elapsed() < 12000) { app.processEvents(); QThread::msleep(5); }
            auto text = changes->findChild<QPlainTextEdit *>("gitChangesOutput")->toPlainText();
            check(refresh->isEnabled() && text.contains("-original") && text.contains("+updated"), "Git extension shows real working tree diff");
            delete changes;
        }
        QDir().mkpath(root.filePath("node_modules"));
        QFile hidden(root.filePath("node_modules/excluded.txt"));
        check(hidden.open(QIODevice::WriteOnly), "create excluded fixture"); hidden.write("needle"); hidden.close();
        QFile searchable(root.filePath("searchable.txt"));
        check(searchable.open(QIODevice::WriteOnly), "create search fixture"); searchable.write("first line\nneedle here\n"); searchable.close();
        bool navigated = false;
        auto *searchPage = createProjectSearch(root.path(), "needle", [&](const QString &path, int line) {
            navigated = path == searchable.fileName() && line == 2;
        }, nullptr);
        auto *results = searchPage->findChild<QTreeWidget *>("projectSearchResults");
        timer.restart();
        while (results->topLevelItemCount() == 0 && timer.elapsed() < 4000) { app.processEvents(); QThread::msleep(5); }
        check(results->topLevelItemCount() == 1, "background search finds content and excludes dependency folder");
        results->itemActivated(results->topLevelItem(0), 0);
        check(navigated, "search result navigates to matching line");
        delete searchPage;
        std::cout << "Passed native extension and agent boundary checks\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
