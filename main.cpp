#include <QApplication>
#include <QSystemTrayIcon>

#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("WonderVisionStudio"));
    QCoreApplication::setApplicationName(QStringLiteral("WonderVisionStudio"));
    const bool start_screen = QCoreApplication::arguments().contains(QStringLiteral("--start-screen"));
    const bool start_in_tray = QCoreApplication::arguments().contains(QStringLiteral("--minimize-to-tray"));
    MainWindow window(start_screen, start_in_tray);
    if(start_in_tray) window.hide();
    else window.show();
    return app.exec();
}
