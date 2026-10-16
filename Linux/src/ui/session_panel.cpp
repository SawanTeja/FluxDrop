#include "ui/session_panel.hpp"
#include "logger.hpp"
#include <algorithm>
#include <filesystem>
#include <future>
#include <memory>
#include <thread>

namespace fs = std::filesystem;

namespace ui {

// Generation counter to discard stale callbacks after disconnect/cancel
static std::atomic<uint64_t> g_session_gen{0};

// Static context for C callbacks
static SessionPanel* g_active_panel = nullptr;
static GtkWidget* g_s_pin_lbl = nullptr;
static GtkWidget* g_s_host_status_lbl = nullptr;
static GtkWidget* g_s_session_status_lbl = nullptr;
static GtkWidget* g_s_session_peer_lbl = nullptr;
static GtkWidget* g_s_progress_br = nullptr;
static GtkWidget* g_s_progress_lbl = nullptr;
static bool g_auto_accept_incoming = false;

struct StatusTextData {
    uint64_t gen;
    std::string text;
};

struct HostReadyData {
    uint64_t gen;
    std::string pin_text;
    std::string status_text;
};

struct ProgressUpdateData {
    uint64_t gen;
    double fraction;
    std::string text;
};

SessionPanel::SessionPanel(GtkWindow* parent_window) : parent_window_(parent_window) {
    FD_LOG("SessionPanel created");

    main_box_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    stack_ = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack_), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(stack_), 300);
    gtk_widget_set_vexpand(stack_, TRUE);
    gtk_box_append(GTK_BOX(main_box_), stack_);

    // Set default download location
    const char* download_dir = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    if (download_dir && g_file_test(download_dir, G_FILE_TEST_IS_DIR)) {
        save_dir_ = download_dir;
    } else {
        const char* home_dir = g_get_home_dir();
        save_dir_ = home_dir ? home_dir : ".";
    }

    setup_pre_session_page();
    setup_hosting_page();
    setup_session_page();

    gtk_stack_add_named(GTK_STACK(stack_), pre_session_page_, "pre_session");
    gtk_stack_add_named(GTK_STACK(stack_), hosting_page_, "hosting");
    gtk_stack_add_named(GTK_STACK(stack_), session_page_, "session");

    show_pre_session();
}

SessionPanel::~SessionPanel() {
    FD_LOG("~SessionPanel — cleaning up");
    g_session_gen++;
    stop_discovery();
    fd_session_disconnect();
    if (g_active_panel == this) {
        g_active_panel = nullptr;
    }
    FD_LOG("~SessionPanel — done");
}

// ── Page 0: Pre-session ──

void SessionPanel::setup_pre_session_page() {
    pre_session_page_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(pre_session_page_, 12);
    gtk_widget_set_margin_end(pre_session_page_, 12);
    gtk_widget_set_margin_top(pre_session_page_, 12);
    gtk_widget_set_margin_bottom(pre_session_page_, 12);

    // Title
    GtkWidget* title = gtk_label_new("🔗 FluxDrop Session");
    gtk_widget_add_css_class(title, "title-text");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_append(GTK_BOX(pre_session_page_), title);

    // Subtitle
    GtkWidget* subtitle = gtk_label_new("Host a session or join an existing one");
    gtk_widget_add_css_class(subtitle, "subtitle-text");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0);
    gtk_box_append(GTK_BOX(pre_session_page_), subtitle);

    // Host / Join buttons
    GtkWidget* btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(btn_row, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(btn_row, 4);
    gtk_widget_set_margin_bottom(btn_row, 4);

    host_button_ = gtk_button_new_with_label("🖥️ Host Session");
    gtk_widget_add_css_class(host_button_, "suggested-action");
    gtk_widget_set_size_request(host_button_, -1, 44);
    g_signal_connect(host_button_, "clicked", G_CALLBACK(on_host_clicked), this);
    gtk_box_append(GTK_BOX(btn_row), host_button_);

    join_button_ = gtk_button_new_with_label("🔗 Join Session");
    gtk_widget_add_css_class(join_button_, "suggested-action");
    gtk_widget_set_size_request(join_button_, -1, 44);
    g_signal_connect(join_button_, "clicked", G_CALLBACK(on_join_clicked), this);
    gtk_box_append(GTK_BOX(btn_row), join_button_);

    gtk_box_append(GTK_BOX(pre_session_page_), btn_row);

    // Save folder row
    GtkWidget* save_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget* save_icon = gtk_label_new("📂 Save to:");
    gtk_widget_add_css_class(save_icon, "status-text");
    gtk_box_append(GTK_BOX(save_row), save_icon);

    save_label_ = gtk_label_new(save_dir_.c_str());
    gtk_widget_add_css_class(save_label_, "subtitle-text");
    gtk_label_set_xalign(GTK_LABEL(save_label_), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(save_label_), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(save_label_, TRUE);
    gtk_box_append(GTK_BOX(save_row), save_label_);

    GtkWidget* change_btn = gtk_button_new_with_label("Change");
    gtk_widget_add_css_class(change_btn, "flat");
    g_signal_connect(change_btn, "clicked", G_CALLBACK(on_change_save_dir), this);
    gtk_box_append(GTK_BOX(save_row), change_btn);

    gtk_box_append(GTK_BOX(pre_session_page_), save_row);

    // Discovery header
    discovery_label_ = gtk_label_new("📡 Nearby Hosts");
    gtk_widget_add_css_class(discovery_label_, "status-text");
    gtk_label_set_xalign(GTK_LABEL(discovery_label_), 0.0);
    gtk_box_append(GTK_BOX(pre_session_page_), discovery_label_);

    // Device list
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 150);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_add_css_class(scroll, "file-list-scroll");

    device_list_box_ = gtk_list_box_new();
    gtk_widget_add_css_class(device_list_box_, "file-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(device_list_box_), GTK_SELECTION_NONE);
    g_signal_connect(device_list_box_, "row-activated", G_CALLBACK(on_device_row_activated), this);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), device_list_box_);
    gtk_box_append(GTK_BOX(pre_session_page_), scroll);

    // Manual connect footer
    GtkWidget* hint = gtk_label_new("💡 Can't find your device?");
    gtk_widget_add_css_class(hint, "subtitle-text");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_box_append(GTK_BOX(pre_session_page_), hint);

    GtkWidget* manual_btn = gtk_button_new_with_label("🔗 Connect by IP");
    gtk_widget_add_css_class(manual_btn, "flat");
    g_signal_connect(manual_btn, "clicked", G_CALLBACK(on_manual_connect), this);
    gtk_box_append(GTK_BOX(pre_session_page_), manual_btn);
}

