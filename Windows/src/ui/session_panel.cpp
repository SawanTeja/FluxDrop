#include "ui/session_panel.hpp"
#include "logger.hpp"
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMimeData>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStyle>
#include <QUrl>
#include <algorithm>
#include <filesystem>
#include <future>
#include <thread>

namespace fs = std::filesystem;

namespace ui {

// Generation counter to discard stale callbacks after disconnect/cancel
static std::atomic<uint64_t> g_session_gen{0};

// Static context for C callbacks (same pattern as existing panels)
static SessionPanel* g_active_panel = nullptr;
static QLabel* g_s_pin_lbl = nullptr;
static QLabel* g_s_host_status_lbl = nullptr;
static QLabel* g_s_session_status_lbl = nullptr;
static QLabel* g_s_session_peer_lbl = nullptr;
static QProgressBar* g_s_progress_br = nullptr;
static QLabel* g_s_progress_lbl = nullptr;

SessionPanel::SessionPanel(QWidget* parent) : QWidget(parent) {

    FD_LOG("SessionPanel created");
    setAcceptDrops(true);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setContentsMargins(0, 0, 0, 0);

    stack_ = new QStackedWidget(this);
    main_layout->addWidget(stack_);

    setup_pre_session_page();
    setup_hosting_page();
    setup_session_page();

    stack_->addWidget(pre_session_page_);
    stack_->addWidget(hosting_page_);
    stack_->addWidget(session_page_);

    show_pre_session();
}

SessionPanel::~SessionPanel() {
    FD_LOG("~SessionPanel — cleaning up");
    g_session_gen++;
    stop_discovery();
    fd_session_disconnect();
    FD_LOG("~SessionPanel — done");
}

// ── Page 0: Pre-session ──

void SessionPanel::setup_pre_session_page() {
    pre_session_page_ = new QWidget(this);
    auto* layout = new QVBoxLayout(pre_session_page_);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // Title
    auto* title = new QLabel(QString::fromUtf8("🔗 FluxDrop Session"), pre_session_page_);
    title->setProperty("cssClass", "title-text");
    layout->addWidget(title);

    auto* subtitle = new QLabel("Host a session or join an existing one", pre_session_page_);
    subtitle->setProperty("cssClass", "subtitle-text");
    layout->addWidget(subtitle);

    // Host / Join buttons
    auto* btn_row = new QHBoxLayout();
    btn_row->setAlignment(Qt::AlignCenter);

    host_button_ = new QPushButton(QString::fromUtf8("🖥️ Host Session"), pre_session_page_);
    host_button_->setProperty("cssClass", "suggested-action");
    host_button_->setMinimumHeight(44);
    connect(host_button_, &QPushButton::clicked, this, &SessionPanel::on_host_clicked);
    btn_row->addWidget(host_button_);

    join_button_ = new QPushButton(QString::fromUtf8("🔗 Join Session"), pre_session_page_);
    join_button_->setProperty("cssClass", "suggested-action");
    join_button_->setMinimumHeight(44);
    connect(join_button_, &QPushButton::clicked, this, &SessionPanel::on_join_clicked);
    btn_row->addWidget(join_button_);

    layout->addLayout(btn_row);

    // Save folder
    QString downloads_dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (!downloads_dir.isEmpty() && QDir(downloads_dir).exists()) {
        save_dir_ = downloads_dir.toStdString();
    } else {
        save_dir_ = QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toStdString();
    }

    auto* save_row = new QHBoxLayout();
    auto* save_icon = new QLabel(QString::fromUtf8("📂 Save to:"), pre_session_page_);
    save_icon->setProperty("cssClass", "status-text");
    save_row->addWidget(save_icon);

    save_label_ = new QLabel(QString::fromStdString(save_dir_), pre_session_page_);
    save_label_->setProperty("cssClass", "subtitle-text");
    save_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    save_row->addWidget(save_label_, 1);

    auto* change_btn = new QPushButton("Change", pre_session_page_);
    change_btn->setProperty("cssClass", "flat");
    connect(change_btn, &QPushButton::clicked, this, &SessionPanel::on_change_save_dir);
    save_row->addWidget(change_btn);

    layout->addLayout(save_row);

    // Discovery header
    discovery_label_ = new QLabel(QString::fromUtf8("📡 Nearby Hosts"), pre_session_page_);
    discovery_label_->setProperty("cssClass", "status-text");
    layout->addWidget(discovery_label_);

    // Device list
    device_list_ = new QListWidget(pre_session_page_);
    device_list_->setMinimumHeight(150);
    device_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(device_list_, &QListWidget::itemClicked, this, &SessionPanel::on_device_row_clicked);
    layout->addWidget(device_list_, 1);

    // Manual connect
    auto* hint = new QLabel(QString::fromUtf8("💡 Can't find your device?"), pre_session_page_);
    hint->setProperty("cssClass", "subtitle-text");
    layout->addWidget(hint);

    auto* manual_btn = new QPushButton(QString::fromUtf8("🔗 Connect by IP"), pre_session_page_);
    manual_btn->setProperty("cssClass", "flat");
    connect(manual_btn, &QPushButton::clicked, this, &SessionPanel::on_manual_connect);
    layout->addWidget(manual_btn);
}

// ── Page 1: Hosting (waiting for guest) ──

void SessionPanel::setup_hosting_page() {
    hosting_page_ = new QWidget(this);
    auto* layout = new QVBoxLayout(hosting_page_);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    layout->setAlignment(Qt::AlignCenter);

    auto* title = new QLabel(QString::fromUtf8("🖥️ Hosting Session"), hosting_page_);
    title->setProperty("cssClass", "title-text");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    pin_label_ = new QLabel("", hosting_page_);
    pin_label_->setProperty("cssClass", "pin-display");
    pin_label_->setAlignment(Qt::AlignCenter);
    layout->addWidget(pin_label_);

    host_status_label_ = new QLabel("Starting...", hosting_page_);
    host_status_label_->setProperty("cssClass", "status-text");
    host_status_label_->setAlignment(Qt::AlignCenter);
    host_status_label_->setWordWrap(true);
    layout->addWidget(host_status_label_);

    cancel_host_button_ = new QPushButton(QString::fromUtf8("⏹ Cancel"), hosting_page_);
    cancel_host_button_->setProperty("cssClass", "destructive-action");
    connect(cancel_host_button_, &QPushButton::clicked, this, &SessionPanel::on_cancel_host_clicked);
    layout->addWidget(cancel_host_button_, 0, Qt::AlignCenter);
}

// ── Page 2: In-session ──

void SessionPanel::setup_session_page() {
    session_page_ = new QWidget(this);
    auto* layout = new QVBoxLayout(session_page_);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // Peer info
    session_peer_label_ = new QLabel("", session_page_);
    session_peer_label_->setProperty("cssClass", "title-text");
    layout->addWidget(session_peer_label_);

    // Drop zone
    drop_area_ = new QFrame(session_page_);
    drop_area_->setProperty("cssClass", "drop-zone");
    auto* drop_layout = new QVBoxLayout(drop_area_);
    drop_layout->setAlignment(Qt::AlignCenter);

    drop_label_ = new QLabel(QString::fromUtf8("🗂️  Drag & Drop Files Here"), drop_area_);
    drop_label_->setProperty("cssClass", "title-text");
    drop_label_->setAlignment(Qt::AlignCenter);
    drop_layout->addWidget(drop_label_);

    auto* sub_label = new QLabel("to send to peer", drop_area_);
    sub_label->setProperty("cssClass", "subtitle-text");
    sub_label->setAlignment(Qt::AlignCenter);
    drop_layout->addWidget(sub_label);

    layout->addWidget(drop_area_);

    // File picker buttons
    auto* btn_box = new QHBoxLayout();
    btn_box->setAlignment(Qt::AlignCenter);

    choose_file_button_ = new QPushButton(QString::fromUtf8("📄 Choose Files"), session_page_);
    choose_file_button_->setProperty("cssClass", "suggested-action");
    connect(choose_file_button_, &QPushButton::clicked, this, &SessionPanel::on_choose_files_clicked);
    btn_box->addWidget(choose_file_button_);

    choose_folder_button_ = new QPushButton(QString::fromUtf8("📁 Choose Folder"), session_page_);
    choose_folder_button_->setProperty("cssClass", "suggested-action");
    connect(choose_folder_button_, &QPushButton::clicked, this, &SessionPanel::on_choose_folder_clicked);
    btn_box->addWidget(choose_folder_button_);

    layout->addLayout(btn_box);

    // File list
    file_list_widget_ = new QListWidget(session_page_);
    file_list_widget_->setSelectionMode(QAbstractItemView::NoSelection);
    file_list_widget_->setMinimumHeight(80);
    file_list_widget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(file_list_widget_, 1);

    // Send / Clear / Disconnect row
    auto* action_box = new QHBoxLayout();
    action_box->setAlignment(Qt::AlignCenter);

    send_button_ = new QPushButton("Send Files", session_page_);
    send_button_->setProperty("cssClass", "suggested-action");
    send_button_->setEnabled(false);
    connect(send_button_, &QPushButton::clicked, this, &SessionPanel::on_send_files_clicked);
    action_box->addWidget(send_button_);

    clear_button_ = new QPushButton(QString::fromUtf8("🗑️ Clear"), session_page_);
    clear_button_->setProperty("cssClass", "flat");
    connect(clear_button_, &QPushButton::clicked, this, &SessionPanel::on_clear_files_clicked);
    action_box->addWidget(clear_button_);

    disconnect_button_ = new QPushButton(QString::fromUtf8("⏹ Disconnect"), session_page_);
    disconnect_button_->setProperty("cssClass", "destructive-action");
    connect(disconnect_button_, &QPushButton::clicked, this, &SessionPanel::on_disconnect_clicked);
    action_box->addWidget(disconnect_button_);

    layout->addLayout(action_box);

    // Status section
    auto* section = new QFrame(session_page_);
    section->setProperty("cssClass", "section-box");
    auto* section_layout = new QVBoxLayout(section);
    section_layout->setContentsMargins(16, 16, 16, 16);
    section_layout->setSpacing(4);

    session_status_label_ = new QLabel("", section);
    session_status_label_->setProperty("cssClass", "status-text");
    session_status_label_->setWordWrap(true);
    section_layout->addWidget(session_status_label_);

    progress_bar_ = new QProgressBar(section);
    progress_bar_->setRange(0, 1000);
    progress_bar_->setValue(0);
    progress_bar_->setVisible(false);
    section_layout->addWidget(progress_bar_);

    progress_label_ = new QLabel("", section);
    progress_label_->setProperty("cssClass", "status-text");
    section_layout->addWidget(progress_label_);

    layout->addWidget(section);
}

// ── Navigation ──

void SessionPanel::show_pre_session() {
    stack_->setCurrentWidget(pre_session_page_);
    start_discovery();
}

void SessionPanel::show_hosting() {
    stack_->setCurrentWidget(hosting_page_);
}

void SessionPanel::show_session(const std::string& peer_ip) {
    session_peer_label_->setText(QString::fromUtf8("✅ Connected to ") + QString::fromStdString(peer_ip));
    session_status_label_->setText("Session active — you can send or receive files.");
    progress_bar_->setVisible(false);
    progress_bar_->setValue(0);
    progress_label_->setText("");
    queued_files_.clear();
    update_file_list_ui();
    stack_->setCurrentWidget(session_page_);
}

// ── Discovery ──

void SessionPanel::start_discovery() {
    FD_LOG("SessionPanel — starting discovery");
    static SessionPanel* g_disc_panel;
    g_disc_panel = this;
    fd_start_discovery(482913, [](const fd_device_t* dev) {
        if (!dev) return;
        networking::DiscoveredDevice cpp_dev;
        cpp_dev.ip = dev->ip;
        cpp_dev.port = dev->port;
        cpp_dev.session_id = dev->session_id;
        g_disc_panel->on_device_found(cpp_dev);
    });
}

void SessionPanel::stop_discovery() {
    FD_LOG("SessionPanel — stopping discovery");
    fd_stop_discovery();
}

void SessionPanel::on_device_found(const networking::DiscoveredDevice& device) {
    std::string key = device.ip + ":" + std::to_string(device.port);
    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        if (devices_.count(key)) return;
        devices_[key] = device;
    }

