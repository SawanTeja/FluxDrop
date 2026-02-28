#pragma once

#include <gtk/gtk.h>

namespace ui {

class FileSenderPanel;
class DeviceListPanel;

class MainWindow {
public:
    explicit MainWindow(GtkApplication* app);
    ~MainWindow();

    GtkWidget* get_window() const { return window_; }

private:
    GtkWidget* window_;
    GtkWidget* stack_;
    GtkWidget* header_bar_;

    FileSenderPanel* send_panel_;
    DeviceListPanel* receive_panel_;

    void setup_css();
    static void on_destroy(GtkWidget* widget, gpointer data);
};

int run_gui(int argc, char* argv[]);

} // namespace ui
