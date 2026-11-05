#pragma once
#include <QWidget>
#include <QPointer>
class QPlainTextEdit;
// Native extensions; no VS Code extension-host compatibility is claimed.
QWidget *createBuiltinExtension(const QString &name, QPlainTextEdit *editor,
                               const QString &path, const QString &project, QWidget *parent);
QString formatDocumentText(const QString &text, bool json, QString *error);
