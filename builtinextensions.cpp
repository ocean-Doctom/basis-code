#include "builtinextensions.h"
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
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
#include <QStringConverter>
#include <memory>
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
    if (name == "Find and Replace") {
        description->setText(QObject::tr("在绑定的源文档中进行字面量查找和替换；替换可撤销，不会自动保存。"));
        auto *query = new QLineEdit(page); query->setObjectName("documentFindText"); query->setPlaceholderText(QObject::tr("查找内容"));
        auto *replacement = new QLineEdit(page); replacement->setObjectName("documentReplaceText"); replacement->setPlaceholderText(QObject::tr("替换为"));
        layout->addWidget(query); layout->addWidget(replacement);
        auto *sensitive = new QCheckBox(QObject::tr("区分大小写"), page);
        auto *whole = new QCheckBox(QObject::tr("匹配整个单词"), page); whole->setObjectName("findWholeWords");
        layout->addWidget(sensitive); layout->addWidget(whole);
        auto flags = [=] {
            QTextDocument::FindFlags options;
            if (sensitive->isChecked()) options |= QTextDocument::FindCaseSensitively;
            if (whole->isChecked()) options |= QTextDocument::FindWholeWords;
            return options;
        };
        auto *next = new QPushButton(QObject::tr("查找下一个（循环）"), page); next->setObjectName("findNextMatch");
        auto *replace = new QPushButton(QObject::tr("替换当前匹配"), page); replace->setObjectName("replaceCurrentMatch");
        auto *all = new QPushButton(QObject::tr("全部替换"), page); all->setObjectName("replaceAllMatches");
        layout->addWidget(next); layout->addWidget(replace); layout->addWidget(all); layout->addStretch();
        auto find = [=] {
            if (!editor || query->text().isEmpty()) { status->setText(QObject::tr("请输入查找内容，并保持源文档打开。")); return; }
            auto result = editor->document()->find(query->text(), editor->textCursor(), flags());
            if (result.isNull()) result = editor->document()->find(query->text(), 0, flags());
            if (result.isNull()) status->setText(QObject::tr("未找到匹配。"));
            else { editor->setTextCursor(result); editor->centerCursor(); status->setText(QObject::tr("匹配位于第 %1 行。切回源文档可查看选中内容。").arg(result.blockNumber() + 1)); }
        };
        QObject::connect(next, &QPushButton::clicked, page, find);
        QObject::connect(query, &QLineEdit::returnPressed, page, find);
        QObject::connect(replace, &QPushButton::clicked, page, [=] {
            if (!editor || query->text().isEmpty()) return;
            auto cursor = editor->textCursor();
            const auto matched = editor->document()->find(query->text(), cursor.selectionStart(), flags());
            if (!cursor.hasSelection() || matched.isNull() || matched.selectionStart() != cursor.selectionStart() || matched.selectionEnd() != cursor.selectionEnd()) { find(); return; }
            cursor.insertText(replacement->text()); editor->setTextCursor(cursor); find();
        });
        QObject::connect(all, &QPushButton::clicked, page, [=] {
            if (!editor || query->text().isEmpty()) return;
            if (editor->document()->characterCount() > 2000000) { status->setText(QObject::tr("文档超过 200 万字符，请缩小文档后使用全部替换。")); return; }
            QList<QPair<int, int>> matches;
            QTextCursor cursor(editor->document());
            while (true) {
                cursor = editor->document()->find(query->text(), cursor, flags());
                if (cursor.isNull()) break;
                matches.append({cursor.selectionStart(), cursor.selectionEnd()});
                if (matches.size() > 10000) { status->setText(QObject::tr("匹配超过 10000 项，未修改文档。请缩小查找范围。")); return; }
            }
            QTextCursor edit(editor->document()); edit.beginEditBlock();
            for (auto it = matches.crbegin(); it != matches.crend(); ++it) {
                edit.setPosition(it->first); edit.setPosition(it->second, QTextCursor::KeepAnchor); edit.insertText(replacement->text());
            }
            edit.endEditBlock();
            status->setText(QObject::tr("已替换 %1 项，可在源文档中一次撤销。").arg(matches.size()));
        });
    } else if (name == "Markdown Preview") {
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
        auto stdoutDecoder = std::make_shared<QStringDecoder>(QStringDecoder::Utf8);
        auto stderrDecoder = std::make_shared<QStringDecoder>(QStringDecoder::Utf8);
        auto appendOutput = [process, output](const QByteArray &bytes, QStringDecoder &decoder) {
            const int used = process->property("displayedBytes").toInt();
            const int available = qMax(0, 1024 * 1024 - used);
            if (!available) {
                if (!process->property("outputTruncated").toBool() && !bytes.isEmpty()) {
                    process->setProperty("outputTruncated", true);
                    auto cursor = output->textCursor(); cursor.movePosition(QTextCursor::End);
                    cursor.insertText(QObject::tr("\n[输出超过 1 MiB，已截断]\n"));
                }
                return;
            }
            process->setProperty("displayedBytes", used + qMin(available, int(bytes.size())));
            auto cursor = output->textCursor(); cursor.movePosition(QTextCursor::End);
            cursor.insertText(decoder(bytes.left(available)));
            if (bytes.size() > available) {
                process->setProperty("outputTruncated", true);
                cursor.insertText(QObject::tr("\n[输出超过 1 MiB，已截断]\n"));
            }
        };
        QObject::connect(refresh, &QPushButton::clicked, page, [=] {
            refresh->setEnabled(false); output->clear(); status->setText(QObject::tr("正在读取 Git…"));
            process->setProperty("stage", 0);
            process->setProperty("displayedBytes", 0);
            process->setProperty("outputTruncated", false);
            stdoutDecoder->resetState(); stderrDecoder->resetState();
            process->start("git", {"--no-optional-locks", "status", "--short"}); timeout->start();
        });
        QObject::connect(process, &QProcess::readyReadStandardOutput, output, [=] { appendOutput(process->readAllStandardOutput(), *stdoutDecoder); });
        QObject::connect(process, &QProcess::readyReadStandardError, output, [=] { appendOutput(process->readAllStandardError(), *stderrDecoder); });
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
    if (name != "Git Changes") {
        if (!source) status->setText(QObject::tr("请先打开源文档，再从该文档打开扩展。"));
        else QObject::connect(source, &QObject::destroyed, page, [page, status] {
            status->setText(QObject::tr("源文档已关闭，请重新打开文档后再打开扩展。"));
            for (auto *button : page->findChildren<QPushButton *>()) button->setEnabled(false);
        });
    }
    return page;
}
