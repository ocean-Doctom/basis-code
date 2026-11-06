#include "codeeditor.h"
#include "syntaxhighlighter.h"
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTextBlock>
#include <QTimer>
#include <algorithm>

class LineNumberArea : public QWidget {
  public:
    explicit LineNumberArea(CodeEditor *editor)
        : QWidget(editor), editor(editor) {
        setObjectName("lineNumberArea");
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

  protected:
    void paintEvent(QPaintEvent *event) override {
        editor->paintLineNumbers(event);
    }

  private:
    CodeEditor *editor;
};

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent) {
    highlighter = new SyntaxHighlighter(document());
    lineNumberArea = new LineNumberArea(this);
    bracketTimer = new QTimer(this);
    bracketTimer->setSingleShot(true);
    connect(bracketTimer, &QTimer::timeout, this, [this] { updateBracketMatch(); });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { bracketTimer->start(0); });
    connect(this, &QPlainTextEdit::textChanged, this, [this] { bracketTimer->start(0); });
    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this] { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            [this](const QRect &rect, int dy) {
                if (dy)
                    lineNumberArea->scroll(0, dy);
                else
                    lineNumberArea->update(0, rect.y(),
                                           lineNumberArea->width(), rect.height());
            });
    connect(this, &QPlainTextEdit::cursorPositionChanged, lineNumberArea,
            [this] { lineNumberArea->update(); });
    updateLineNumberAreaWidth();
}

int CodeEditor::lineNumberAreaWidth() const {
    const int digits = QString::number(blockCount()).size();
    return 18 + digits * fontMetrics().horizontalAdvance(QLatin1Char('9'));
}

void CodeEditor::updateLineNumberAreaWidth() {
    const int width = lineNumberAreaWidth();
    setViewportMargins(width, 0, 0, 0);
    const QRect area = contentsRect();
    lineNumberArea->setGeometry(area.left(), viewport()->geometry().top(),
                                width, viewport()->height());
    lineNumberArea->update();
}

void CodeEditor::resizeEvent(QResizeEvent *event) {
    QPlainTextEdit::resizeEvent(event);
    updateLineNumberAreaWidth();
}

void CodeEditor::changeEvent(QEvent *event) {
    QPlainTextEdit::changeEvent(event);
    if (lineNumberArea && (event->type() == QEvent::FontChange ||
                           event->type() == QEvent::ApplicationFontChange))
        updateLineNumberAreaWidth();
}

void CodeEditor::paintLineNumbers(QPaintEvent *event) {
    QPainter painter(lineNumberArea);
    painter.fillRect(event->rect(), QColor(githubTheme ? "#0d1117" : darkTheme ? "#202231" : "#fafbfd"));
    painter.setFont(font());
    QTextBlock block = firstVisibleBlock();
    while (block.isValid()) {
        const QRectF bounds = blockBoundingGeometry(block).translated(contentOffset());
        if (bounds.top() > event->rect().bottom())
            break;
        if (block.isVisible() && bounds.bottom() >= event->rect().top()) {
            painter.setPen(block.blockNumber() == textCursor().blockNumber()
                               ? QColor(githubTheme ? "#e6edf3" : darkTheme ? "#c0caf5" : "#344e79") : QColor(githubTheme ? "#6e7681" : darkTheme ? "#9aa5ce" : "#606e87"));
            painter.drawText(0, qRound(bounds.top()), lineNumberArea->width() - 8,
                             fontMetrics().height(), Qt::AlignRight | Qt::AlignTop,
                             QString::number(block.blockNumber() + 1));
        }
        block = block.next();
    }
}

void CodeEditor::setFilePath(const QString &path) {
    highlighter->setFilePath(path);
    bracketTimer->start(0);
}
bool CodeEditor::largeFileMode() const { return highlighter->largeFileMode(); }

void CodeEditor::setDarkTheme(bool dark) {
    setColorTheme(dark ? "tokyo-night" : "light");
}
void CodeEditor::setColorTheme(const QString &name) {
    darkTheme = name != "light";
    githubTheme = name == "github-dark-default";
    highlighter->setColorTheme(name);
    bracketTimer->start(0);
    lineNumberArea->update();
}

void CodeEditor::updateBracketMatch() {
    bracketDestination = -1;
    setExtraSelections({});
    if (textCursor().hasSelection() || largeFileMode()) return;
    const QString opening = "([{", closing = ")]}";
    int source = -1;
    QChar first;
    for (int position : {textCursor().position() - 1, textCursor().position()}) {
        if (position < 0 || position >= document()->characterCount() - 1) continue;
        const auto block = document()->findBlock(position);
        const auto *positions = highlighter->bracketPositions(block);
        if (!positions || !std::binary_search(positions->cbegin(), positions->cend(), position - block.position())) continue;
        source = position; first = document()->characterAt(position); break;
    }
    if (source < 0) return;
    const bool forward = opening.contains(first);
    const int direction = forward ? 1 : -1;
    QVector<QChar> stack{first};
    int partner = -1, blocks = 0, tokens = 0;
    bool mismatch = false;
    auto block = document()->findBlock(source);
    while (block.isValid()) {
        if (++blocks > 256) return;
        const auto *positions = highlighter->bracketPositions(block);
        if (!positions) return;
        int index = forward
            ? int(std::upper_bound(positions->cbegin(), positions->cend(), source - block.position()) - positions->cbegin())
            : int(std::lower_bound(positions->cbegin(), positions->cend(), source - block.position()) - positions->cbegin()) - 1;
        for (; index >= 0 && index < positions->size(); index += direction) {
            const int position = block.position() + positions->at(index);
            if (++tokens > 10000 || qAbs(position - source) > 20000) return;
            const QChar bracket = document()->characterAt(position);
            const bool opens = opening.contains(bracket);
            if (opens == forward) stack.append(bracket);
            else {
                const int expected = forward ? opening.indexOf(stack.last()) : closing.indexOf(stack.last());
                const int actual = forward ? closing.indexOf(bracket) : opening.indexOf(bracket);
                if (actual != expected) { mismatch = true; break; }
                stack.removeLast();
                if (stack.isEmpty()) { partner = position; break; }
            }
        }
        if (mismatch || partner >= 0) break;
        block = forward ? block.next() : block.previous();
        if (block.isValid() && qAbs(block.position() - source) > 20000) return;
    }
    QList<QTextEdit::ExtraSelection> selections;
    const bool matched = partner >= 0;
    for (int position : matched ? QList<int>{source, partner} : QList<int>{source}) {
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(document());
        selection.cursor.setPosition(position); selection.cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        selection.format.setBackground(QColor(matched ? (darkTheme ? "#264f78" : "#dbeafe") : (darkTheme ? "#5a1d23" : "#fde8e8")));
        selection.format.setForeground(QColor(matched ? (darkTheme ? "#79c0ff" : "#1d4ed8") : (darkTheme ? "#ff7b72" : "#b42318")));
        selections.append(selection);
    }
    setExtraSelections(selections);
    if (matched) bracketDestination = partner + (source == textCursor().position() - 1 ? 1 : 0);
}

void CodeEditor::jumpToMatchingBracket() {
    updateBracketMatch();
    if (bracketDestination < 0) return;
    auto cursor = textCursor(); cursor.setPosition(bracketDestination);
    setTextCursor(cursor); centerCursor();
}
