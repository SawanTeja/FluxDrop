#include "ui/file_sender.hpp"
#include "ui/transfer_dialog.hpp"
#include "logger.hpp"
#include "fluxdrop_core.h"
#include <QFileDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QHBoxLayout>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace ui {

// Generation counter — same pattern as GTK version
static std::atomic<uint64_t> g_sender_gen{0};

// Static context for C callbacks (mirrors GTK version exactly)
static QLabel* g_pin_lbl = nullptr;
static QLabel* g_status_lbl = nullptr;
static QProgressBar* g_progress_br = nullptr;
static QLabel* g_progress_lbl = nullptr;
static QPushButton* g_send_btn = nullptr;
static QPushButton* g_clear_btn_widget = nullptr;
static QPushButton* g_cancel_btn = nullptr;
static QPushButton* g_choose_file_btn = nullptr;
static QPushButton* g_choose_folder_btn = nullptr;
static std::atomic<bool>* g_running_ptr = nullptr;

FileSenderPanel::FileSenderPanel(QWidget* parent)
    : QWidget(parent) {

    FD_LOG("FileSenderPanel created");

    setAcceptDrops(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // Drop zone
    drop_area_ = new QFrame(this);
    drop_area_->setProperty("cssClass", "drop-zone");
    auto* drop_layout = new QVBoxLayout(drop_area_);
    drop_layout->setAlignment(Qt::AlignCenter);

    drop_label_ = new QLabel(QString::fromUtf8("🗂️  Drag & Drop Files Here"), drop_area_);
    drop_label_->setProperty("cssClass", "title-text");
    drop_label_->setAlignment(Qt::AlignCenter);
    drop_layout->addWidget(drop_label_);

    auto* sub_label = new QLabel("or use the buttons below", drop_area_);
    sub_label->setProperty("cssClass", "subtitle-text");
    sub_label->setAlignment(Qt::AlignCenter);
    drop_layout->addWidget(sub_label);

    layout->addWidget(drop_area_);

    // Buttons row
    auto* btn_box = new QHBoxLayout();
    btn_box->setAlignment(Qt::AlignCenter);

    choose_file_button_ = new QPushButton(QString::fromUtf8("📄 Choose Files"), this);
    choose_file_button_->setProperty("cssClass", "suggested-action");
    connect(choose_file_button_, &QPushButton::clicked, this, &FileSenderPanel::on_choose_file);
    btn_box->addWidget(choose_file_button_);

    choose_folder_button_ = new QPushButton(QString::fromUtf8("📁 Choose Folder"), this);
    choose_folder_button_->setProperty("cssClass", "suggested-action");
    connect(choose_folder_button_, &QPushButton::clicked, this, &FileSenderPanel::on_choose_folder);
    btn_box->addWidget(choose_folder_button_);

    layout->addLayout(btn_box);

    // File list
    file_list_widget_ = new QListWidget(this);
    file_list_widget_->setSelectionMode(QAbstractItemView::NoSelection);
    file_list_widget_->setMinimumHeight(120);
    file_list_widget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(file_list_widget_, 1);

    // Action row
    auto* action_box = new QHBoxLayout();
    action_box->setAlignment(Qt::AlignCenter);

    send_button_ = new QPushButton("Start Sharing", this);
    send_button_->setProperty("cssClass", "suggested-action");
    send_button_->setEnabled(false);
    connect(send_button_, &QPushButton::clicked, this, &FileSenderPanel::on_send_clicked);
    action_box->addWidget(send_button_);

    clear_button_ = new QPushButton(QString::fromUtf8("🗑️ Clear"), this);
    clear_button_->setProperty("cssClass", "destructive-action");
    connect(clear_button_, &QPushButton::clicked, this, &FileSenderPanel::on_clear_clicked);
    action_box->addWidget(clear_button_);

    cancel_button_ = new QPushButton(QString::fromUtf8("⏹ Cancel Sharing"), this);
    cancel_button_->setProperty("cssClass", "destructive-action");
    cancel_button_->setVisible(false);
    connect(cancel_button_, &QPushButton::clicked, this, &FileSenderPanel::on_cancel_clicked);
    action_box->addWidget(cancel_button_);

    layout->addLayout(action_box);

    // PIN display
    pin_label_ = new QLabel("", this);
    pin_label_->setProperty("cssClass", "pin-display");
    pin_label_->setAlignment(Qt::AlignCenter);
    pin_label_->setVisible(false);
    layout->addWidget(pin_label_);

    // Status
    status_label_ = new QLabel("", this);
    status_label_->setProperty("cssClass", "status-text");
    status_label_->setAlignment(Qt::AlignCenter);
    layout->addWidget(status_label_);

    // Progress bar
    progress_bar_ = new QProgressBar(this);
    progress_bar_->setRange(0, 1000);
    progress_bar_->setValue(0);
    progress_bar_->setVisible(false);
    layout->addWidget(progress_bar_);

    // Progress label
    progress_label_ = new QLabel("", this);
    progress_label_->setProperty("cssClass", "status-text");
    progress_label_->setAlignment(Qt::AlignCenter);
    layout->addWidget(progress_label_);
}

FileSenderPanel::~FileSenderPanel() {
    FD_LOG("~FileSenderPanel — cleaning up");
    g_sender_gen++;
    server_running_ = false;
    fd_cancel_server();
    FD_LOG("~FileSenderPanel — done");
}

void FileSenderPanel::add_path(const std::string& path) {
    FD_LOG("Adding path: " << path);
    fs::path p = fs::path(path).lexically_normal();

    std::error_code ec;
    if (!fs::exists(p, ec)) {
        FD_WARN("Ignoring missing path: " << p.string());
        return;
    }

    const std::string normalized = p.string();
    if (std::find(queued_files_.begin(), queued_files_.end(), normalized) != queued_files_.end()) {
        FD_WARN("Skipping duplicate queued path: " << normalized);
        return;
    }

    if (fs::is_directory(p, ec) || fs::is_regular_file(p, ec)) {
        queued_files_.push_back(normalized);
    }
    FD_LOG("Total queued files: " << queued_files_.size());
    update_file_list_ui();
}

void FileSenderPanel::clear_files() {
    FD_LOG("Clearing file queue");
    queued_files_.clear();
    update_file_list_ui();
}

void FileSenderPanel::update_file_list_ui() {
    file_list_widget_->clear();

    for (const auto& file : queued_files_) {
        fs::path p(file);
        std::string display = "\xF0\x9F\x93\x84 " + p.filename().string(); // 📄

        std::error_code ec;
        auto fsize = fs::file_size(file, ec);
        if (!ec) {
            double size = static_cast<double>(fsize);
            const char* units[] = {"B", "KB", "MB", "GB"};
            int i = 0;
            while (size >= 1024 && i < 3) { size /= 1024; i++; }
            char buf[64];
            snprintf(buf, sizeof(buf), " (%.1f %s)", size, units[i]);
            display += buf;
        }

        file_list_widget_->addItem(QString::fromStdString(display));
    }

    send_button_->setEnabled(!queued_files_.empty() && !server_running_);
}

void FileSenderPanel::start_server() {
    if (queued_files_.empty() || server_running_) {
        FD_WARN("start_server called but empty=" << queued_files_.empty() << " running=" << server_running_.load());
        return;
    }

    FD_LOG("start_server() — starting with " << queued_files_.size() << " files");

    g_sender_gen++;
    uint64_t gen = g_sender_gen.load();

    server_running_ = true;

    send_button_->setVisible(false);
    clear_button_->setVisible(false);
    cancel_button_->setVisible(true);
    choose_file_button_->setEnabled(false);
    choose_folder_button_->setEnabled(false);
    progress_bar_->setVisible(true);
    pin_label_->setVisible(true);
    progress_bar_->setValue(0);
    status_label_->setText("Starting server...");
    progress_label_->setText("");

    std::vector<const char*> c_paths;
    for (const auto& filepath : queued_files_) {
        c_paths.push_back(filepath.c_str());
    }

    // Store widget pointers for C callbacks
    g_pin_lbl = pin_label_;
    g_status_lbl = status_label_;
    g_progress_br = progress_bar_;
    g_progress_lbl = progress_label_;
    g_send_btn = send_button_;
    g_clear_btn_widget = clear_button_;
    g_cancel_btn = cancel_button_;
    g_choose_file_btn = choose_file_button_;
    g_choose_folder_btn = choose_folder_button_;
    g_running_ptr = &server_running_;

    FD_LOG("start_server() — gen=" << gen << " launching fd_start_server");

    auto ready_cb = [](const char* ip, int port, int pin) {
        FD_LOG("Server ready: " << ip << ":" << port << " PIN=" << pin);
        uint64_t gen = g_sender_gen.load();
        QString pin_text = "PIN: " + QString::number(pin);
        QString status_text = "Listening on " + QString(ip) + ":" + QString::number(port) + " — Waiting for receiver...";
        QMetaObject::invokeMethod(g_pin_lbl, [gen, pin_text]() {
            if (gen != g_sender_gen.load()) return;
            g_pin_lbl->setText(pin_text);
        }, Qt::QueuedConnection);
        QMetaObject::invokeMethod(g_status_lbl, [gen, status_text]() {
            if (gen != g_sender_gen.load()) return;
            g_status_lbl->setText(status_text);
        }, Qt::QueuedConnection);
    };

    auto status_cb = [](const char* msg) {
        FD_LOG("Server status: " << msg);
        uint64_t gen = g_sender_gen.load();
        QString text = QString::fromUtf8(msg);
        QMetaObject::invokeMethod(g_status_lbl, [gen, text]() {
            if (gen != g_sender_gen.load()) return;
            g_status_lbl->setText(text);
        }, Qt::QueuedConnection);
    };

    auto error_cb = [](const char* err) {
        FD_ERR("Server error: " << err);
        uint64_t gen = g_sender_gen.load();
        QString msg = QString::fromUtf8("❌ Error: ") + QString::fromUtf8(err);
        QMetaObject::invokeMethod(g_status_lbl, [gen, msg]() {
            if (gen != g_sender_gen.load()) return;
            g_status_lbl->setText(msg);
            g_progress_br->setValue(1000);
            g_progress_lbl->setText("Done");
            g_send_btn->setVisible(true);
            g_send_btn->setEnabled(true);
            g_clear_btn_widget->setVisible(true);
            g_cancel_btn->setVisible(false);
            g_choose_file_btn->setEnabled(true);
            g_choose_folder_btn->setEnabled(true);
        }, Qt::QueuedConnection);
        if (g_running_ptr) *g_running_ptr = false;
    };

    auto progress_cb = [](const char* filename, uint64_t transferred, uint64_t total, double speed) {
        double frac = (total > 0) ? (static_cast<double>(transferred) / total) : 0.0;
        int pct = static_cast<int>(frac * 100);
        char buf[128];
        snprintf(buf, sizeof(buf), "%d%% — %.1f MB/s — %s", pct, speed, filename);
        uint64_t gen = g_sender_gen.load();
        int value = static_cast<int>(frac * 1000);
        QString text = QString::fromUtf8(buf);
        QMetaObject::invokeMethod(g_progress_br, [gen, value]() {
            if (gen != g_sender_gen.load()) return;
            g_progress_br->setValue(value);
        }, Qt::QueuedConnection);
        QMetaObject::invokeMethod(g_progress_lbl, [gen, text]() {
            if (gen != g_sender_gen.load()) return;
            g_progress_lbl->setText(text);
        }, Qt::QueuedConnection);
    };

    auto complete_cb = []() {
        FD_LOG("Server transfer complete");
        uint64_t gen = g_sender_gen.load();
        QMetaObject::invokeMethod(g_status_lbl, [gen]() {
            if (gen != g_sender_gen.load()) return;
            g_status_lbl->setText(QString::fromUtf8("✅ All files transferred successfully!"));
            g_progress_br->setValue(1000);
            g_progress_lbl->setText("Done");
            g_send_btn->setVisible(true);
            g_send_btn->setEnabled(true);
            g_clear_btn_widget->setVisible(true);
            g_cancel_btn->setVisible(false);
            g_choose_file_btn->setEnabled(true);
            g_choose_folder_btn->setEnabled(true);
        }, Qt::QueuedConnection);
        if (g_running_ptr) *g_running_ptr = false;
    };

    fd_start_server(c_paths.data(), c_paths.size(),
                    ready_cb, status_cb, error_cb, progress_cb, complete_cb);
}

void FileSenderPanel::cancel_server() {
    FD_LOG("cancel_server() — non-blocking cancel, incrementing generation");

    g_sender_gen++;
    server_running_ = false;

    fd_request_cancel_server();

    pin_label_->setText("");
    pin_label_->setVisible(false);
    progress_bar_->setVisible(false);
    progress_bar_->setValue(0);
    progress_label_->setText("");
    status_label_->setText("Sharing cancelled.");

    send_button_->setVisible(true);
    send_button_->setEnabled(!queued_files_.empty());
    clear_button_->setVisible(true);
    choose_file_button_->setEnabled(true);
    choose_folder_button_->setEnabled(true);
    cancel_button_->setVisible(false);

    FD_LOG("cancel_server() — UI reset complete");
}

// Drag & drop handlers (replacing GTK's GtkDropTarget)

void FileSenderPanel::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        drop_area_->setProperty("cssClass", "drop-zone-active");
        drop_area_->style()->unpolish(drop_area_);
        drop_area_->style()->polish(drop_area_);
    }
}

