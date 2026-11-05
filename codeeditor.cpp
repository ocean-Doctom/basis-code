#include "codeeditor.h"
#include "syntaxhighlighter.h"
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QTextBlock>

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
}

void CodeEditor::setDarkTheme(bool dark) {
    setColorTheme(dark ? "tokyo-night" : "light");
}
void CodeEditor::setColorTheme(const QString &name) {
    darkTheme = name != "light";
    githubTheme = name == "github-dark-default";
    highlighter->setColorTheme(name);
    lineNumberArea->update();
}
