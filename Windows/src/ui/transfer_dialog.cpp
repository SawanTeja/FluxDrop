#include "ui/transfer_dialog.hpp"

namespace ui {

TransferDialog::TransferDialog(QWidget* parent, const std::string& title)
    : QDialog(parent) {

    setWindowTitle(QString::fromStdString(title));
    resize(400, 200);
    setModal(true);

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(24, 24, 24, 24);
    vbox->setSpacing(12);

    filename_label_ = new QLabel("", this);
    filename_label_->setProperty("cssClass", "title-text");
    vbox->addWidget(filename_label_);

    status_label_ = new QLabel("Preparing...", this);
    status_label_->setProperty("cssClass", "status-text");
    vbox->addWidget(status_label_);

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, 1000);
    progress_bar_->setValue(0);
    vbox->addWidget(progress_bar_);

    progress_label_ = new QLabel("0%", this);
    progress_label_->setProperty("cssClass", "status-text");
    vbox->addWidget(progress_label_);
}

void TransferDialog::set_filename(const std::string& filename) {
    filename_label_->setText(QString::fromStdString(filename));
}

void TransferDialog::set_progress(double fraction, const std::string& text) {
    progress_bar_->setValue(static_cast<int>(fraction * 1000));
    progress_label_->setText(QString::fromStdString(text));
}

void TransferDialog::set_status(const std::string& status) {
    status_label_->setText(QString::fromStdString(status));
}

} // namespace ui