// ── Page 1: Hosting (waiting for guest) ──

void SessionPanel::setup_hosting_page() {
    hosting_page_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(hosting_page_, 12);
    gtk_widget_set_margin_end(hosting_page_, 12);
    gtk_widget_set_margin_top(hosting_page_, 12);
    gtk_widget_set_margin_bottom(hosting_page_, 12);
    gtk_widget_set_halign(hosting_page_, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(hosting_page_, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(hosting_page_, TRUE);

    GtkWidget* title = gtk_label_new("🖥️ Hosting Session");
    gtk_widget_add_css_class(title, "title-text");
    gtk_box_append(GTK_BOX(hosting_page_), title);

    pin_label_ = gtk_label_new("");
    gtk_widget_add_css_class(pin_label_, "pin-display");
    gtk_box_append(GTK_BOX(hosting_page_), pin_label_);

    host_status_label_ = gtk_label_new("Starting...");
    gtk_widget_add_css_class(host_status_label_, "status-text");
    gtk_label_set_wrap(GTK_LABEL(host_status_label_), TRUE);
    gtk_label_set_justify(GTK_LABEL(host_status_label_), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(hosting_page_), host_status_label_);

    cancel_host_button_ = gtk_button_new_with_label("⏹ Cancel");
    gtk_widget_add_css_class(cancel_host_button_, "destructive-action");
    g_signal_connect(cancel_host_button_, "clicked", G_CALLBACK(on_cancel_host_clicked), this);
    gtk_box_append(GTK_BOX(hosting_page_), cancel_host_button_);
}

// ── Page 2: In-session ──

void SessionPanel::setup_session_page() {
    session_page_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(session_page_, 12);
    gtk_widget_set_margin_end(session_page_, 12);
    gtk_widget_set_margin_top(session_page_, 12);
    gtk_widget_set_margin_bottom(session_page_, 12);

    // Peer info
    session_peer_label_ = gtk_label_new("");
    gtk_widget_add_css_class(session_peer_label_, "title-text");
    gtk_label_set_xalign(GTK_LABEL(session_peer_label_), 0.0);
    gtk_box_append(GTK_BOX(session_page_), session_peer_label_);

    // Drop zone
    drop_area_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(drop_area_, "drop-zone");
    gtk_widget_set_halign(drop_area_, GTK_ALIGN_FILL);

    drop_label_ = gtk_label_new("🗂️  Drag & Drop Files Here");
    gtk_widget_add_css_class(drop_label_, "title-text");
    gtk_box_append(GTK_BOX(drop_area_), drop_label_);

    GtkWidget* sub_label = gtk_label_new("to send to peer");
    gtk_widget_add_css_class(sub_label, "subtitle-text");
    gtk_box_append(GTK_BOX(drop_area_), sub_label);

    gtk_box_append(GTK_BOX(session_page_), drop_area_);

    // Drag & drop controller
    GtkDropTarget* drop_target = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(drop_target, "drop", G_CALLBACK(on_drag_drop), this);
    g_signal_connect(drop_target, "enter", G_CALLBACK(on_drag_enter), this);
    g_signal_connect(drop_target, "leave", G_CALLBACK(on_drag_leave), this);
    gtk_widget_add_controller(drop_area_, GTK_EVENT_CONTROLLER(drop_target));

    // File picker buttons
    GtkWidget* btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_CENTER);

    choose_file_button_ = gtk_button_new_with_label("📄 Choose Files");
    gtk_widget_add_css_class(choose_file_button_, "suggested-action");
    g_signal_connect(choose_file_button_, "clicked", G_CALLBACK(on_choose_files_clicked), this);
    gtk_box_append(GTK_BOX(btn_box), choose_file_button_);

    choose_folder_button_ = gtk_button_new_with_label("📁 Choose Folder");
    gtk_widget_add_css_class(choose_folder_button_, "suggested-action");
    g_signal_connect(choose_folder_button_, "clicked", G_CALLBACK(on_choose_folder_clicked), this);
    gtk_box_append(GTK_BOX(btn_box), choose_folder_button_);

    gtk_box_append(GTK_BOX(session_page_), btn_box);

    // File list
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 80);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_add_css_class(scroll, "file-list-scroll");

    file_list_box_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(file_list_box_), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(file_list_box_, "file-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), file_list_box_);
    gtk_box_append(GTK_BOX(session_page_), scroll);

    // Send / Clear / Disconnect row
    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(action_box, GTK_ALIGN_CENTER);

    send_button_ = gtk_button_new_with_label("Send Files");
    gtk_widget_add_css_class(send_button_, "suggested-action");
    gtk_widget_set_sensitive(send_button_, FALSE);
    g_signal_connect(send_button_, "clicked", G_CALLBACK(on_send_files_clicked), this);
    gtk_box_append(GTK_BOX(action_box), send_button_);

    clear_button_ = gtk_button_new_with_label("🗑️ Clear");
    gtk_widget_add_css_class(clear_button_, "flat");
    g_signal_connect(clear_button_, "clicked", G_CALLBACK(on_clear_files_clicked), this);
    gtk_box_append(GTK_BOX(action_box), clear_button_);

    disconnect_button_ = gtk_button_new_with_label("⏹ Disconnect");
    gtk_widget_add_css_class(disconnect_button_, "destructive-action");
    g_signal_connect(disconnect_button_, "clicked", G_CALLBACK(on_disconnect_clicked), this);
    gtk_box_append(GTK_BOX(action_box), disconnect_button_);

    gtk_box_append(GTK_BOX(session_page_), action_box);

    // Status section
    GtkWidget* section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(section, "section-box");

    session_status_label_ = gtk_label_new("");
    gtk_widget_add_css_class(session_status_label_, "status-text");
    gtk_label_set_wrap(GTK_LABEL(session_status_label_), TRUE);
    gtk_label_set_xalign(GTK_LABEL(session_status_label_), 0.0);
    gtk_box_append(GTK_BOX(section), session_status_label_);

    progress_bar_ = gtk_progress_bar_new();
    gtk_widget_set_visible(progress_bar_, FALSE);
    gtk_box_append(GTK_BOX(section), progress_bar_);

    progress_label_ = gtk_label_new("");
    gtk_widget_add_css_class(progress_label_, "status-text");
    gtk_label_set_xalign(GTK_LABEL(progress_label_), 0.0);
    gtk_box_append(GTK_BOX(section), progress_label_);

    gtk_box_append(GTK_BOX(session_page_), section);
}

// ── Navigation ──

void SessionPanel::show_pre_session() {
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), "pre_session");

    // Clear existing discovery list and reset discovered devices
    {
        std::lock_guard<std::mutex> lock(devices_mutex_);
        devices_.clear();
    }
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(device_list_box_)) != nullptr) {
        gtk_list_box_remove(GTK_LIST_BOX(device_list_box_), child);
    }

    start_discovery();
}

