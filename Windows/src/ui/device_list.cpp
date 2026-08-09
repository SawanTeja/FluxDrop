#include "ui/device_list.hpp"
#include "ui/transfer_dialog.hpp"
#include "logger.hpp"
#include <QFileDialog>
#include <QDialog>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStyle>
#include <thread>

namespace ui {

// Generation counter — same pattern as GTK version
static std::atomic<uint64_t> g_recv_gen{0};

// Static context for C callbacks
static DeviceListPanel* g_client_panel = nullptr;

DeviceListPanel::DeviceListPanel(QWidget* parent)
    : QWidget(parent), parent_window_(parent) {

    FD_LOG("DeviceListPanel created");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // Header
    auto* header = new QLabel(QString::fromUtf8("📡 Nearby Devices"), this);
    header->setProperty("cssClass", "title-text");
    layout->addWidget(header);

    info_label_ = new QLabel("Scanning for FluxDrop senders on your network...", this);
    info_label_->setProperty("cssClass", "subtitle-text");
    layout->addWidget(info_label_);

    // Save folder picker
    QString downloads_dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!downloads_dir.isEmpty() && QDir(downloads_dir).exists()) {
        save_dir_ = downloads_dir.toStdString();
    } else {
        save_dir_ = QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdString();
    }

    auto* save_row = new QHBoxLayout();

    auto* save_icon = new QLabel(QString::fromUtf8("📂 Save to:"), this);
    save_icon->setProperty("cssClass", "status-text");
    save_row->addWidget(save_icon);

    save_label_ = new QLabel(QString::fromStdString(save_dir_), this);
    save_label_->setProperty("cssClass", "subtitle-text");
    save_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    save_row->addWidget(save_label_, 1);

    auto* change_btn = new QPushButton("Change", this);
    change_btn->setProperty("cssClass", "flat");
    connect(change_btn, &QPushButton::clicked, this, &DeviceListPanel::on_change_save_dir);
    save_row->addWidget(change_btn);

    layout->addLayout(save_row);

    // Device list
    list_widget_ = new QListWidget(this);
    list_widget_->setMinimumHeight(200);
    list_widget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(list_widget_, &QListWidget::itemClicked, this, &DeviceListPanel::on_device_row_clicked);
    layout->addWidget(list_widget_, 1);

    // Manual fallback
    auto* hint_label = new QLabel(QString::fromUtf8("💡 Can't find your device?"), this);
    hint_label->setProperty("cssClass", "subtitle-text");
    layout->addWidget(hint_label);

    auto* manual_btn = new QPushButton(QString::fromUtf8("🔗 Connect by IP"), this);
    manual_btn->setProperty("cssClass", "flat");
    connect(manual_btn, &QPushButton::clicked, this, &DeviceListPanel::on_manual_connect);
    layout->addWidget(manual_btn);

    // Status section
    auto* section = new QFrame(this);
    section->setProperty("cssClass", "section-box");
    auto* section_layout = new QVBoxLayout(section);
    section_layout->setContentsMargins(16, 16, 16, 16);
    section_layout->setSpacing(4);

    status_label_ = new QLabel("", section);
    status_label_->setProperty("cssClass", "status-text");
    status_label_->setWordWrap(true);
    section_layout->addWidget(status_label_);

    progress_bar_ = new QProgressBar(section);
    progress_bar_->setRange(0, 1000);
    progress_bar_->setValue(0);
    progress_bar_->setVisible(false);
    section_layout->addWidget(progress_bar_);

    progress_label_ = new QLabel("", section);
    progress_label_->setProperty("cssClass", "status-text");
    section_layout->addWidget(progress_label_);

    cancel_button_ = new QPushButton(QString::fromUtf8("⏹ Cancel Transfer"), section);
    cancel_button_->setProperty("cssClass", "destructive-action");
    cancel_button_->setVisible(false);
    connect(cancel_button_, &QPushButton::clicked, this, &DeviceListPanel::on_cancel_transfer);
    section_layout->addWidget(cancel_button_);

    layout->addWidget(section);
}

DeviceListPanel::~DeviceListPanel() {
    FD_LOG("~DeviceListPanel — cleaning up");
    g_recv_gen++;
    stop_discovery();
    if (transferring_) {
        FD_LOG("~DeviceListPanel — cancelling active transfer (blocking)");
        fd_cancel_client();
        transferring_ = false;
    }
    FD_LOG("~DeviceListPanel — done");
}

