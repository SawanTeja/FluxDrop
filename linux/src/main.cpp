#include <gtk/gtk.h>
#include "ui/main_window.hpp"
#include "fluxdrop_core.h"

int main(int argc, char* argv[]) {
    fd_init();

    int status = ui::run_gui(argc, argv);
    
    fd_cleanup();
    return status;
}
