#include "windowtitlebar.h"
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QMouseEvent>
#include <QToolButton>
#include <QWindow>

WindowTitleBar::WindowTitleBar(QWidget *window) : QFrame(window), host(window) {
    setObjectName("windowTitleBar");
    setFixedHeight(44);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *dragArea = new QWidget(this);
    dragArea->setObjectName("titleBarContent");
    dragArea->installEventFilter(this);
    content = new QHBoxLayout(dragArea);
    content->setContentsMargins(16, 0, 12, 0);
    content->setSpacing(6);
    layout->addWidget(dragArea, 1);
    auto captionButton = [this, layout](const QString &name,
                                        const QString &icon,
                                        const QString &label) {
        auto *button = new QToolButton(this);
        button->setObjectName(name);
        button->setIcon(QIcon(icon));
        button->setIconSize(QSize(12, 12));
        button->setAccessibleName(label);
        button->setToolTip(label);
        button->setFixedSize(46, 44);
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
        return button;
    };
    auto *minimize =
        captionButton("windowMinimize", ":/icons/minimize.svg", tr("最小化"));
    maximizeButton =
        captionButton("windowMaximize", ":/icons/maximize.svg", tr("最大化"));
    auto *close = captionButton("windowClose", ":/close.svg", tr("关闭"));
    connect(minimize, &QToolButton::clicked, host, &QWidget::showMinimized);
    connect(maximizeButton, &QToolButton::clicked, this,
            &WindowTitleBar::toggleMaximized);
    connect(close, &QToolButton::clicked, host, &QWidget::close);
    host->installEventFilter(this);
    updateMaximizeButton();
}

void WindowTitleBar::toggleMaximized() {
    if (host->isMaximized()) {
        host->showNormal();
    } else {
        host->showMaximized();
    }
    updateMaximizeButton();
}

void WindowTitleBar::updateMaximizeButton() {
    const bool maximized = host->isMaximized();
    maximizeButton->setIcon(
        QIcon(maximized ? ":/icons/restore.svg" : ":/icons/maximize.svg"));
    maximizeButton->setToolTip(maximized ? tr("还原") : tr("最大化"));
    maximizeButton->setAccessibleName(maximizeButton->toolTip());
    setToolTip(host->windowTitle());
}

void WindowTitleBar::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && host->windowHandle()) {
        host->windowHandle()->startSystemMove();
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void WindowTitleBar::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        toggleMaximized();
        event->accept();
        return;
    }
    QFrame::mouseDoubleClickEvent(event);
}

bool WindowTitleBar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == host && (event->type() == QEvent::WindowStateChange ||
                            event->type() == QEvent::WindowTitleChange)) {
        updateMaximizeButton();
    }
    // Buttons consume their own events. Only the empty content area starts a
    // drag.
    if (watched->objectName() == "titleBarContent") {
        if (event->type() == QEvent::MouseButtonPress) {
            mousePressEvent(static_cast<QMouseEvent *>(event));
            return event->isAccepted();
        }
        if (event->type() == QEvent::MouseButtonDblClick) {
            mouseDoubleClickEvent(static_cast<QMouseEvent *>(event));
            return event->isAccepted();
        }
    }
    return QFrame::eventFilter(watched, event);
}