void DeviceListPanel::start_discovery() {
    FD_LOG("Starting device discovery");
    static DeviceListPanel* g_panel;
    g_panel = this;
    fd_start_discovery(482913, [](const fd_device_t* dev) {
        if (!dev) return;
        networking::DiscoveredDevice cpp_dev;
        cpp_dev.ip = dev->ip;
        cpp_dev.port = dev->port;
        cpp_dev.session_id = dev->session_id;
        g_panel->on_device_found(cpp_dev);
    });
}

void DeviceListPanel::stop_discovery() {
    FD_LOG("Stopping device discovery");
    fd_stop_discovery();
}

void DeviceListPanel::on_device_found(const networking::DiscoveredDevice& device) {
    std::string key = device.ip + ":" + std::to_string(device.port);
    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        if (devices_.count(key)) return;
        devices_[key] = device;
    }

    FD_LOG("Device found: " << key);

    // Marshal to UI thread — equivalent of g_idle_add(add_device_row_idle, ...)
    auto dev_copy = device;
    QMetaObject::invokeMethod(this, [this, dev_copy]() {
        QString text = QString::fromUtf8("💻  FluxDrop Device — ") +
                       QString::fromStdString(dev_copy.ip) +
                       " — Room " + QString::number(dev_copy.session_id) +
                       "  →";

        auto* item = new QListWidgetItem(text, list_widget_);
        item->setData(Qt::UserRole, QString::fromStdString(dev_copy.ip));
        item->setData(Qt::UserRole + 1, dev_copy.port);
        item->setData(Qt::UserRole + 2, dev_copy.session_id);
        item->setSizeHint(QSize(0, 50));
    }, Qt::QueuedConnection);
}

void DeviceListPanel::clear_and_restart_discovery() {
    FD_LOG("Clearing stale device list and restarting discovery");
    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        devices_.clear();
    }
    QMetaObject::invokeMethod(this, [this]() {
        list_widget_->clear();
    }, Qt::QueuedConnection);
    stop_discovery();
    start_discovery();
}

void DeviceListPanel::on_device_row_clicked(QListWidgetItem* item) {
    if (transferring_) {
        FD_WARN("Row activated but transfer already in progress");
        return;
    }

    networking::DiscoveredDevice device;
    device.ip = item->data(Qt::UserRole).toString().toStdString();
    device.port = static_cast<unsigned short>(item->data(Qt::UserRole + 1).toInt());
    device.session_id = item->data(Qt::UserRole + 2).toUInt();

    FD_LOG("Device selected: " << device.ip << ":" << device.port);
    connect_to_device(device);
}