void SessionPanel::show_hosting() {
    gtk_label_set_text(GTK_LABEL(pin_label_), "");
    gtk_label_set_text(GTK_LABEL(host_status_label_), "Starting...");
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), "hosting");
}

void SessionPanel::show_session(const std::string& peer_ip) {
    std::string title = "✅ Connected to " + peer_ip;
    gtk_label_set_text(GTK_LABEL(session_peer_label_), title.c_str());
    gtk_label_set_text(GTK_LABEL(session_status_label_), "Session active — you can send or receive files.");
    gtk_widget_set_visible(progress_bar_, FALSE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar_), 0.0);
    gtk_label_set_text(GTK_LABEL(progress_label_), "");
    queued_files_.clear();
    update_file_list_ui();
    gtk_stack_set_visible_child_name(GTK_STACK(stack_), "session");
}

// ── Discovery ──

void SessionPanel::start_discovery() {
    FD_LOG("SessionPanel — starting discovery");
    static SessionPanel* g_disc_panel = nullptr;
    g_disc_panel = this;
    fd_start_discovery(482913, [](const fd_device_t* dev) {
        if (!dev || !g_disc_panel) return;
        networking::DiscoveredDevice cpp_dev;
        cpp_dev.ip = dev->ip ? dev->ip : "";
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

    struct DeviceFoundData {
        SessionPanel* self;
        networking::DiscoveredDevice device;
    };

    auto* d = new DeviceFoundData{this, device};

    g_idle_add(
        +[](gpointer user_data) -> gboolean {
            auto* data = static_cast<DeviceFoundData*>(user_data);
            auto* self = data->self;
            auto dev = data->device;

            GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            gtk_widget_add_css_class(row_box, "device-row");

            std::string text = "💻  FluxDrop Host — " + dev.ip + ":" + std::to_string(dev.port) + "  →";
            GtkWidget* label = gtk_label_new(text.c_str());
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_widget_set_hexpand(label, TRUE);
            gtk_box_append(GTK_BOX(row_box), label);

            GtkWidget* row = gtk_list_box_row_new();
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), row_box);
            g_object_set_data_full(G_OBJECT(row), "device_data", new networking::DiscoveredDevice(dev),
                                   [](gpointer p) { delete static_cast<networking::DiscoveredDevice*>(p); });

            gtk_list_box_append(GTK_LIST_BOX(self->device_list_box_), row);

            delete data;
            return G_SOURCE_REMOVE;
        },
        d);
}

void SessionPanel::on_device_row_activated(GtkListBox* /*box*/, GtkListBoxRow* row, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    if (self->session_active_) return;

    auto* dev_ptr = static_cast<networking::DiscoveredDevice*>(g_object_get_data(G_OBJECT(row), "device_data"));
    if (dev_ptr) {
        FD_LOG("Device selected: " << dev_ptr->ip << ":" << dev_ptr->port);
        self->connect_to_device(*dev_ptr);
    }
}

void SessionPanel::connect_to_device(const networking::DiscoveredDevice& device) {
    FD_LOG("Opening PIN dialog for " << device.ip);

    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Enter PIN");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 320, 180);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    if (parent_window_) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), parent_window_);
    }

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 24);
    gtk_widget_set_margin_end(vbox, 24);
    gtk_widget_set_margin_top(vbox, 24);
    gtk_widget_set_margin_bottom(vbox, 24);

    std::string prompt_str = "Join " + device.ip;
    GtkWidget* prompt = gtk_label_new(prompt_str.c_str());
    gtk_widget_add_css_class(prompt, "title-text");
    gtk_label_set_xalign(GTK_LABEL(prompt), 0.0);
    gtk_box_append(GTK_BOX(vbox), prompt);

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter 4-digit PIN");
    gtk_entry_buffer_set_max_length(gtk_entry_get_buffer(GTK_ENTRY(entry)), 4);
    gtk_box_append(GTK_BOX(vbox), entry);

    GtkWidget* connect_btn = gtk_button_new_with_label("Join Session");
    gtk_widget_add_css_class(connect_btn, "suggested-action");
    gtk_box_append(GTK_BOX(vbox), connect_btn);

    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    struct PinDialogData {
        SessionPanel* self;
        GtkWidget* dialog;
        GtkWidget* entry;
        networking::DiscoveredDevice device;
    };

    auto* data = new PinDialogData{this, dialog, entry, device};

    auto on_submit = [](GtkWidget* /*widget*/, gpointer user_data) {
        auto* d = static_cast<PinDialogData*>(user_data);
        const char* pin_text = gtk_editable_get_text(GTK_EDITABLE(d->entry));
        std::string pin = pin_text ? pin_text : "";
        if (pin.empty()) return;

        FD_LOG("Joining session at " << d->device.ip << ":" << d->device.port);

        auto dev_copy = d->device;
        auto* panel = d->self;
        GtkWidget* dlg = d->dialog;

        gtk_window_close(GTK_WINDOW(dlg));

        panel->stop_discovery();
        panel->start_join_session(dev_copy, pin);
    };

    g_signal_connect(connect_btn, "clicked", G_CALLBACK(+on_submit), data);
    g_signal_connect(entry, "activate", G_CALLBACK(+on_submit), data);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget* /*w*/, gpointer d) {
        delete static_cast<PinDialogData*>(d);
    }), data);

    gtk_window_present(GTK_WINDOW(dialog));
}

