#include <QApplication>
#include "ui/main_window.hpp"
#include "fluxdrop_core.h"
#include "logger.hpp"

int main(int argc, char* argv[]) {
    FD_LOG("FluxDrop starting");
    fd_init();

    QApplication app(argc, argv);
    app.setApplicationName("FluxDrop");
    app.setOrganizationDomain("fluxdrop.dev");

    ui::MainWindow window;
    window.show();

    int status = app.exec();

    FD_LOG("FluxDrop shutting down — calling fd_cleanup()");
    fd_cleanup();
    FD_LOG("FluxDrop exited with status " << status);
    return status;
}
