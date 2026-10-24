#include<QApplication>
#include<QMainWindow>
#include<QTextEdit>
#include<QMenuBar>
#include<QMenu>
#include<QAction>
#include <QFileDialog>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMessageBox>

int main(int argc,char *argv[]){
    QApplication app(argc,argv);

    QMainWindow window;
    window.setWindowTitle("记事本");
    window.resize(800,600);

    QTextEdit *editor =new QTextEdit(&window);
    editor->setAcceptRichText(false);
    editor->setPlaceholderText("在这里输入文字……");
    window.setCentralWidget(editor);

    QMenu *filemenu=window.menuBar()->addMenu("文件");
    QAction *newaction=filemenu->addAction("新建");
    QAction *openaction=filemenu->addAction("打开");
    QObject::connect(newaction,&QAction::triggered,editor,&QTextEdit::clear);
    QObject::connect(openaction, &QAction::triggered,
                 &window, [&window, editor]() {
    QString fileName = QFileDialog::getOpenFileName(
        &window,
        "打开文件",
        QString(),
        "文本文件 (*.txt);;所有文件 (*)"
    );
    if (fileName.isEmpty()) {
    return;
    }

    QFile file(fileName);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(&window, "打开失败", file.errorString());
        return;
    }

    QTextStream in(&file);
    editor->setPlainText(in.readAll());
    });

  

    window.show();


    return app.exec();
}