void SessionPanel::start_join_session(const networking::DiscoveredDevice& device, const std::string& pin) {
    g_session_gen++;

    g_active_panel = this;
    g_s_session_status_lbl = session_status_label_;
    g_s_session_peer_lbl = session_peer_label_;
    g_s_progress_br = progress_bar_;
    g_s_progress_lbl = progress_label_;

    fd_session_join(
        device.ip.c_str(), device.port, pin.c_str(), save_dir_.c_str(),
        // on_session_established
        [](const fd_session_info_t* info) {
            uint64_t gen = g_session_gen.load();
            std::string peer = (info && info->peer_ip) ? info->peer_ip : "";
            struct EstablishedData {
                uint64_t gen;
                std::string peer;
            };
            auto* d = new EstablishedData{gen, peer};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* ed = static_cast<EstablishedData*>(user_data);
                    if (ed->gen == g_session_gen.load() && g_active_panel) {
                        g_active_panel->session_active_ = true;
                        g_active_panel->show_session(ed->peer);
                    }
                    delete ed;
                    return G_SOURCE_REMOVE;
                },
                d);
        },
        // on_session_ended
        []() {
            uint64_t gen = g_session_gen.load();
            struct EndedData {
                uint64_t gen;
            };
            auto* d = new EndedData{gen};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* ed = static_cast<EndedData*>(user_data);
                    if (ed->gen == g_session_gen.load() && g_active_panel) {
                        g_auto_accept_incoming = false;
                        g_active_panel->session_active_ = false;
                        g_active_panel->show_pre_session();
                    }
                    delete ed;
                    return G_SOURCE_REMOVE;
                },
                d);
        },
        // on_status
        [](const char* msg) {
            uint64_t gen = g_session_gen.load();
            auto* d = new StatusTextData{gen, msg ? msg : ""};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* td = static_cast<StatusTextData*>(user_data);
                    if (td->gen == g_session_gen.load()) {
                        if (GTK_IS_LABEL(g_s_session_status_lbl)) {
                            gtk_label_set_text(GTK_LABEL(g_s_session_status_lbl), td->text.c_str());
                        }
                    }
                    delete td;
                    return G_SOURCE_REMOVE;
                },
                d);
        },
        // on_error
        [](const char* err) {
            uint64_t gen = g_session_gen.load();
            std::string text = "❌ " + std::string(err ? err : "Unknown error");
            auto* d = new StatusTextData{gen, text};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* td = static_cast<StatusTextData*>(user_data);
                    if (td->gen == g_session_gen.load() && g_active_panel) {
                        if (g_active_panel->session_active_) {
                            if (GTK_IS_LABEL(g_s_session_status_lbl)) {
                                gtk_label_set_text(GTK_LABEL(g_s_session_status_lbl), td->text.c_str());
                            }
                        } else {
                            if (GTK_IS_LABEL(g_s_session_status_lbl)) {
                                gtk_label_set_text(GTK_LABEL(g_s_session_status_lbl), td->text.c_str());
                            }
                            g_active_panel->show_pre_session();
                        }
                    }
                    delete td;
                    return G_SOURCE_REMOVE;
                },
                d);
        },
        // on_file_offer
        [](const char* filename, uint64_t size) -> bool {
            if (g_auto_accept_incoming) return true;

            auto prom = std::make_shared<std::promise<bool>>();
            auto fut = prom->get_future();

            std::string fname = filename ? filename : "unknown";
            double size_mb = static_cast<double>(size) / (1024.0 * 1024.0);
            char prompt_buf[512];
            snprintf(prompt_buf, sizeof(prompt_buf), "Accept incoming file?\n\n%s\n%.1f MB", fname.c_str(), size_mb);
            std::string prompt_text = prompt_buf;

            struct OfferDialogData {
                std::shared_ptr<std::promise<bool>> prom;
                std::string prompt_text;
            };

            auto* data = new OfferDialogData{prom, prompt_text};

            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* d = static_cast<OfferDialogData*>(user_data);
                    if (!g_active_panel) {
                        d->prom->set_value(false);
                        delete d;
                        return G_SOURCE_REMOVE;
                    }

                    GtkWidget* dialog = gtk_window_new();
                    gtk_window_set_title(GTK_WINDOW(dialog), "Incoming File");
                    gtk_window_set_default_size(GTK_WINDOW(dialog), 350, 150);
                    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
                    if (g_active_panel->parent_window_) {
                        gtk_window_set_transient_for(GTK_WINDOW(dialog), g_active_panel->parent_window_);
                    }

                    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
                    gtk_widget_set_margin_start(vbox, 24);
                    gtk_widget_set_margin_end(vbox, 24);
                    gtk_widget_set_margin_top(vbox, 24);
                    gtk_widget_set_margin_bottom(vbox, 24);

                    GtkWidget* label = gtk_label_new(d->prompt_text.c_str());
                    gtk_widget_add_css_class(label, "title-text");
                    gtk_box_append(GTK_BOX(vbox), label);

                    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
                    gtk_widget_set_halign(hbox, GTK_ALIGN_CENTER);

                    GtkWidget* reject_btn = gtk_button_new_with_label("Reject");
                    gtk_widget_add_css_class(reject_btn, "destructive-action");
                    gtk_box_append(GTK_BOX(hbox), reject_btn);

                    GtkWidget* accept_btn = gtk_button_new_with_label("Accept");
                    gtk_widget_add_css_class(accept_btn, "suggested-action");
                    gtk_box_append(GTK_BOX(hbox), accept_btn);

                    gtk_box_append(GTK_BOX(vbox), hbox);
                    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

                    struct DialogState {
                        std::shared_ptr<std::promise<bool>> prom;
                        std::atomic<bool> answered{false};
                        GtkWidget* dialog;
                    };

                    auto* state = new DialogState{d->prom, false, dialog};

                    g_signal_connect(accept_btn, "clicked",
                                     G_CALLBACK(+[](GtkWidget* /*b*/, gpointer p) {
                                         auto* s = static_cast<DialogState*>(p);
                                         bool expected = false;
                                         if (s->answered.compare_exchange_strong(expected, true)) {
                                             g_auto_accept_incoming = true;
                                             s->prom->set_value(true);
                                         }
                                         gtk_window_close(GTK_WINDOW(s->dialog));
                                     }),
                                     state);

                    g_signal_connect(reject_btn, "clicked",
                                     G_CALLBACK(+[](GtkWidget* /*b*/, gpointer p) {
                                         auto* s = static_cast<DialogState*>(p);
                                         bool expected = false;
                                         if (s->answered.compare_exchange_strong(expected, true)) {
                                             s->prom->set_value(false);
                                         }
                                         gtk_window_close(GTK_WINDOW(s->dialog));
                                     }),
                                     state);

                    g_signal_connect(dialog, "destroy",
                                     G_CALLBACK(+[](GtkWidget* /*w*/, gpointer p) {
                                         auto* s = static_cast<DialogState*>(p);
                                         bool expected = false;
                                         if (s->answered.compare_exchange_strong(expected, true)) {
                                             s->prom->set_value(false);
                                         }
                                         delete s;
                                     }),
                                     state);

                    gtk_window_present(GTK_WINDOW(dialog));
                    delete d;
                    return G_SOURCE_REMOVE;
                },
                data);

            while (fut.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready) {
                if (!g_active_panel || !g_active_panel->session_active_) return false;
            }
            return fut.get();
        },
        // on_progress
        [](const char* filename, uint64_t transferred, uint64_t total, double speed) {
            double frac = (total > 0) ? (static_cast<double>(transferred) / total) : 0.0;
            int pct = static_cast<int>(frac * 100);
            char buf[128];
            snprintf(buf, sizeof(buf), "%d%% — %.1f MB/s — %s", pct, speed, filename ? filename : "");
            uint64_t gen = g_session_gen.load();

            auto* d = new ProgressUpdateData{gen, frac, buf};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* p = static_cast<ProgressUpdateData*>(user_data);
                    if (p->gen == g_session_gen.load()) {
                        if (GTK_IS_PROGRESS_BAR(g_s_progress_br)) {
                            gtk_widget_set_visible(g_s_progress_br, TRUE);
                            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_s_progress_br), p->fraction);
                        }
                        if (GTK_IS_LABEL(g_s_progress_lbl)) {
                            gtk_label_set_text(GTK_LABEL(g_s_progress_lbl), p->text.c_str());
                        }
                    }
                    delete p;
                    return G_SOURCE_REMOVE;
                },
                d);
        },
        // on_file_complete
        [](const char* filename) {
            uint64_t gen = g_session_gen.load();
            std::string text = "✅ " + std::string(filename ? filename : "");
            auto* d = new StatusTextData{gen, text};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* td = static_cast<StatusTextData*>(user_data);
                    if (td->gen == g_session_gen.load()) {
                        if (GTK_IS_LABEL(g_s_session_status_lbl)) {
                            gtk_label_set_text(GTK_LABEL(g_s_session_status_lbl), td->text.c_str());
                        }
                    }
                    delete td;
                    return G_SOURCE_REMOVE;
                },
                d);
        });
}

