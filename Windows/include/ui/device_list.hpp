#pragma once

#include <QWidget>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QFrame>
#include <QVBoxLayout>
#include "networking.hpp"
#include "fluxdrop_core.h"
#include <map>
#include <mutex>
#include <string>
#include <atomic>
#include <future>

namespace ui {

class DeviceListPanel : public QWidget {
    Q_OBJECT
public:
    explicit DeviceListPanel(QWidget* parent = nullptr);
    ~DeviceListPanel() override;

    void start_discovery();
    void stop_discovery();

    // Public for C callback access (same pattern as GTK version)
    QLabel* status_label_;
    QProgressBar* progress_bar_;
    QLabel* progress_label_;
    QPushButton* cancel_button_;
    QWidget* parent_window_;
    std::atomic<bool> transferring_{false};

    void clear_and_restart_discovery();

private slots:
    void on_device_row_clicked(QListWidgetItem* item);
    void on_manual_connect();
    void on_change_save_dir();
    void on_cancel_transfer();

private:
    QListWidget* list_widget_;
    QLabel* info_label_;
    QLabel* save_label_;
    std::string save_dir_;

    std::map<std::string, networking::DiscoveredDevice> devices_;
    std::mutex devices_mutex_;

    void on_device_found(const networking::DiscoveredDevice& device);
    void connect_to_device(const networking::DiscoveredDevice& device);
};

} // namespace ui
