#pragma once

#include <QWidget>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QProgressBar>
#include <QFrame>
#include <QVBoxLayout>
#include <vector>
#include <string>
#include <atomic>

namespace ui {

class FileSenderPanel : public QWidget {
    Q_OBJECT
public:
    explicit FileSenderPanel(QWidget* parent = nullptr);
    ~FileSenderPanel() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void on_choose_file();
    void on_choose_folder();
    void on_send_clicked();
    void on_clear_clicked();
    void on_cancel_clicked();

private:
    QFrame* drop_area_;
    QLabel* drop_label_;
    QListWidget* file_list_widget_;
    QPushButton* send_button_;
    QPushButton* clear_button_;
    QPushButton* cancel_button_;
    QPushButton* choose_file_button_;
    QPushButton* choose_folder_button_;
    QLabel* status_label_;
    QLabel* pin_label_;
    QProgressBar* progress_bar_;
    QLabel* progress_label_;

    std::vector<std::string> queued_files_;
    std::atomic<bool> server_running_{false};

    void add_path(const std::string& path);
    void clear_files();
    void start_server();
    void cancel_server();
    void update_file_list_ui();
};

} // namespace ui