void DeviceListPanel::connect_to_device(const networking::DiscoveredDevice& device) {
    FD_LOG("Opening PIN dialog for " << device.ip);

    auto* dialog = new QDialog(window());
    dialog->setWindowTitle("Enter PIN");
    dialog->resize(320, 180);
    dialog->setModal(true);

    auto* vbox = new QVBoxLayout(dialog);
    vbox->setContentsMargins(24, 24, 24, 24);
    vbox->setSpacing(12);

    auto* prompt = new QLabel(("Connect to " + device.ip).c_str(), dialog);
    prompt->setProperty("cssClass", "title-text");
    vbox->addWidget(prompt);

    auto* entry = new QLineEdit(dialog);
    entry->setPlaceholderText("Enter 4-digit PIN");
    entry->setMaxLength(4);
    vbox->addWidget(entry);

    auto* connect_btn = new QPushButton("Connect", dialog);
    connect_btn->setProperty("cssClass", "suggested-action");
    vbox->addWidget(connect_btn);

    auto dev_copy = device;
    auto save_dir_copy = save_dir_;

    connect(connect_btn, &QPushButton::clicked, dialog, [this, dialog, entry, dev_copy, save_dir_copy]() {
        std::string pin = entry->text().toStdString();
        if (pin.empty()) {
            FD_WARN("Connect clicked but PIN is empty");
            return;
        }

        FD_LOG("Connecting to " << dev_copy.ip << ":" << dev_copy.port << " with PIN");

        dialog->close();
        dialog->deleteLater();

        g_recv_gen++;
        uint64_t gen = g_recv_gen.load();

        transferring_ = true;
        progress_bar_->setVisible(true);
        cancel_button_->setVisible(true);
        progress_bar_->setValue(0);
        status_label_->setText("Connecting...");
        progress_label_->setText("");

        g_client_panel = this;

        auto status_cb = [](const char* msg) {
            FD_LOG("Client status: " << msg);
            uint64_t gen = g_recv_gen.load();
            QString text = QString::fromUtf8(msg);
            QMetaObject::invokeMethod(g_client_panel->status_label_, [gen, text]() {
                if (gen != g_recv_gen.load()) return;
                g_client_panel->status_label_->setText(text);
            }, Qt::QueuedConnection);
        };

        auto error_cb = [](const char* err) {
            FD_ERR("Client error: " << err);
            uint64_t gen = g_recv_gen.load();
            g_client_panel->transferring_ = false;
            // Truncate verbose Boost error strings for user display
            std::string msg(err);
            auto bracket = msg.find('[');
            if (bracket != std::string::npos && bracket > 0) {
                msg = msg.substr(0, bracket);
                while (!msg.empty() && (msg.back() == ' ' || msg.back() == ':')) msg.pop_back();
            }
            QString error_text = "Error: " + QString::fromStdString(msg);
            QMetaObject::invokeMethod(g_client_panel->status_label_, [gen, error_text]() {
                if (gen != g_recv_gen.load()) return;
                g_client_panel->status_label_->setText(error_text);
                g_client_panel->progress_bar_->setValue(1000);
                g_client_panel->cancel_button_->setVisible(false);
            }, Qt::QueuedConnection);
            g_client_panel->clear_and_restart_discovery();
        };

        auto file_request_cb = [](const char* filename, uint64_t size) -> bool {
            FD_LOG("File request: " << filename << " (" << size << " bytes)");
            std::promise<bool> prom;
            auto fut = prom.get_future();

            QString fname = QString::fromUtf8(filename);
            double size_mb = static_cast<double>(size) / (1024.0 * 1024.0);
            QString prompt_text = QString("Accept incoming file?\n\n%1\n%2 MB")
                                      .arg(fname)
                                      .arg(size_mb, 0, 'f', 1);
            std::promise<bool>* prom_ptr = &prom;

            QMetaObject::invokeMethod(g_client_panel, [prom_ptr, prompt_text]() {
                auto* dialog = new QDialog(g_client_panel->window());
                dialog->setWindowTitle("Incoming File");
                dialog->resize(350, 150);
                dialog->setModal(true);

                auto* vbox = new QVBoxLayout(dialog);
                vbox->setContentsMargins(24, 24, 24, 24);
                vbox->setSpacing(12);

                auto* label = new QLabel(prompt_text, dialog);
                label->setProperty("cssClass", "title-text");
                vbox->addWidget(label);

                auto* hbox = new QHBoxLayout();
                hbox->setAlignment(Qt::AlignCenter);

                auto* reject_btn = new QPushButton("Reject", dialog);
                reject_btn->setProperty("cssClass", "destructive-action");
                hbox->addWidget(reject_btn);

                auto* accept_btn = new QPushButton("Accept", dialog);
                accept_btn->setProperty("cssClass", "suggested-action");
                hbox->addWidget(accept_btn);

                vbox->addLayout(hbox);

                bool* answered = new bool(false);

                QObject::connect(accept_btn, &QPushButton::clicked, dialog, [dialog, prom_ptr, answered]() {
                    if (!*answered) {
                        FD_LOG("File request: ACCEPTED");
                        prom_ptr->set_value(true);
                        *answered = true;
                    }
                    dialog->close();
                });

                QObject::connect(reject_btn, &QPushButton::clicked, dialog, [dialog, prom_ptr, answered]() {
                    if (!*answered) {
                        FD_LOG("File request: REJECTED");
                        prom_ptr->set_value(false);
                        *answered = true;
                    }
                    dialog->close();
                });

                QObject::connect(dialog, &QDialog::destroyed, [prom_ptr, answered]() {
                    if (!*answered) {
                        FD_WARN("File request dialog destroyed without answer — rejecting");
                        prom_ptr->set_value(false);
                        *answered = true;
                    }
                    delete answered;
                });

                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->show();
            }, Qt::QueuedConnection);

            while (fut.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready) {
                if (!g_client_panel->transferring_) {
                    FD_WARN("File request interrupted by cancel — rejecting");
                    return false;
                }
            }
            return fut.get();
        };

        auto progress_cb = [](const char* filename, uint64_t transferred, uint64_t total, double speed) {
            double frac = (total > 0) ? (static_cast<double>(transferred) / total) : 0.0;
            int pct = static_cast<int>(frac * 100);
            char buf[128];
            snprintf(buf, sizeof(buf), "%d%% — %.1f MB/s — %s", pct, speed, filename);
            uint64_t gen = g_recv_gen.load();
            int value = static_cast<int>(frac * 1000);
            QString text = QString::fromUtf8(buf);
            QMetaObject::invokeMethod(g_client_panel->progress_bar_, [gen, value]() {
                if (gen != g_recv_gen.load()) return;
                g_client_panel->progress_bar_->setValue(value);
            }, Qt::QueuedConnection);
            QMetaObject::invokeMethod(g_client_panel->progress_label_, [gen, text]() {
                if (gen != g_recv_gen.load()) return;
                g_client_panel->progress_label_->setText(text);
            }, Qt::QueuedConnection);
        };

        auto complete_cb = []() {
            FD_LOG("Client transfer complete");
            uint64_t gen = g_recv_gen.load();
            g_client_panel->transferring_ = false;
            QMetaObject::invokeMethod(g_client_panel->status_label_, [gen]() {
                if (gen != g_recv_gen.load()) return;
                g_client_panel->status_label_->setText(QString::fromUtf8("✅ All files received!"));
                g_client_panel->progress_bar_->setValue(1000);
                g_client_panel->cancel_button_->setVisible(false);
            }, Qt::QueuedConnection);
            g_client_panel->clear_and_restart_discovery();
        };

        fd_connect(dev_copy.ip.c_str(), dev_copy.port, pin.c_str(), save_dir_copy.c_str(),
                   status_cb, error_cb, file_request_cb, progress_cb, complete_cb);
    });

    dialog->show();
}