// ── Host button ──

void SessionPanel::on_host_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    FD_LOG("Host Session clicked");

    self->stop_discovery();
    self->show_hosting();

    g_session_gen++;
    uint64_t gen = g_session_gen.load();

    g_active_panel = self;
    g_s_pin_lbl = self->pin_label_;
    g_s_host_status_lbl = self->host_status_label_;
    g_s_session_status_lbl = self->session_status_label_;
    g_s_session_peer_lbl = self->session_peer_label_;
    g_s_progress_br = self->progress_bar_;
    g_s_progress_lbl = self->progress_label_;

    fd_session_set_save_dir(self->save_dir_.c_str());

    fd_session_host(
        // on_ready
        [](const char* ip, int port, int pin) {
            uint64_t gen = g_session_gen.load();
            std::string pin_str = "PIN: " + std::to_string(pin);
            std::string status_str = "Listening on " + std::string(ip ? ip : "") + ":" + std::to_string(port) +
                                     " — Waiting for guest...";
            auto* d = new HostReadyData{gen, pin_str, status_str};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* hd = static_cast<HostReadyData*>(user_data);
                    if (hd->gen == g_session_gen.load()) {
                        if (GTK_IS_LABEL(g_s_pin_lbl)) {
                            gtk_label_set_text(GTK_LABEL(g_s_pin_lbl), hd->pin_text.c_str());
                        }
                        if (GTK_IS_LABEL(g_s_host_status_lbl)) {
                            gtk_label_set_text(GTK_LABEL(g_s_host_status_lbl), hd->status_text.c_str());
                        }
                    }
                    delete hd;
                    return G_SOURCE_REMOVE;
                },
                d);
        },
        // on_session_established
        [](const fd_session_info_t* info) {
            uint64_t gen = g_session_gen.load();
            std::string peer = (info && info->peer_ip) ? info->peer_ip : "";
            struct EstablishedData {
                uint64_t gen;
                std::string peer;
            };
            auto* d = new EstablishedData{gen, peer};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* ed = static_cast<EstablishedData*>(user_data);
                    if (ed->gen == g_session_gen.load() && g_active_panel) {
                        g_active_panel->session_active_ = true;
                        g_active_panel->show_session(ed->peer);
                    }
                    delete ed;
                    return G_SOURCE_REMOVE;
                },
                d);
        },
        // on_session_ended
        []() {
            uint64_t gen = g_session_gen.load();
            struct EndedData {
                uint64_t gen;
            };
            auto* d = new EndedData{gen};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* ed = static_cast<EndedData*>(user_data);
                    if (ed->gen == g_session_gen.load() && g_active_panel) {
                        g_auto_accept_incoming = false;
                        g_active_panel->session_active_ = false;
                        g_active_panel->show_pre_session();
                    }
                    delete ed;
                    return G_SOURCE_REMOVE;
                },
                d);
        },
        // on_status
        [](const char* msg) {
            uint64_t gen = g_session_gen.load();
            auto* d = new StatusTextData{gen, msg ? msg : ""};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* td = static_cast<StatusTextData*>(user_data);
                    if (td->gen == g_session_gen.load() && g_active_panel) {
                        if (g_active_panel->session_active_) {
                            if (GTK_IS_LABEL(g_s_session_status_lbl)) {
                                gtk_label_set_text(GTK_LABEL(g_s_session_status_lbl), td->text.c_str());
                            }
                        } else {
                            if (GTK_IS_LABEL(g_s_host_status_lbl)) {
                                gtk_label_set_text(GTK_LABEL(g_s_host_status_lbl), td->text.c_str());
                            }
                        }
                    }
                    delete td;
                    return G_SOURCE_REMOVE;
                },
                d);
        },
        // on_error
        [](const char* err) {
            uint64_t gen = g_session_gen.load();
            std::string text = "❌ " + std::string(err ? err : "Unknown error");
            auto* d = new StatusTextData{gen, text};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* td = static_cast<StatusTextData*>(user_data);
                    if (td->gen == g_session_gen.load() && g_active_panel) {
                        if (g_active_panel->session_active_) {
                            if (GTK_IS_LABEL(g_s_session_status_lbl)) {
                                gtk_label_set_text(GTK_LABEL(g_s_session_status_lbl), td->text.c_str());
                            }
                        } else {
                            if (GTK_IS_LABEL(g_s_host_status_lbl)) {
                                gtk_label_set_text(GTK_LABEL(g_s_host_status_lbl), td->text.c_str());
                            }
                        }
                    }
                    delete td;
                    return G_SOURCE_REMOVE;
                },
                d);
        },
        // on_file_offer
        [](const char* filename, uint64_t size) -> bool {
            if (g_auto_accept_incoming) return true;

            auto prom = std::make_shared<std::promise<bool>>();
            auto fut = prom->get_future();

            std::string fname = filename ? filename : "unknown";
            double size_mb = static_cast<double>(size) / (1024.0 * 1024.0);
            char prompt_buf[512];
            snprintf(prompt_buf, sizeof(prompt_buf), "Accept incoming file?\n\n%s\n%.1f MB", fname.c_str(), size_mb);
            std::string prompt_text = prompt_buf;

            struct OfferDialogData {
                std::shared_ptr<std::promise<bool>> prom;
                std::string prompt_text;
            };

            auto* data = new OfferDialogData{prom, prompt_text};

            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* d = static_cast<OfferDialogData*>(user_data);
                    if (!g_active_panel) {
                        d->prom->set_value(false);
                        delete d;
                        return G_SOURCE_REMOVE;
                    }

                    GtkWidget* dialog = gtk_window_new();
                    gtk_window_set_title(GTK_WINDOW(dialog), "Incoming File");
                    gtk_window_set_default_size(GTK_WINDOW(dialog), 350, 150);
                    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
                    if (g_active_panel->parent_window_) {
                        gtk_window_set_transient_for(GTK_WINDOW(dialog), g_active_panel->parent_window_);
                    }

                    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
                    gtk_widget_set_margin_start(vbox, 24);
                    gtk_widget_set_margin_end(vbox, 24);
                    gtk_widget_set_margin_top(vbox, 24);
                    gtk_widget_set_margin_bottom(vbox, 24);

                    GtkWidget* label = gtk_label_new(d->prompt_text.c_str());
                    gtk_widget_add_css_class(label, "title-text");
                    gtk_box_append(GTK_BOX(vbox), label);

                    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
                    gtk_widget_set_halign(hbox, GTK_ALIGN_CENTER);

                    GtkWidget* reject_btn = gtk_button_new_with_label("Reject");
                    gtk_widget_add_css_class(reject_btn, "destructive-action");
                    gtk_box_append(GTK_BOX(hbox), reject_btn);

                    GtkWidget* accept_btn = gtk_button_new_with_label("Accept");
                    gtk_widget_add_css_class(accept_btn, "suggested-action");
                    gtk_box_append(GTK_BOX(hbox), accept_btn);

                    gtk_box_append(GTK_BOX(vbox), hbox);
                    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

                    struct DialogState {
                        std::shared_ptr<std::promise<bool>> prom;
                        std::atomic<bool> answered{false};
                        GtkWidget* dialog;
                    };

                    auto* state = new DialogState{d->prom, false, dialog};

                    g_signal_connect(accept_btn, "clicked",
                                     G_CALLBACK(+[](GtkWidget* /*b*/, gpointer p) {
                                         auto* s = static_cast<DialogState*>(p);
                                         bool expected = false;
                                         if (s->answered.compare_exchange_strong(expected, true)) {
                                             g_auto_accept_incoming = true;
                                             s->prom->set_value(true);
                                         }
                                         gtk_window_close(GTK_WINDOW(s->dialog));
                                     }),
                                     state);

                    g_signal_connect(reject_btn, "clicked",
                                     G_CALLBACK(+[](GtkWidget* /*b*/, gpointer p) {
                                         auto* s = static_cast<DialogState*>(p);
                                         bool expected = false;
                                         if (s->answered.compare_exchange_strong(expected, true)) {
                                             s->prom->set_value(false);
                                         }
                                         gtk_window_close(GTK_WINDOW(s->dialog));
                                     }),
                                     state);

                    g_signal_connect(dialog, "destroy",
                                     G_CALLBACK(+[](GtkWidget* /*w*/, gpointer p) {
                                         auto* s = static_cast<DialogState*>(p);
                                         bool expected = false;
                                         if (s->answered.compare_exchange_strong(expected, true)) {
                                             s->prom->set_value(false);
                                         }
                                         delete s;
                                     }),
                                     state);

                    gtk_window_present(GTK_WINDOW(dialog));
                    delete d;
                    return G_SOURCE_REMOVE;
                },
                data);

            while (fut.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready) {
                if (!g_active_panel || !g_active_panel->session_active_) return false;
            }
            return fut.get();
        },
        // on_progress
        [](const char* filename, uint64_t transferred, uint64_t total, double speed) {
            double frac = (total > 0) ? (static_cast<double>(transferred) / total) : 0.0;
            int pct = static_cast<int>(frac * 100);
            char buf[128];
            snprintf(buf, sizeof(buf), "%d%% — %.1f MB/s — %s", pct, speed, filename ? filename : "");
            uint64_t gen = g_session_gen.load();

            auto* d = new ProgressUpdateData{gen, frac, buf};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* p = static_cast<ProgressUpdateData*>(user_data);
                    if (p->gen == g_session_gen.load()) {
                        if (GTK_IS_PROGRESS_BAR(g_s_progress_br)) {
                            gtk_widget_set_visible(g_s_progress_br, TRUE);
                            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_s_progress_br), p->fraction);
                        }
                        if (GTK_IS_LABEL(g_s_progress_lbl)) {
                            gtk_label_set_text(GTK_LABEL(g_s_progress_lbl), p->text.c_str());
                        }
                    }
                    delete p;
                    return G_SOURCE_REMOVE;
                },
                d);
        },
        // on_file_complete
        [](const char* filename) {
            uint64_t gen = g_session_gen.load();
            std::string text = "✅ " + std::string(filename ? filename : "");
            auto* d = new StatusTextData{gen, text};
            g_idle_add(
                +[](gpointer user_data) -> gboolean {
                    auto* td = static_cast<StatusTextData*>(user_data);
                    if (td->gen == g_session_gen.load()) {
                        if (GTK_IS_LABEL(g_s_session_status_lbl)) {
                            gtk_label_set_text(GTK_LABEL(g_s_session_status_lbl), td->text.c_str());
                        }
                    }
                    delete td;
                    return G_SOURCE_REMOVE;
                },
                d);
        });
}

