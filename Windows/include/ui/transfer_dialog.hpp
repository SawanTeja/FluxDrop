#pragma once

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <string>

namespace ui {

class TransferDialog : public QDialog {
    Q_OBJECT
public:
    TransferDialog(QWidget* parent, const std::string& title);

    void set_filename(const std::string& filename);
    void set_progress(double fraction, const std::string& text);
    void set_status(const std::string& status);

private:
    QLabel* filename_label_;
    QProgressBar* progress_bar_;
    QLabel* progress_label_;
    QLabel* status_label_;
};

} // namespace ui
