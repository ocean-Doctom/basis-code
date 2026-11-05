#include "notepad.h"
#include "appversion.h"
#include <QApplication>
#include <QFileInfo>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Basis Code");
    QApplication::setApplicationVersion(BASIS_CODE_VERSION);
    Notepad window;
    window.show();
    if (app.arguments().size() > 1) {
        const QString path = app.arguments().at(1);
        if (QFileInfo(path).isDir()) {
            window.openProject(path);
        } else {
            window.loadFile(path);
        }
    }
    return app.exec();
}