// ── Join ──

void SessionPanel::on_join_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    FD_LOG("Join Session clicked — opening manual IP dialog");
    on_manual_connect(nullptr, self);
}

// ── Cancel / Disconnect ──

void SessionPanel::on_cancel_host_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    FD_LOG("Cancel host clicked");
    g_session_gen++;
    self->session_active_ = false;
    fd_session_disconnect();
    self->show_pre_session();
}

void SessionPanel::on_disconnect_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    FD_LOG("Disconnect clicked");
    g_session_gen++;
    self->session_active_ = false;
    fd_session_disconnect();
    self->show_pre_session();
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
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(file_list_box_)) != nullptr) {
        gtk_list_box_remove(GTK_LIST_BOX(file_list_box_), child);
    }

    for (const auto& file : queued_files_) {
        fs::path p(file);
        std::string display = "📄 " + p.filename().string();

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

        GtkWidget* label = gtk_label_new(display.c_str());
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_add_css_class(label, "file-item");
        gtk_list_box_append(GTK_LIST_BOX(file_list_box_), label);
    }

    gtk_widget_set_sensitive(send_button_, !queued_files_.empty() && session_active_);
}

void SessionPanel::on_choose_files_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    FD_LOG("Choose files dialog opening");

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkFileChooserNative* native = gtk_file_chooser_native_new("Select Files", self->parent_window_,
                                                               GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(native), TRUE);

    g_signal_connect(native, "response",
                     G_CALLBACK(+[](GtkNativeDialog* dialog, int response, gpointer data) {
                         if (response == GTK_RESPONSE_ACCEPT) {
                             auto* panel = static_cast<SessionPanel*>(data);
                             GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);
                             GListModel* files = gtk_file_chooser_get_files(chooser);
                             for (guint i = 0; i < g_list_model_get_n_items(files); i++) {
                                 GFile* file = G_FILE(g_list_model_get_item(files, i));
                                 char* path = g_file_get_path(file);
                                 if (path) {
                                     panel->add_path(path);
                                     g_free(path);
                                 }
                                 g_object_unref(file);
                             }
                             g_object_unref(files);
                         }
                         g_object_unref(dialog);
                     }),
                     self);

    gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
    G_GNUC_END_IGNORE_DEPRECATIONS
}

