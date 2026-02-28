#pragma once

#include <gtk/gtk.h>
#include "networking.hpp"
#include <vector>
#include <string>
#include <thread>
#include <atomic>

namespace ui {

class FileSenderPanel {
public:
    explicit FileSenderPanel(GtkWindow* parent_window);
    ~FileSenderPanel();

    GtkWidget* get_widget() const { return panel_; }

private:
    GtkWidget* panel_;
    GtkWidget* drop_area_;
    GtkWidget* drop_label_;
    GtkWidget* file_list_box_;
    GtkWidget* send_button_;
    GtkWidget* clear_button_;
    GtkWidget* choose_file_button_;
    GtkWidget* choose_folder_button_;
    GtkWidget* status_label_;
    GtkWidget* pin_label_;
    GtkWidget* progress_bar_;
    GtkWidget* progress_label_;
    GtkWindow* parent_window_;

    std::vector<std::string> queued_files_;
    std::thread server_thread_;
    std::atomic<bool> server_running_{false};

    void add_path(const std::string& path);
    void clear_files();
    void start_server();
    void update_file_list_ui();

    static void on_choose_file(GtkButton* button, gpointer user_data);
    static void on_choose_folder(GtkButton* button, gpointer user_data);
    static void on_send_clicked(GtkButton* button, gpointer user_data);
    static void on_clear_clicked(GtkButton* button, gpointer user_data);
    static gboolean on_drop(GtkDropTarget* target, const GValue* value,
                            double x, double y, gpointer user_data);
};

} // namespace ui
