#include <gtk/gtk.h>
#include "ui/main_window.hpp"
#include "fluxdrop_core.h"
#include "logger.hpp"

#ifdef _WIN32
#include <stdlib.h>
#endif

int main(int argc, char* argv[]) {

#ifdef _WIN32
    // Force OpenGL rendering on Windows to prevent GTK4 Vulkan crashes due to Optimus/OBS
    _putenv("GSK_RENDERER=gl");
#endif

    FD_LOG("FluxDrop starting");
    fd_init();

    int status = ui::run_gui(argc, argv);

    FD_LOG("FluxDrop shutting down — calling fd_cleanup()");
    fd_cleanup();
    FD_LOG("FluxDrop exited with status " << status);
    return status;
}