void SessionPanel::on_choose_folder_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    FD_LOG("Choose folder dialog opening");

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkFileChooserNative* native = gtk_file_chooser_native_new(
        "Select Folder", self->parent_window_, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Open", "_Cancel");

    g_signal_connect(native, "response",
                     G_CALLBACK(+[](GtkNativeDialog* dialog, int response, gpointer data) {
                         if (response == GTK_RESPONSE_ACCEPT) {
                             auto* panel = static_cast<SessionPanel*>(data);
                             GFile* folder = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
                             if (folder) {
                                 char* path = g_file_get_path(folder);
                                 if (path) {
                                     panel->add_path(path);
                                     g_free(path);
                                 }
                                 g_object_unref(folder);
                             }
                         }
                         g_object_unref(dialog);
                     }),
                     self);

    gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
    G_GNUC_END_IGNORE_DEPRECATIONS
}

void SessionPanel::on_clear_files_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    self->queued_files_.clear();
    self->update_file_list_ui();
}

void SessionPanel::on_send_files_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    if (self->queued_files_.empty() || !self->session_active_) return;

    FD_LOG("Sending " << self->queued_files_.size() << " file(s) via session");

    std::vector<const char*> c_paths;
    for (const auto& p : self->queued_files_) {
        c_paths.push_back(p.c_str());
    }

    std::string status_text = "Sending " + std::to_string(self->queued_files_.size()) + " file(s)...";
    gtk_label_set_text(GTK_LABEL(self->session_status_label_), status_text.c_str());
    gtk_widget_set_visible(self->progress_bar_, TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(self->progress_bar_), 0.0);

    fd_session_send_files(c_paths.data(), static_cast<int>(c_paths.size()));

    self->queued_files_.clear();
    self->update_file_list_ui();
}

