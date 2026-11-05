#pragma once
#include <QWidget>
#include <memory>
class ChatGptPage : public QWidget {
  public:
    explicit ChatGptPage(QWidget *parent = nullptr, const QString &provider = "chatgpt");
    ~ChatGptPage() override;
    void setComposerText(const QString &text);
  private:
    friend struct ChatPageTestAccess;
    struct State;
    std::unique_ptr<State> state;
};
