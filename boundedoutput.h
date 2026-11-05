#pragma once
#include <QCoreApplication>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextDocument>

// Keep recent output without ever inserting an unbounded line into the widget.
inline void appendBoundedOutput(QPlainTextEdit *output, const QString &text) {
    if (text.isEmpty()) return;
    constexpr qsizetype limit = 256 * 1024;
    const QString marker = QCoreApplication::translate("Output", "[较早输出已截断，仅保留最近内容]\n");
    QStringView tail(text);
    bool truncated = tail.size() > limit - marker.size();
    if (truncated) {
        tail = tail.right(limit - marker.size());
        if (!tail.isEmpty() && tail.front().isLowSurrogate()) tail = tail.mid(1);
    }
    const int length = output->document()->characterCount() - 1;
    truncated = truncated || length + tail.size() > limit;
    auto cursor = output->textCursor();
    cursor.beginEditBlock();
    if (truncated) {
        int remove = qMin<qsizetype>(length, qMax<qsizetype>(0, length + tail.size() + marker.size() - limit));
        if (remove > 0 && remove < length && output->document()->characterAt(remove).isLowSurrogate()) ++remove;
        cursor.setPosition(0); cursor.setPosition(remove, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.setPosition(0); cursor.insertText(marker);
    }
    cursor.movePosition(QTextCursor::End); cursor.insertText(tail.toString());
    cursor.endEditBlock();
    output->setTextCursor(cursor); output->ensureCursorVisible();
}
