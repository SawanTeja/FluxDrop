#include "fluxdrop_core.h"
#include "logger.hpp"
#include "ui/main_window.hpp"
#include <gtk/gtk.h>

int main(int argc, char* argv[]) {

    FD_LOG("FluxDrop starting");
    fd_init();

    int status = ui::run_gui(argc, argv);

    FD_LOG("FluxDrop shutting down — calling fd_cleanup()");
    fd_cleanup();
    FD_LOG("FluxDrop exited with status " << status);
    return status;
}