    FD_LOG("Device found: " << key);

    auto dev_copy = device;
    QMetaObject::invokeMethod(
        this,
        [this, dev_copy]() {
            QString text = QString::fromUtf8("💻  FluxDrop Host — ") + QString::fromStdString(dev_copy.ip) +
                           ":" + QString::number(dev_copy.port) + "  →";
            auto* item = new QListWidgetItem(text, device_list_);
            item->setData(Qt::UserRole, QString::fromStdString(dev_copy.ip));
            item->setData(Qt::UserRole + 1, dev_copy.port);
            item->setData(Qt::UserRole + 2, dev_copy.session_id);
            item->setSizeHint(QSize(0, 50));
        },
        Qt::QueuedConnection);
}

void SessionPanel::on_device_row_clicked(QListWidgetItem* item) {
    if (session_active_) return;

    networking::DiscoveredDevice device;
    device.ip = item->data(Qt::UserRole).toString().toStdString();
    device.port = static_cast<unsigned short>(item->data(Qt::UserRole + 1).toInt());
    device.session_id = item->data(Qt::UserRole + 2).toUInt();

    FD_LOG("Device selected: " << device.ip << ":" << device.port);
    connect_to_device(device);
}

void SessionPanel::connect_to_device(const networking::DiscoveredDevice& device) {
    FD_LOG("Opening PIN dialog for " << device.ip);

    auto* dialog = new QDialog(window());
    dialog->setWindowTitle("Enter PIN");
    dialog->resize(320, 180);
    dialog->setModal(true);

    auto* vbox = new QVBoxLayout(dialog);
    vbox->setContentsMargins(24, 24, 24, 24);
    vbox->setSpacing(12);

    auto* prompt = new QLabel(("Join " + device.ip).c_str(), dialog);
    prompt->setProperty("cssClass", "title-text");
    vbox->addWidget(prompt);

    auto* entry = new QLineEdit(dialog);
    entry->setPlaceholderText("Enter 4-digit PIN");
    entry->setMaxLength(4);
    vbox->addWidget(entry);

    auto* connect_btn = new QPushButton("Join Session", dialog);
    connect_btn->setProperty("cssClass", "suggested-action");
    vbox->addWidget(connect_btn);

    auto dev_copy = device;
    auto save_dir_copy = save_dir_;

    connect(connect_btn, &QPushButton::clicked, dialog, [this, dialog, entry, dev_copy, save_dir_copy]() {
        std::string pin = entry->text().toStdString();
        if (pin.empty()) return;

        FD_LOG("Joining session at " << dev_copy.ip << ":" << dev_copy.port);

        dialog->close();
        dialog->deleteLater();

        stop_discovery();

        g_session_gen++;
        uint64_t gen = g_session_gen.load();

        g_active_panel = this;
        g_s_session_status_lbl = session_status_label_;
        g_s_session_peer_lbl = session_peer_label_;
        g_s_progress_br = progress_bar_;
        g_s_progress_lbl = progress_label_;

        fd_session_join(dev_copy.ip.c_str(), dev_copy.port, pin.c_str(), save_dir_copy.c_str(),
            // on_session_established
            [](const fd_session_info_t* info) {
                uint64_t gen = g_session_gen.load();
                std::string peer = info->peer_ip;
                QMetaObject::invokeMethod(g_active_panel,
                    [gen, peer]() {
                        if (gen != g_session_gen.load()) return;
                        g_active_panel->session_active_ = true;
                        g_active_panel->show_session(peer);
                    },
                    Qt::QueuedConnection);
            },
            // on_session_ended
            []() {
                uint64_t gen = g_session_gen.load();
                QMetaObject::invokeMethod(g_active_panel,
                    [gen]() {
                        if (gen != g_session_gen.load()) return;
                        g_active_panel->session_active_ = false;
                        g_active_panel->show_pre_session();
                    },
                    Qt::QueuedConnection);
            },
            // on_status
            [](const char* msg) {
                uint64_t gen = g_session_gen.load();
                QString text = QString::fromUtf8(msg);
                QMetaObject::invokeMethod(g_s_session_status_lbl,
                    [gen, text]() {
                        if (gen != g_session_gen.load()) return;
                        g_s_session_status_lbl->setText(text);
                    },
                    Qt::QueuedConnection);
            },
            // on_error
            [](const char* err) {
                uint64_t gen = g_session_gen.load();
                QString text = QString::fromUtf8("❌ ") + QString::fromUtf8(err);
                QMetaObject::invokeMethod(g_active_panel,
                    [gen, text]() {
                        if (gen != g_session_gen.load()) return;
                        g_s_session_status_lbl->setText(text);
                        g_active_panel->session_active_ = false;
                        g_active_panel->show_pre_session();
                    },
                    Qt::QueuedConnection);
            },
            // on_file_offer
            [](const char* filename, uint64_t size) -> bool {
                std::promise<bool> prom;
                auto fut = prom.get_future();

                QString fname = QString::fromUtf8(filename);
                double size_mb = static_cast<double>(size) / (1024.0 * 1024.0);
                QString prompt_text = QString("Accept incoming file?\n\n%1\n%2 MB").arg(fname).arg(size_mb, 0, 'f', 1);
                std::promise<bool>* prom_ptr = &prom;

                QMetaObject::invokeMethod(g_active_panel,
                    [prom_ptr, prompt_text]() {
                        auto* dialog = new QDialog(g_active_panel->window());
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
                                prom_ptr->set_value(true);
                                *answered = true;
                            }
                            dialog->close();
                        });

                        QObject::connect(reject_btn, &QPushButton::clicked, dialog, [dialog, prom_ptr, answered]() {
                            if (!*answered) {
                                prom_ptr->set_value(false);
                                *answered = true;
                            }
                            dialog->close();
                        });

                        QObject::connect(dialog, &QDialog::destroyed, [prom_ptr, answered]() {
                            if (!*answered) {
                                prom_ptr->set_value(false);
                                *answered = true;
                            }
                            delete answered;
                        });

                        dialog->setAttribute(Qt::WA_DeleteOnClose);
                        dialog->show();
                    },
                    Qt::QueuedConnection);

                while (fut.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready) {
                    if (!g_active_panel->session_active_) return false;
                }
                return fut.get();
            },
            // on_progress
            [](const char* filename, uint64_t transferred, uint64_t total, double speed) {
                double frac = (total > 0) ? (static_cast<double>(transferred) / total) : 0.0;
                int pct = static_cast<int>(frac * 100);
                char buf[128];
                snprintf(buf, sizeof(buf), "%d%% — %.1f MB/s — %s", pct, speed, filename);
                uint64_t gen = g_session_gen.load();
                int value = static_cast<int>(frac * 1000);
                QString text = QString::fromUtf8(buf);
                QMetaObject::invokeMethod(g_s_progress_br,
                    [gen, value]() {
                        if (gen != g_session_gen.load()) return;
                        g_s_progress_br->setVisible(true);
                        g_s_progress_br->setValue(value);
                    },
                    Qt::QueuedConnection);
                QMetaObject::invokeMethod(g_s_progress_lbl,
                    [gen, text]() {
                        if (gen != g_session_gen.load()) return;
                        g_s_progress_lbl->setText(text);
                    },
                    Qt::QueuedConnection);
            },
            // on_file_complete
            [](const char* filename) {
                uint64_t gen = g_session_gen.load();
                QString text = QString::fromUtf8("✅ ") + QString::fromUtf8(filename);
                QMetaObject::invokeMethod(g_s_session_status_lbl,
                    [gen, text]() {
                        if (gen != g_session_gen.load()) return;
                        g_s_session_status_lbl->setText(text);
                    },
                    Qt::QueuedConnection);
            }
        );
    });

    dialog->show();
}

