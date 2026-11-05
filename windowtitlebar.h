#pragma once
#include <QFrame>

class QHBoxLayout;
class QMouseEvent;
class QToolButton;

class WindowTitleBar : public QFrame {
  public:
    explicit WindowTitleBar(QWidget *window);
    QHBoxLayout *contentLayout() const { return content; }

  protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void toggleMaximized();
    void updateMaximizeButton();
    QWidget *host;
    QHBoxLayout *content;
    QToolButton *maximizeButton;
};
