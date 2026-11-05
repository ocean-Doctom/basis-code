#pragma once
#include <QWidget>
#include <functional>
QWidget *createProjectSearch(const QString &root, const QString &initial,
                            std::function<void(const QString &, int)> open, QWidget *parent);
