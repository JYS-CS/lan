#include <QApplication>
#include <QTimer>
#include <QThread>
#include "gui/MainWindow.h"
#include "gui/SplashScreen.h"
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

    // Show the Sniffnet-inspired splash screen first
    SplashScreen* splash = new SplashScreen();
    splash->show();

    // Optional: simulate a slow loading process so the user can appreciate the splash screen
    for (int i = 0; i <= 100; i += 5) {
        splash->setProgress(i, QString("Loading modules... %1%").arg(i));
        app.processEvents();
        QThread::msleep(50); // Add a small delay for visual effect
    }
    
    splash->setProgress(100, "Starting Net Monitor...");
    app.processEvents();

    gui::MainWindow* mainWindow = new gui::MainWindow();
    
    // Close splash and show main window after a short delay
    QTimer::singleShot(1500, [splash, mainWindow]() {
        splash->finish(mainWindow);
    });

    int ret = app.exec();

    // Clean up libcrafter securely
    CleanCrafter();

    return ret;
}
