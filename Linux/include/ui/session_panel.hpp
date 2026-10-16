#pragma once

#include "fluxdrop_core.h"
#include "networking.hpp"
#include <gtk/gtk.h>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ui {

class SessionPanel {
  public:
    explicit SessionPanel(GtkWindow* parent_window);
    ~SessionPanel();

    GtkWidget* get_widget() const { return main_box_; }

  private:
    GtkWindow* parent_window_;
    GtkWidget* main_box_;

    // The three view states managed by GtkStack
    GtkWidget* stack_;

    // === Page 0: Pre-session (Host / Join buttons + discovery) ===
    GtkWidget* pre_session_page_;
    GtkWidget* host_button_;
    GtkWidget* join_button_;
    GtkWidget* device_list_box_;
    GtkWidget* discovery_label_;
    GtkWidget* save_label_;
    std::string save_dir_;

    // === Page 1: Hosting (waiting for guest) ===
    GtkWidget* hosting_page_;
    GtkWidget* pin_label_;
    GtkWidget* host_status_label_;
    GtkWidget* cancel_host_button_;

    // === Page 2: In-session (bidirectional transfers) ===
    GtkWidget* session_page_;
    GtkWidget* session_peer_label_;
    GtkWidget* session_status_label_;
    GtkWidget* drop_area_;
    GtkWidget* drop_label_;
    GtkWidget* file_list_box_;
    GtkWidget* send_button_;
    GtkWidget* choose_file_button_;
    GtkWidget* choose_folder_button_;
    GtkWidget* clear_button_;
    GtkWidget* disconnect_button_;
    GtkWidget* progress_bar_;
    GtkWidget* progress_label_;

    std::vector<std::string> queued_files_;
    std::atomic<bool> session_active_{false};

    // Discovery state
    std::map<std::string, networking::DiscoveredDevice> devices_;
    std::mutex devices_mutex_;

    void setup_pre_session_page();
    void setup_hosting_page();
    void setup_session_page();

    void start_discovery();
    void stop_discovery();
    void on_device_found(const networking::DiscoveredDevice& device);
    void connect_to_device(const networking::DiscoveredDevice& device);
    void start_join_session(const networking::DiscoveredDevice& device, const std::string& pin);

    void add_path(const std::string& path);
    void update_file_list_ui();
    void show_pre_session();
    void show_hosting();
    void show_session(const std::string& peer_ip);

    // Callbacks & event handlers
    static void on_host_clicked(GtkButton* button, gpointer user_data);
    static void on_join_clicked(GtkButton* button, gpointer user_data);
    static void on_cancel_host_clicked(GtkButton* button, gpointer user_data);
    static void on_disconnect_clicked(GtkButton* button, gpointer user_data);
    static void on_send_files_clicked(GtkButton* button, gpointer user_data);
    static void on_choose_files_clicked(GtkButton* button, gpointer user_data);
    static void on_choose_folder_clicked(GtkButton* button, gpointer user_data);
    static void on_clear_files_clicked(GtkButton* button, gpointer user_data);
    static void on_device_row_activated(GtkListBox* box, GtkListBoxRow* row, gpointer user_data);
    static void on_manual_connect(GtkButton* button, gpointer user_data);
    static void on_change_save_dir(GtkButton* button, gpointer user_data);

    // Drag and Drop
    static gboolean on_drag_drop(GtkDropTarget* target, const GValue* value, double x, double y, gpointer user_data);
    static GdkDragAction on_drag_enter(GtkDropTarget* target, double x, double y, gpointer user_data);
    static void on_drag_leave(GtkDropTarget* target, gpointer user_data);
};

} // namespace ui
