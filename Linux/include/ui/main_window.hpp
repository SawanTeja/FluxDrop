#pragma once

#include <gtk/gtk.h>

namespace ui {

class SessionPanel;

class MainWindow {
  public:
    explicit MainWindow(GtkApplication* app);
    ~MainWindow();

    GtkWidget* get_window() const { return window_; }

  private:
    GtkWidget* window_;
    SessionPanel* session_panel_;

    void setup_css();
    static void on_destroy(GtkWidget* widget, gpointer data);
};

int run_gui(int argc, char* argv[]);

} // namespace ui
