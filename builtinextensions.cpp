#include "builtinextensions.h"
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTimer>
#include <QProcess>
#include <QFileInfo>
#include <QTextCursor>
#include <QScrollBar>
#include <QUrl>
#include <QDesktopServices>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

QString formatDocumentText(const QString &text, bool json, QString *error) {
    error->clear();
    if (json) {
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(text.toUtf8(), &parse);
        if (parse.error != QJsonParseError::NoError) {
            *error = QObject::tr("JSON 错误（UTF-8 字节 %1）：%2").arg(parse.offset).arg(parse.errorString());
            return text;
        }
        return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
    }
    QStringList lines = text.split('\n');
    for (auto &line : lines) {
        while (line.endsWith(' ') || line.endsWith('\t') || line.endsWith('\r')) line.chop(1);
    }
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    return lines.isEmpty() ? QString() : lines.join('\n') + '\n';
}

QWidget *createBuiltinExtension(const QString &name, QPlainTextEdit *source,
                               const QString &path, const QString &project, QWidget *parent) {
    auto *page = new QWidget(parent);
    page->setProperty("toolkitName", name);
    page->setObjectName("builtinExtensionPage");
    QPointer<QPlainTextEdit> editor(source);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    auto *heading = new QLabel(name, page);
    heading->setObjectName("toolkitTitle"); layout->addWidget(heading);
    auto *description = new QLabel(path.isEmpty() ? QObject::tr("请先打开需要处理的文档，再打开此扩展。") : path, page);
    description->setWordWrap(true); layout->addWidget(description);
    auto *status = new QLabel(page); status->setWordWrap(true);
    status->setObjectName("extensionStatus"); layout->addWidget(status);
    if (name == "Markdown Preview") {
        auto *preview = new QTextBrowser(page);
        preview->setObjectName("markdownPreview");
        preview->setOpenLinks(false);
        QObject::connect(preview, &QTextBrowser::anchorClicked, page, [](const QUrl &url) {
            if (url.scheme() == "https" || url.scheme() == "http") QDesktopServices::openUrl(url);
        });
        layout->addWidget(preview, 1);
        if (editor) {
            preview->document()->setBaseUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath() + '/'));
            auto *delay = new QTimer(page); delay->setSingleShot(true); delay->setInterval(150);
            auto render = [editor, preview, status] {
                if (!editor) { status->setText(QObject::tr("源文档已关闭。")); return; }
                if (editor->document()->characterCount() > 1000000) {
                    status->setText(QObject::tr("文档超过 100 万字符，已暂停自动预览。")); return;
                }
                const int scroll = preview->verticalScrollBar()->value();
                preview->setMarkdown(editor->toPlainText());
                preview->verticalScrollBar()->setValue(scroll);
                status->setText(QObject::tr("实时预览 · 修改后 150 ms 更新"));
            };
            QObject::connect(editor, &QPlainTextEdit::textChanged, delay, [delay] { delay->start(); });
            QObject::connect(delay, &QTimer::timeout, page, render); render();
        }
    } else if (name == "Git Changes") {
        description->setText(QObject::tr("查看已保存文件的 Git 状态和差异；新文件显示在状态列表中。"));
        auto *refresh = new QPushButton(QObject::tr("刷新 Git 差异"), page);
        refresh->setObjectName("refreshGitChanges"); layout->addWidget(refresh);
        auto *output = new QPlainTextEdit(page); output->setReadOnly(true);
        output->setMaximumBlockCount(10000); output->setObjectName("gitChangesOutput"); layout->addWidget(output, 1);
        auto *process = new QProcess(page);
#ifdef Q_OS_WIN
        process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
#endif
        auto *timeout = new QTimer(page); timeout->setSingleShot(true); timeout->setInterval(15000);
        QObject::connect(timeout, &QTimer::timeout, process, [process, status] { status->setText(QObject::tr("Git 命令超时。")); process->kill(); });
        process->setWorkingDirectory(project.isEmpty() ? QFileInfo(path).absolutePath() : project);
        QObject::connect(refresh, &QPushButton::clicked, page, [=] {
            refresh->setEnabled(false); output->clear(); status->setText(QObject::tr("正在读取 Git…"));
            process->setProperty("stage", 0);
            process->start("git", {"--no-optional-locks", "status", "--short"}); timeout->start();
        });
        QObject::connect(process, &QProcess::readyReadStandardOutput, output, [=] { output->insertPlainText(QString::fromUtf8(process->readAllStandardOutput())); });
        QObject::connect(process, &QProcess::readyReadStandardError, output, [=] { output->insertPlainText(QString::fromUtf8(process->readAllStandardError())); });
        QObject::connect(process, &QProcess::errorOccurred, page, [=](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) { timeout->stop(); refresh->setEnabled(true); status->setText(QObject::tr("无法启动 Git，请先安装 Git 并加入 PATH。")); }
        });
        QObject::connect(process, &QProcess::finished, page, [=](int code, QProcess::ExitStatus exit) {
            timeout->stop();
            if (code == 0 && exit == QProcess::NormalExit && process->property("stage").toInt() == 0) {
                output->insertPlainText("\n--- Working tree diff (HEAD) ---\n"); process->setProperty("stage", 1);
                process->start("git", {"--no-pager", "diff", "--no-ext-diff", "--no-textconv", "--no-color", "HEAD", "--"}); timeout->start(); return;
            }
            refresh->setEnabled(true);
            status->setText(code == 0 ? QObject::tr("差异读取完成。") : QObject::tr("Git 返回错误，请查看输出（新仓库可能尚无首次提交）。"));
        });
        QObject::connect(page, &QObject::destroyed, process, [process] {
            if (process->state() != QProcess::NotRunning) { process->kill(); process->waitForFinished(1000); }
        });
        refresh->click();
    } else {
        const bool json = name == "JSON Tools";
        description->setText(json ? QObject::tr("校验并以四空格缩进格式化 JSON；错误时保留原文。") : QObject::tr("清理行尾空格、空白尾行，统一末尾换行。修改可通过编辑器撤销。"));
        auto *apply = new QPushButton(json ? QObject::tr("校验并格式化 JSON") : QObject::tr("整理文档空白"), page);
        apply->setObjectName("applyDocumentFormat"); apply->setEnabled(bool(editor)); layout->addWidget(apply);
        auto *preview = new QPlainTextEdit(page); preview->setReadOnly(true);
        preview->setObjectName("formatResult"); layout->addWidget(preview, 1);
        QObject::connect(apply, &QPushButton::clicked, page, [=] {
            if (!editor) { apply->setEnabled(false); status->setText(QObject::tr("源文档已关闭。")); return; }
            QString error; const QString original = editor->toPlainText();
            const QString result = formatDocumentText(original, json, &error);
            if (!error.isEmpty()) { status->setText(error); return; }
            preview->setPlainText(result);
            if (original != result) {
                auto cursor = editor->textCursor(); const int position = cursor.position();
                cursor.beginEditBlock(); cursor.select(QTextCursor::Document); cursor.insertText(result); cursor.endEditBlock();
                cursor.setPosition(qMin(position, int(result.size()))); editor->setTextCursor(cursor);
            }
            status->setText(QObject::tr("已应用到源文档。请切回文档查看或撤销；保存仍由你决定。"));
        });
    }
    return page;
}
