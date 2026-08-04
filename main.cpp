#include <QApplication>
#include "gui/MainWindow.h"
#include "gui/Theme.h"
#include "core/DHCPManager.h"
#include <crafter.h>

using namespace Crafter;

int main(int argc, char *argv[]) {
    // Register custom types for cross-thread signals
    qRegisterMetaType<core::DHCPServerConfig>("core::DHCPServerConfig");
    qRegisterMetaType<core::DHCPLease>("core::DHCPLease");

    // Initialize libcrafter safely
    InitCrafter();

    // Set logging pattern to include timestamps [HH:mm:ss.zzz]
    qSetMessagePattern("[%{time h:mm:ss.zzz}] %{message}");

    QApplication app(argc, argv);
    app.setStyleSheet(gui::Theme::globalStyleSheet());

    gui::MainWindow mainWindow;
    mainWindow.show();

    int ret = app.exec();

    // Clean up libcrafter securely
    CleanCrafter();

    return ret;
}