// ── Host button ──

void SessionPanel::on_host_clicked() {
    FD_LOG("Host Session clicked");

    stop_discovery();
    show_hosting();

    g_session_gen++;
    uint64_t gen = g_session_gen.load();

    g_active_panel = this;
    g_s_pin_lbl = pin_label_;
    g_s_host_status_lbl = host_status_label_;
    g_s_session_status_lbl = session_status_label_;
    g_s_session_peer_lbl = session_peer_label_;
    g_s_progress_br = progress_bar_;
    g_s_progress_lbl = progress_label_;

    // Set save directory for host too
    fd_session_set_save_dir(save_dir_.c_str());

    fd_session_host(
        // on_ready
        [](const char* ip, int port, int pin) {
            uint64_t gen = g_session_gen.load();
            QString pin_text = "PIN: " + QString::number(pin);
            QString status_text = "Listening on " + QString(ip) + ":" + QString::number(port) + " — Waiting for guest...";
            QMetaObject::invokeMethod(g_s_pin_lbl,
                [gen, pin_text]() {
                    if (gen != g_session_gen.load()) return;
                    g_s_pin_lbl->setText(pin_text);
                },
                Qt::QueuedConnection);
            QMetaObject::invokeMethod(g_s_host_status_lbl,
                [gen, status_text]() {
                    if (gen != g_session_gen.load()) return;
                    g_s_host_status_lbl->setText(status_text);
                },
                Qt::QueuedConnection);
        },
        // on_session_established
        [](const fd_session_info_t* info) {
            uint64_t gen = g_session_gen.load();
            std::string peer = info->peer_ip;
            QMetaObject::invokeMethod(g_active_panel,
                [gen, peer]() {
                    if (gen != g_session_gen.load()) return;
                    g_active_panel->session_active_ = true;
                    g_active_panel->show_session(peer);
                },
                Qt::QueuedConnection);
        },
        // on_session_ended
        []() {
            uint64_t gen = g_session_gen.load();
            QMetaObject::invokeMethod(g_active_panel,
                [gen]() {
                    if (gen != g_session_gen.load()) return;
                    g_active_panel->session_active_ = false;
                    g_active_panel->show_pre_session();
                },
                Qt::QueuedConnection);
        },
        // on_status
        [](const char* msg) {
            uint64_t gen = g_session_gen.load();
            QString text = QString::fromUtf8(msg);
            // Route to host status if not yet in session, else session status
            QMetaObject::invokeMethod(g_active_panel,
                [gen, text]() {
                    if (gen != g_session_gen.load()) return;
                    if (g_active_panel->session_active_) {
                        g_s_session_status_lbl->setText(text);
                    } else {
                        g_s_host_status_lbl->setText(text);
                    }
                },
                Qt::QueuedConnection);
        },
        // on_error
        [](const char* err) {
            uint64_t gen = g_session_gen.load();
            QString text = QString::fromUtf8("❌ ") + QString::fromUtf8(err);
            QMetaObject::invokeMethod(g_active_panel,
                [gen, text]() {
                    if (gen != g_session_gen.load()) return;
                    if (g_active_panel->session_active_) {
                        g_s_session_status_lbl->setText(text);
                    } else {
                        g_s_host_status_lbl->setText(text);
                    }
                    g_active_panel->session_active_ = false;
                },
                Qt::QueuedConnection);
        },
        // on_file_offer (same dialog as join)
        [](const char* filename, uint64_t size) -> bool {
            std::promise<bool> prom;
            auto fut = prom.get_future();

            QString fname = QString::fromUtf8(filename);
            double size_mb = static_cast<double>(size) / (1024.0 * 1024.0);
            QString prompt_text = QString("Accept incoming file?\n\n%1\n%2 MB").arg(fname).arg(size_mb, 0, 'f', 1);
            std::promise<bool>* prom_ptr = &prom;

            QMetaObject::invokeMethod(g_active_panel,
                [prom_ptr, prompt_text]() {
                    auto* dialog = new QDialog(g_active_panel->window());
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
                            prom_ptr->set_value(true);
                            *answered = true;
                        }
                        dialog->close();
                    });

                    QObject::connect(reject_btn, &QPushButton::clicked, dialog, [dialog, prom_ptr, answered]() {
                        if (!*answered) {
                            prom_ptr->set_value(false);
                            *answered = true;
                        }
                        dialog->close();
                    });

                    QObject::connect(dialog, &QDialog::destroyed, [prom_ptr, answered]() {
                        if (!*answered) {
                            prom_ptr->set_value(false);
                            *answered = true;
                        }
                        delete answered;
                    });

                    dialog->setAttribute(Qt::WA_DeleteOnClose);
                    dialog->show();
                },
                Qt::QueuedConnection);

            while (fut.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready) {
                if (!g_active_panel->session_active_) return false;
            }
            return fut.get();
        },
        // on_progress
        [](const char* filename, uint64_t transferred, uint64_t total, double speed) {
            double frac = (total > 0) ? (static_cast<double>(transferred) / total) : 0.0;
            int pct = static_cast<int>(frac * 100);
            char buf[128];
            snprintf(buf, sizeof(buf), "%d%% — %.1f MB/s — %s", pct, speed, filename);
            uint64_t gen = g_session_gen.load();
            int value = static_cast<int>(frac * 1000);
            QString text = QString::fromUtf8(buf);
            QMetaObject::invokeMethod(g_s_progress_br,
                [gen, value]() {
                    if (gen != g_session_gen.load()) return;
                    g_s_progress_br->setVisible(true);
                    g_s_progress_br->setValue(value);
                },
                Qt::QueuedConnection);
            QMetaObject::invokeMethod(g_s_progress_lbl,
                [gen, text]() {
                    if (gen != g_session_gen.load()) return;
                    g_s_progress_lbl->setText(text);
                },
                Qt::QueuedConnection);
        },
        // on_file_complete
        [](const char* filename) {
            uint64_t gen = g_session_gen.load();
            QString text = QString::fromUtf8("✅ ") + QString::fromUtf8(filename);
            QMetaObject::invokeMethod(g_s_session_status_lbl,
                [gen, text]() {
                    if (gen != g_session_gen.load()) return;
                    g_s_session_status_lbl->setText(text);
                },
                Qt::QueuedConnection);
        }
    );
}

