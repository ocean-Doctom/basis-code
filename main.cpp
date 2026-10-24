#include<QApplication>
#include<QMainWindow>
#include<QTextEdit>
#include<QMenuBar>
#include<QMenu>
#include<QAction>

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
    QObject::connect(newaction,&QAction::triggered,editor,&QTextEdit::clear);

    window.show();


    return app.exec();
}