// ── Drag & drop ──

gboolean SessionPanel::on_drag_drop(GtkDropTarget* /*target*/, const GValue* value, double /*x*/, double /*y*/,
                                    gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    if (self->drop_area_) {
        gtk_widget_remove_css_class(self->drop_area_, "drop-zone-active");
    }

    if (!self->session_active_) return FALSE;

    if (G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
        GdkFileList* file_list = static_cast<GdkFileList*>(g_value_get_boxed(value));
        GSList* files = gdk_file_list_get_files(file_list);
        for (GSList* l = files; l != nullptr; l = l->next) {
            GFile* file = G_FILE(l->data);
            char* path = g_file_get_path(file);
            if (path) {
                self->add_path(path);
                g_free(path);
            }
        }
        return TRUE;
    }
    return FALSE;
}

GdkDragAction SessionPanel::on_drag_enter(GtkDropTarget* /*target*/, double /*x*/, double /*y*/, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    if (self->session_active_) {
        if (self->drop_area_) {
            gtk_widget_add_css_class(self->drop_area_, "drop-zone-active");
        }
        return GDK_ACTION_COPY;
    }
    return (GdkDragAction)0;
}

void SessionPanel::on_drag_leave(GtkDropTarget* /*target*/, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    if (self->drop_area_) {
        gtk_widget_remove_css_class(self->drop_area_, "drop-zone-active");
    }
}

// ── Manual connect ──

void SessionPanel::on_manual_connect(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Connect by IP");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 340, 220);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    if (self->parent_window_) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), self->parent_window_);
    }

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 24);
    gtk_widget_set_margin_end(vbox, 24);
    gtk_widget_set_margin_top(vbox, 24);
    gtk_widget_set_margin_bottom(vbox, 24);

    GtkWidget* title = gtk_label_new("Enter the host's IP and port");
    gtk_widget_add_css_class(title, "title-text");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_append(GTK_BOX(vbox), title);

    GtkWidget* ip_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ip_entry), "IP address (e.g. 192.168.43.1)");
    gtk_box_append(GTK_BOX(vbox), ip_entry);

    GtkWidget* port_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(port_entry), "Port (shown on host)");
    gtk_box_append(GTK_BOX(vbox), port_entry);

    GtkWidget* connect_btn = gtk_button_new_with_label("Next");
    gtk_widget_add_css_class(connect_btn, "suggested-action");
    gtk_box_append(GTK_BOX(vbox), connect_btn);

    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    struct ManualConnectData {
        SessionPanel* self;
        GtkWidget* dialog;
        GtkWidget* ip_entry;
        GtkWidget* port_entry;
    };

    auto* data = new ManualConnectData{self, dialog, ip_entry, port_entry};

    auto on_next = [](GtkWidget* /*widget*/, gpointer user_data) {
        auto* d = static_cast<ManualConnectData*>(user_data);
        const char* ip_txt = gtk_editable_get_text(GTK_EDITABLE(d->ip_entry));
        const char* port_txt = gtk_editable_get_text(GTK_EDITABLE(d->port_entry));
        std::string ip = ip_txt ? ip_txt : "";
        std::string port_str = port_txt ? port_txt : "";
        if (ip.empty() || port_str.empty()) return;

        networking::DiscoveredDevice device;
        device.ip = ip;
        try {
            device.port = static_cast<unsigned short>(std::stoi(port_str));
        } catch (...) {
            return;
        }
        device.session_id = 0;

        auto* panel = d->self;
        gtk_window_close(GTK_WINDOW(d->dialog));
        panel->connect_to_device(device);
    };

    g_signal_connect(connect_btn, "clicked", G_CALLBACK(+on_next), data);
    g_signal_connect(port_entry, "activate", G_CALLBACK(+on_next), data);
    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget* /*w*/, gpointer d) {
        delete static_cast<ManualConnectData*>(d);
    }), data);

    gtk_window_present(GTK_WINDOW(dialog));
}

void SessionPanel::on_change_save_dir(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<SessionPanel*>(user_data);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkFileChooserNative* native = gtk_file_chooser_native_new(
        "Select Save Folder", self->parent_window_, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Select", "_Cancel");

    g_signal_connect(native, "response",
                     G_CALLBACK(+[](GtkNativeDialog* dialog, int response, gpointer data) {
                         if (response == GTK_RESPONSE_ACCEPT) {
                             auto* panel = static_cast<SessionPanel*>(data);
                             GFile* folder = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
                             if (folder) {
                                 char* path = g_file_get_path(folder);
                                 if (path) {
                                     panel->save_dir_ = path;
                                     gtk_label_set_text(GTK_LABEL(panel->save_label_), path);
                                     fd_session_set_save_dir(path);
                                     FD_LOG("Save directory changed to: " << panel->save_dir_);
                                     g_free(path);
                                 }
                                 g_object_unref(folder);
                             }
                         }
                         g_object_unref(dialog);
                     }),
                     self);

    gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
    G_GNUC_END_IGNORE_DEPRECATIONS
}

} // namespace ui
