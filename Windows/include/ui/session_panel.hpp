#pragma once

#include "fluxdrop_core.h"
#include "networking.hpp"
#include <QFileDialog>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace ui {

class SessionPanel : public QWidget {
    Q_OBJECT
  public:
    explicit SessionPanel(QWidget* parent = nullptr);
    ~SessionPanel() override;

  protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

  private slots:
    void on_host_clicked();
    void on_join_clicked();
    void on_disconnect_clicked();
    void on_cancel_host_clicked();
    void on_send_files_clicked();
    void on_choose_files_clicked();
    void on_choose_folder_clicked();
    void on_clear_files_clicked();
    void on_device_row_clicked(QListWidgetItem* item);
    void on_manual_connect();
    void on_change_save_dir();

  private:
    // The three view states
    QStackedWidget* stack_;

    // === Page 0: Pre-session (Host / Join buttons + discovery) ===
    QWidget* pre_session_page_;
    QPushButton* host_button_;
    QPushButton* join_button_;
    QListWidget* device_list_;
    QLabel* discovery_label_;
    QLabel* save_label_;
    std::string save_dir_;

    // === Page 1: Hosting (waiting for guest) ===
    QWidget* hosting_page_;
    QLabel* pin_label_;
    QLabel* host_status_label_;
    QPushButton* cancel_host_button_;

    // === Page 2: In-session (bidirectional transfers) ===
    QWidget* session_page_;
    QLabel* session_peer_label_;
    QLabel* session_status_label_;
    QFrame* drop_area_;
    QLabel* drop_label_;
    QListWidget* file_list_widget_;
    QPushButton* send_button_;
    QPushButton* choose_file_button_;
    QPushButton* choose_folder_button_;
    QPushButton* clear_button_;
    QPushButton* disconnect_button_;
    QProgressBar* progress_bar_;
    QLabel* progress_label_;

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

    void add_path(const std::string& path);
    void update_file_list_ui();
    void show_pre_session();
    void show_hosting();
    void show_session(const std::string& peer_ip);
};

} // namespace ui