void DeviceListPanel::on_manual_connect() {
    if (transferring_) return;

    auto* dialog = new QDialog(window());
    dialog->setWindowTitle("Connect by IP");
    dialog->resize(340, 200);
    dialog->setModal(true);

    auto* vbox = new QVBoxLayout(dialog);
    vbox->setContentsMargins(24, 24, 24, 24);
    vbox->setSpacing(12);

    auto* title = new QLabel("Enter the sender's IP and port", dialog);
    title->setProperty("cssClass", "title-text");
    vbox->addWidget(title);

    auto* ip_entry = new QLineEdit(dialog);
    ip_entry->setPlaceholderText("IP address (e.g. 192.168.43.1)");
    vbox->addWidget(ip_entry);

    auto* port_entry = new QLineEdit(dialog);
    port_entry->setPlaceholderText("Port (shown on sender)");
    vbox->addWidget(port_entry);

    auto* connect_btn = new QPushButton("Connect", dialog);
    connect_btn->setProperty("cssClass", "suggested-action");
    vbox->addWidget(connect_btn);

    connect(connect_btn, &QPushButton::clicked, dialog, [this, dialog, ip_entry, port_entry]() {
        std::string ip = ip_entry->text().toStdString();
        std::string port_str = port_entry->text().toStdString();

        if (ip.empty() || port_str.empty()) return;

        networking::DiscoveredDevice device;
        device.ip = ip;
        device.port = static_cast<unsigned short>(std::stoi(port_str));
        device.session_id = 0;

        dialog->close();
        dialog->deleteLater();
        connect_to_device(device);
    });

    dialog->show();
}

void DeviceListPanel::on_change_save_dir() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Save Folder",
                                                     QString::fromStdString(save_dir_));
    if (!dir.isEmpty()) {
        save_dir_ = dir.toStdString();
        save_label_->setText(dir);
        FD_LOG("Save directory changed to: " << save_dir_);
    }
}

void DeviceListPanel::on_cancel_transfer() {
    FD_LOG("Cancel transfer button clicked — non-blocking cancel");

    g_recv_gen++;
    transferring_ = false;

    fd_request_cancel_client();

    cancel_button_->setVisible(false);
    progress_bar_->setVisible(false);
    status_label_->setText("Transfer cancelled.");
    progress_label_->setText("");
}

} // namespace ui