void FileSenderPanel::dragLeaveEvent(QDragLeaveEvent* /*event*/) {
    drop_area_->setProperty("cssClass", "drop-zone");
    drop_area_->style()->unpolish(drop_area_);
    drop_area_->style()->polish(drop_area_);
}

void FileSenderPanel::dropEvent(QDropEvent* event) {
    FD_LOG("File drop received");
    drop_area_->setProperty("cssClass", "drop-zone");
    drop_area_->style()->unpolish(drop_area_);
    drop_area_->style()->polish(drop_area_);

    const QMimeData* mime = event->mimeData();
    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (url.isLocalFile()) {
                add_path(url.toLocalFile().toStdString());
            }
        }
    }
}

// Button click handlers

void FileSenderPanel::on_choose_file() {
    FD_LOG("Choose file dialog opening");
    QStringList files = QFileDialog::getOpenFileNames(this, "Select Files");
    for (const QString& file : files) {
        add_path(file.toStdString());
    }
}

void FileSenderPanel::on_choose_folder() {
    FD_LOG("Choose folder dialog opening");
    QString dir = QFileDialog::getExistingDirectory(this, "Select Folder");
    if (!dir.isEmpty()) {
        add_path(dir.toStdString());
    }
}

void FileSenderPanel::on_send_clicked() {
    FD_LOG("Send button clicked");
    start_server();
}

void FileSenderPanel::on_clear_clicked() {
    FD_LOG("Clear button clicked");
    clear_files();
    pin_label_->setVisible(false);
    progress_bar_->setVisible(false);
    status_label_->setText("");
    progress_label_->setText("");
}

void FileSenderPanel::on_cancel_clicked() {
    FD_LOG("Cancel button clicked");
    cancel_server();
}

} // namespace ui