// ── Join (used for manual connect — device list clicks go through connect_to_device) ──

void SessionPanel::on_join_clicked() {
    FD_LOG("Join Session clicked — opening manual IP dialog");
    on_manual_connect();
}

// ── Cancel / Disconnect ──

void SessionPanel::on_cancel_host_clicked() {
    FD_LOG("Cancel host clicked");
    g_session_gen++;
    session_active_ = false;
    fd_session_disconnect();
    show_pre_session();
}

void SessionPanel::on_disconnect_clicked() {
    FD_LOG("Disconnect clicked");
    g_session_gen++;
    session_active_ = false;
    fd_session_disconnect();
    show_pre_session();
}

// ── File management ──

void SessionPanel::add_path(const std::string& path) {
    FD_LOG("Adding path: " << path);
    fs::path p = fs::path(path).lexically_normal();

    std::error_code ec;
    if (!fs::exists(p, ec)) return;

    const std::string normalized = p.string();
    if (std::find(queued_files_.begin(), queued_files_.end(), normalized) != queued_files_.end()) return;

    if (fs::is_directory(p, ec) || fs::is_regular_file(p, ec)) {
        queued_files_.push_back(normalized);
    }
    update_file_list_ui();
}

void SessionPanel::update_file_list_ui() {
    file_list_widget_->clear();

    for (const auto& file : queued_files_) {
        fs::path p(file);
        std::string display = "\xF0\x9F\x93\x84 " + p.filename().string();

        std::error_code ec;
        auto fsize = fs::file_size(file, ec);
        if (!ec) {
            double size = static_cast<double>(fsize);
            const char* units[] = {"B", "KB", "MB", "GB"};
            int i = 0;
            while (size >= 1024 && i < 3) {
                size /= 1024;
                i++;
            }
            char buf[64];
            snprintf(buf, sizeof(buf), " (%.1f %s)", size, units[i]);
            display += buf;
        }

        file_list_widget_->addItem(QString::fromStdString(display));
    }

    send_button_->setEnabled(!queued_files_.empty() && session_active_);
}

