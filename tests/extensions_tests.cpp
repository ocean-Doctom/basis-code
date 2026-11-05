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
#include <QCheckBox>
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
        const auto listed = tools.run("nested-list", "list_files", {{"path", "nested"}, {"depth", 1}});
        const auto listing = QJsonDocument::fromJson(listed.payload["content"].toString().toUtf8()).object();
        check(listing["count"].toInt() == 1, "listing depth is relative to requested subfolder");
#ifdef Q_OS_WIN
        QProcess junction;
        junction.start(qEnvironmentVariable("SystemRoot") + "/System32/cmd.exe",
                       {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(root.filePath("outside-link")), QDir::toNativeSeparators(outside.path())});
        const bool junctionDone = junction.waitForFinished(5000);
        if (!junctionDone || junction.exitCode() != 0)
            std::cerr << "Junction fixture: " << junction.readAllStandardOutput().constData() << junction.readAllStandardError().constData() << '\n';
        check(junctionDone && junction.exitCode() == 0, "create temporary junction fixture");
        QFile outsideSecret(outside.filePath("outside-only.txt"));
        check(outsideSecret.open(QIODevice::WriteOnly), "create outside junction fixture");
        outsideSecret.write("EXTERNAL_BOUNDARY_MARKER"); outsideSecret.close();
        check(tools.resolve("outside-link/outside-only.txt").isEmpty(), "junction cannot escape project root");
        const auto external = tools.run("junction-search", "search_files", {{"pattern", "EXTERNAL_BOUNDARY_MARKER"}, {"content", true}});
        const auto externalResults = QJsonDocument::fromJson(external.payload["content"].toString().toUtf8()).object();
        check(externalResults["count"].toInt() == 0, "recursive content search does not read junction targets outside project");
#endif
        tools.setWritePolicy(AgentTools::WritePolicy::Confirm);
        auto pending = tools.run("b", "write_file", {{"path", "blocked.txt"}, {"content", "no"}});
        check(pending.needsConfirm, "write requires consent");
        tools.setWorkRoot(outside.path());
        tools.resolvePending(pending.pending, "b", true);
        check(!QFileInfo::exists(outside.filePath("blocked.txt")), "changing root invalidates pending writes");
        tools.setCommandPolicy(AgentTools::CommandPolicy::Allowlist);
        auto denied = tools.run("c", "run_command", {{"command", "echo safe; echo unsafe"}});
        check(QJsonDocument::fromJson(denied.payload["content"].toString().toUtf8()).object().contains("error"), "reject shell composition");
        tools.setCommandPolicy(AgentTools::CommandPolicy::Any);
#ifdef Q_OS_WIN
        const QString nativeCommand = "Write-Output BASIS_AGENT_NATIVE_OK";
#else
        const QString nativeCommand = "echo BASIS_AGENT_NATIVE_OK";
#endif
        const auto commandResult = tools.run("native", "run_command", {{"command", nativeCommand}});
        const auto commandContent = QJsonDocument::fromJson(commandResult.payload["content"].toString().toUtf8()).object();
        check(commandContent["exitCode"].toInt(-1) == 0 && commandContent["stdout"].toString().contains("BASIS_AGENT_NATIVE_OK"), "native tool command runs and captures output");
#ifdef Q_OS_WIN
        const QString failingCommand = "cmd.exe /c exit 7";
#else
        const QString failingCommand = "exit 7";
#endif
        const auto failure = tools.run("native-failure", "run_command", {{"command", failingCommand}});
        const auto failureContent = QJsonDocument::fromJson(failure.payload["content"].toString().toUtf8()).object();
        check(failureContent["exitCode"].toInt() == 7, "tool preserves native failure exit code");
#ifdef Q_OS_WIN
        const auto eof = tools.run("eof", "run_command", {{"command", "[Console]::In.ReadToEnd(); Write-Output BASIS_EOF_OK"}});
        check(QJsonDocument::fromJson(eof.payload["content"].toString().toUtf8()).object()["stdout"].toString().contains("BASIS_EOF_OK"), "noninteractive tool input receives EOF");
        const auto largeOutput = tools.run("large-output", "run_command", {{"command", "Write-Output ('x' * 600000)"}});
        const auto capped = QJsonDocument::fromJson(largeOutput.payload["content"].toString().toUtf8()).object();
        check(capped["stdout"].toString().size() <= 512 * 1024 && capped["stdoutTruncated"].toBool(), "large command output is capped and marked truncated");
#endif
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
        editor.setPlainText("foo food Foo");
        auto *replacePage = createBuiltinExtension("Find and Replace", &editor, "replace.txt", root.path(), nullptr);
        replacePage->findChild<QLineEdit *>("documentFindText")->setText("foo");
        replacePage->findChild<QLineEdit *>("documentReplaceText")->setText("foo-new");
        replacePage->findChild<QCheckBox *>("findWholeWords")->setChecked(true);
        replacePage->findChild<QPushButton *>("replaceAllMatches")->click();
        check(editor.toPlainText() == "foo-new food foo-new", "replace all respects whole words and does not repeat inserted matches");
        editor.undo(); check(editor.toPlainText() == "foo food Foo", "all replacements form one undo group");
        replacePage->findChild<QPushButton *>("findNextMatch")->click();
        check(editor.textCursor().hasSelection(), "find selects a source match");
        check(replacePage->findChild<QPlainTextEdit *>("findMatchContext")->toPlainText().contains("food"), "find renders adjacent source context");
        delete replacePage;
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
        QFile large(root.filePath("large-unsearched.txt"));
        check(large.open(QIODevice::WriteOnly), "create oversized search fixture"); large.write(QByteArray(513 * 1024, 'a')); large.close();
        auto *searchPage = createProjectSearch(root.path(), "needle", [&](const QString &path, int line) {
            navigated = path == searchable.fileName() && line == 2;
        }, nullptr);
        auto *results = searchPage->findChild<QTreeWidget *>("projectSearchResults");
        timer.restart();
        while (results->topLevelItemCount() == 0 && timer.elapsed() < 4000) { app.processEvents(); QThread::msleep(5); }
        check(results->topLevelItemCount() == 1, "background search finds content and excludes dependency folder");
        check(searchPage->findChild<QLabel *>("projectSearchStatus")->text().contains(QString::fromUtf8("个文件因过大")), "search reports skipped oversized content");
        results->itemActivated(results->topLevelItem(0), 0);
        check(navigated, "search result navigates to matching line");
        delete searchPage;
        std::cout << "Passed native extension and agent boundary checks\n"; return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
