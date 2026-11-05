#include "codeeditor.h"
#include <QApplication>
#include <QImage>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextLayout>
#include <iostream>
#include <stdexcept>

static void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        CodeEditor editor;
        editor.resize(600, 300);
        editor.setLineWrapMode(QPlainTextEdit::NoWrap);
        editor.show();
        app.processEvents();
        auto *gutter = editor.findChild<QWidget *>("lineNumberArea");
        check(gutter && gutter->isVisible(), "line number area visible");
        const int singleDigitWidth = gutter->width();
        QStringList lines;
        for (int i = 1; i <= 120; ++i)
            lines.append(QString("line %1 ").arg(i) + QString(200, 'x'));
        editor.setPlainText(lines.join('\n'));
        app.processEvents();
        check(editor.blockCount() == 120 && gutter->width() > singleDigitWidth,
              "gutter grows for three digit line numbers");
        check(editor.viewport()->geometry().left() >= gutter->geometry().right(),
              "line numbers do not overlap editor text");
        const auto beforeScroll = gutter->grab().toImage();
        editor.verticalScrollBar()->setValue(50);
        app.processEvents();
        check(gutter->grab().toImage() != beforeScroll,
              "visible line numbers change on vertical scroll");
        const auto beforeHorizontalScroll = gutter->grab().toImage();
        editor.horizontalScrollBar()->setValue(100);
        app.processEvents();
        check(gutter->grab().toImage() == beforeHorizontalScroll,
              "line numbers stay fixed on horizontal scroll");
        const int originalWidth = gutter->width();
        auto font = editor.font();
        font.setPointSize(24);
        editor.setFont(font);
        app.processEvents();
        check(gutter->width() > originalWidth, "gutter follows font size");
        editor.setLineWrapMode(QPlainTextEdit::WidgetWidth);
        editor.resize(360, 220);
        app.processEvents();
        check(editor.blockCount() == 120 && gutter->height() == editor.viewport()->height(),
              "wrapping keeps logical line count and resized gutter height");
        editor.setPlainText("single line");
        app.processEvents();
        check(gutter->width() < originalWidth * 2,
              "gutter shrinks after deleting lines");
        auto colorAt = [&](int block, int offset) {
            const auto formats = editor.document()->findBlockByNumber(block).layout()->formats();
            for (const auto &range : formats)
                if (offset >= range.start && offset < range.start + range.length)
                    return range.format.foreground().color().name();
            return QString();
        };
        for (const auto &suffix : {"cpp", "java", "py", "js", "ts"}) {
            editor.setFilePath(QString("example.") + suffix);
            editor.setPlainText(QString(suffix) == "py" ? "def greet():\n    return 42"
                                                       : "class Example { return 42; }");
            app.processEvents();
            check(colorAt(0, 0) == "#794baa", "language keyword highlighted");
        }
        editor.setFilePath("example.cpp");
        editor.setPlainText("/* comment\n\nstill comment */ int value = 42;\nconst char *s = \"// string\";");
        app.processEvents();
        check(colorAt(2, 0) == "#718774" && colorAt(2, 17) == "#794baa",
              "multiline comment crosses blank lines and ends before keyword");
        editor.setFilePath("example.py");
        editor.setPlainText("text = \"\"\"hello\n\nworld\"\"\"\n# comment");
        app.processEvents();
        check(colorAt(2, 0) == "#a36136" && colorAt(3, 0) == "#718774",
              "Python triple quoted strings and comments");
        editor.setFilePath("example.json");
        editor.setPlainText("{\"key\": true}");
        app.processEvents();
        check(colorAt(0, 1) == "#426fae" && colorAt(0, 8) == "#794baa",
              "JSON property keys and literals");
        for (const auto &example : QList<QPair<QString, QString>>{
                 {"example.html", "<div title=\"hello\">"},
                 {"example.css", "color: red;"},
                 {"example.md", "# Heading"}}) {
            editor.setFilePath(example.first);
            editor.setPlainText(example.second);
            app.processEvents();
            check(colorAt(0, 0) == "#426fae", "markup and stylesheet highlighting");
        }
        editor.setFilePath("CMakeLists.txt");
        editor.setPlainText("project(example)");
        app.processEvents();
        check(colorAt(0, 0) == "#794baa", "CMake keyword highlighted");
        editor.setColorTheme("github-dark-default");
        editor.setFilePath("example.cpp");
        editor.setPlainText("return 42;");
        app.processEvents();
        check(colorAt(0, 0) == "#ff7b72" && colorAt(0, 7) == "#79c0ff",
              "GitHub Dark Default token colors applied");
        editor.setColorTheme("light");
        editor.setFilePath("example.txt");
        app.processEvents();
        check(colorAt(0, 1).isEmpty(), "plain text clears previous highlighting");
        std::cout << "Passed line number checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