void SessionPanel::on_choose_files_clicked() {
    QStringList files = QFileDialog::getOpenFileNames(this, "Select Files");
    for (const QString& file : files) {
        add_path(file.toStdString());
    }
}

void SessionPanel::on_choose_folder_clicked() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Folder");
    if (!dir.isEmpty()) {
        add_path(dir.toStdString());
    }
}

void SessionPanel::on_clear_files_clicked() {
    queued_files_.clear();
    update_file_list_ui();
}

void SessionPanel::on_send_files_clicked() {
    if (queued_files_.empty() || !session_active_) return;

    FD_LOG("Sending " << queued_files_.size() << " file(s) via session");

    std::vector<const char*> c_paths;
    for (const auto& p : queued_files_) {
        c_paths.push_back(p.c_str());
    }

    session_status_label_->setText("Sending " + QString::number(queued_files_.size()) + " file(s)...");
    progress_bar_->setVisible(true);
    progress_bar_->setValue(0);

    fd_session_send_files(c_paths.data(), static_cast<int>(c_paths.size()));

    queued_files_.clear();
    update_file_list_ui();
}

// ── Drag & drop ──

void SessionPanel::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() && session_active_) {
        event->acceptProposedAction();
        if (drop_area_) {
            drop_area_->setProperty("cssClass", "drop-zone-active");
            drop_area_->style()->unpolish(drop_area_);
            drop_area_->style()->polish(drop_area_);
        }
    }
}

