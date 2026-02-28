#pragma once

#include <gtk/gtk.h>
#include "networking.hpp"
#include <map>
#include <mutex>
#include <string>
#include <memory>
#include <future>

namespace ui {

class DeviceListPanel {
    friend void on_connect_btn_clicked(GtkButton* btn, gpointer data);
public:
    explicit DeviceListPanel(GtkWindow* parent_window);
    ~DeviceListPanel();

    GtkWidget* get_widget() const { return panel_; }
    void start_discovery();
    void stop_discovery();

private:
    GtkWidget* panel_;
    GtkWidget* list_box_;
    GtkWidget* status_label_;
    GtkWidget* progress_bar_;
    GtkWidget* progress_label_;
    GtkWidget* info_label_;
    GtkWidget* save_label_;
    GtkWindow* parent_window_;
    std::string save_dir_;

    networking::DiscoveryListener listener_;
    std::map<std::string, networking::DiscoveredDevice> devices_;
    std::mutex devices_mutex_;
    std::atomic<bool> transferring_{false};
    std::shared_ptr<std::atomic<bool>> cancel_flag_;

    GtkWidget* cancel_button_;

    void on_device_found(const networking::DiscoveredDevice& device);
    void connect_to_device(const networking::DiscoveredDevice& device);

    static void row_activated_cb(GtkListBox* list_box, GtkListBoxRow* row, gpointer user_data);
};

} // namespace ui