void SessionPanel::dragLeaveEvent(QDragLeaveEvent* /*event*/) {
    if (drop_area_) {
        drop_area_->setProperty("cssClass", "drop-zone");
        drop_area_->style()->unpolish(drop_area_);
        drop_area_->style()->polish(drop_area_);
    }
}

void SessionPanel::dropEvent(QDropEvent* event) {
    if (drop_area_) {
        drop_area_->setProperty("cssClass", "drop-zone");
        drop_area_->style()->unpolish(drop_area_);
        drop_area_->style()->polish(drop_area_);
    }

    const QMimeData* mime = event->mimeData();
    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (url.isLocalFile()) {
                add_path(url.toLocalFile().toStdString());
            }
        }
    }
}

// ── Manual connect ──

void SessionPanel::on_manual_connect() {
    auto* dialog = new QDialog(window());
    dialog->setWindowTitle("Connect by IP");
    dialog->resize(340, 220);
    dialog->setModal(true);

    auto* vbox = new QVBoxLayout(dialog);
    vbox->setContentsMargins(24, 24, 24, 24);
    vbox->setSpacing(12);

    auto* title = new QLabel("Enter the host's IP and port", dialog);
    title->setProperty("cssClass", "title-text");
    vbox->addWidget(title);

    auto* ip_entry = new QLineEdit(dialog);
    ip_entry->setPlaceholderText("IP address (e.g. 192.168.43.1)");
    vbox->addWidget(ip_entry);

    auto* port_entry = new QLineEdit(dialog);
    port_entry->setPlaceholderText("Port (shown on host)");
    vbox->addWidget(port_entry);

    auto* connect_btn = new QPushButton("Next", dialog);
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

void SessionPanel::on_change_save_dir() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Save Folder", QString::fromStdString(save_dir_));
    if (!dir.isEmpty()) {
        save_dir_ = dir.toStdString();
        save_label_->setText(dir);
        FD_LOG("Save directory changed to: " << save_dir_);
    }
}

} // namespace ui
