#include "ui/device_list.hpp"
#include "ui/transfer_dialog.hpp"
#include "logger.hpp"
#include <iostream>
#include <thread>

namespace ui {

// ─── Generation counter — incremented on cancel to discard stale idle callbacks
static std::atomic<uint64_t> g_recv_gen{0};

// ─── Idle callback data structs ─────────────────────────────────────────────

struct RecvStatusData {
    GtkWidget* label;
    std::string text;
    uint64_t gen;
};

struct RecvProgressData {
    GtkWidget* progress_bar;
    GtkWidget* progress_label;
    double fraction;
    std::string text;
    uint64_t gen;
};

struct RecvCompleteData {
    GtkWidget* status_label;
    GtkWidget* progress_bar;
    std::string message;
    uint64_t gen;
};

struct AddRowData {
    GtkWidget* list_box;
    networking::DiscoveredDevice device;
};

struct RecvReenableData {
    GtkWidget* cancel_button;
    GtkWidget* progress_bar;
    GtkWidget* status_label;
    GtkWidget* progress_label;
    std::string status_message;
    uint64_t gen;
};

// ─── Connect button callback data ──────────────────────────────────────────

struct ConnectData {
    DeviceListPanel* panel;
    networking::DiscoveredDevice device;
    GtkWidget* entry;
    GtkWidget* pin_window;
    std::string save_dir;
};

struct FileRequestData {
    GtkWindow* parent_window;
    std::string filename;
    uint64_t filesize;
    std::promise<bool>* promise;
};

// ─── Static context for C callbacks ─────────────────────────────────────────
static DeviceListPanel* g_client_panel = nullptr;

// ─── Idle callbacks (run on main thread) ────────────────────────────────────

static gboolean update_recv_status_idle(gpointer data) {
    auto* d = static_cast<RecvStatusData*>(data);
    if (d->gen == g_recv_gen.load() && GTK_IS_LABEL(d->label)) {
        gtk_label_set_text(GTK_LABEL(d->label), d->text.c_str());
    } else {
        FD_WARN("Discarding stale recv status idle");
    }
    delete d;
    return G_SOURCE_REMOVE;
}

static gboolean update_recv_progress_idle(gpointer data) {
    auto* d = static_cast<RecvProgressData*>(data);
    if (d->gen == g_recv_gen.load()) {
        if (GTK_IS_PROGRESS_BAR(d->progress_bar))
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(d->progress_bar), d->fraction);
        if (GTK_IS_LABEL(d->progress_label))
            gtk_label_set_text(GTK_LABEL(d->progress_label), d->text.c_str());
    }
    delete d;
    return G_SOURCE_REMOVE;
}

static gboolean recv_complete_idle(gpointer data) {
    auto* d = static_cast<RecvReenableData*>(data);
    if (d->gen == g_recv_gen.load()) {
        FD_LOG("Recv complete idle: " << d->status_message);
        if (GTK_IS_LABEL(d->status_label))
            gtk_label_set_text(GTK_LABEL(d->status_label), d->status_message.c_str());
        if (GTK_IS_PROGRESS_BAR(d->progress_bar))
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(d->progress_bar), 1.0);
        if (GTK_IS_WIDGET(d->cancel_button))
            gtk_widget_set_visible(d->cancel_button, FALSE);
    } else {
        FD_WARN("Discarding stale recv complete idle");
    }
    delete d;
    return G_SOURCE_REMOVE;
}

static gboolean file_request_idle(gpointer data) {
    auto* d = static_cast<FileRequestData*>(data);

    FD_LOG("File request dialog for: " << d->filename);

    GtkWidget* dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Incoming File");
    gtk_window_set_default_size(GTK_WINDOW(dialog), 350, 150);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), d->parent_window);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 24);
    gtk_widget_set_margin_end(vbox, 24);
    gtk_widget_set_margin_top(vbox, 24);
    gtk_widget_set_margin_bottom(vbox, 24);

    double size_mb = static_cast<double>(d->filesize) / (1024.0 * 1024.0);
    char buf[256];
    snprintf(buf, sizeof(buf), "Accept incoming file?\n\n%s\n%.1f MB", d->filename.c_str(), size_mb);

    GtkWidget* prompt = gtk_label_new(buf);
    gtk_widget_add_css_class(prompt, "title-text");
    gtk_box_append(GTK_BOX(vbox), prompt);

    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(hbox, GTK_ALIGN_CENTER);

    GtkWidget* reject_btn = gtk_button_new_with_label("Reject");
    gtk_widget_add_css_class(reject_btn, "destructive-action");

    GtkWidget* accept_btn = gtk_button_new_with_label("Accept");
    gtk_widget_add_css_class(accept_btn, "suggested-action");

    gtk_box_append(GTK_BOX(hbox), reject_btn);
    gtk_box_append(GTK_BOX(hbox), accept_btn);
    gtk_box_append(GTK_BOX(vbox), hbox);

    gtk_window_set_child(GTK_WINDOW(dialog), vbox);

    struct RespData {
        GtkWidget* win;
        std::promise<bool>* prom;
        bool answered;
    };
    auto* rd = new RespData{dialog, d->promise, false};

    g_signal_connect(accept_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user) {
        auto* rnd = static_cast<RespData*>(user);
        if (!rnd->answered) {
            FD_LOG("File request: ACCEPTED");
            rnd->prom->set_value(true);
            rnd->answered = true;
        }
        gtk_window_close(GTK_WINDOW(rnd->win));
    }), rd);

    g_signal_connect(reject_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user) {
        auto* rnd = static_cast<RespData*>(user);
        if (!rnd->answered) {
            FD_LOG("File request: REJECTED");
            rnd->prom->set_value(false);
            rnd->answered = true;
        }
        gtk_window_close(GTK_WINDOW(rnd->win));
    }), rd);

    g_signal_connect(dialog, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer user) {
        auto* rnd = static_cast<RespData*>(user);
        if (!rnd->answered) {
            FD_WARN("File request dialog destroyed without answer — rejecting");
            rnd->prom->set_value(false);
            rnd->answered = true;
        }
        delete rnd;
    }), rd);

    gtk_window_present(GTK_WINDOW(dialog));
    delete d;
    return G_SOURCE_REMOVE;
}

static gboolean add_device_row_idle(gpointer d) {
    auto* data = static_cast<AddRowData*>(d);
    if (!GTK_IS_LIST_BOX(data->list_box)) { delete data; return G_SOURCE_REMOVE; }

    FD_LOG("Adding device row: " << data->device.ip << ":" << data->device.port);

    GtkWidget* row_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(row_box, "device-row");
    gtk_widget_set_margin_start(row_box, 4);
    gtk_widget_set_margin_end(row_box, 4);
    gtk_widget_set_margin_top(row_box, 4);
    gtk_widget_set_margin_bottom(row_box, 4);

    GtkWidget* icon = gtk_label_new("💻");
    gtk_box_append(GTK_BOX(row_box), icon);

    GtkWidget* info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_hexpand(info_box, TRUE);

    GtkWidget* name_label = gtk_label_new("FluxDrop Device");
    gtk_label_set_xalign(GTK_LABEL(name_label), 0.0);
    gtk_widget_add_css_class(name_label, "title-text");
    gtk_box_append(GTK_BOX(info_box), name_label);

    std::string detail = data->device.ip + " — Room " + std::to_string(data->device.session_id);
    GtkWidget* detail_label = gtk_label_new(detail.c_str());
    gtk_label_set_xalign(GTK_LABEL(detail_label), 0.0);
    gtk_widget_add_css_class(detail_label, "subtitle-text");
    gtk_box_append(GTK_BOX(info_box), detail_label);

    gtk_box_append(GTK_BOX(row_box), info_box);

    GtkWidget* arrow = gtk_label_new("→");
    gtk_widget_add_css_class(arrow, "title-text");
    gtk_box_append(GTK_BOX(row_box), arrow);

    auto* dev_copy = new networking::DiscoveredDevice(data->device);
    auto destroy_fn = +[](gpointer p) { delete static_cast<networking::DiscoveredDevice*>(p); };
    g_object_set_data_full(G_OBJECT(row_box), "device", dev_copy, destroy_fn);

    gtk_list_box_append(GTK_LIST_BOX(data->list_box), row_box);
    delete data;
    return G_SOURCE_REMOVE;
}

// ─── Connect button clicked handler ─────────────────────────────────────────

void on_connect_btn_clicked(GtkButton* /*btn*/, gpointer data) {
    auto* cd = static_cast<ConnectData*>(data);
    GtkEntryBuffer* buffer = gtk_entry_get_buffer(GTK_ENTRY(cd->entry));
    std::string pin = gtk_entry_buffer_get_text(buffer);

    if (pin.empty()) {
        FD_WARN("Connect clicked but PIN is empty");
        return;
    }

    FD_LOG("Connecting to " << cd->device.ip << ":" << cd->device.port << " with PIN");

    gtk_window_close(GTK_WINDOW(cd->pin_window));

    // Increment generation for this transfer session
    g_recv_gen++;
    uint64_t gen = g_recv_gen.load();

    cd->panel->transferring_ = true;
    gtk_widget_set_visible(cd->panel->progress_bar_, TRUE);
    gtk_widget_set_visible(cd->panel->cancel_button_, TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(cd->panel->progress_bar_), 0.0);
    gtk_label_set_text(GTK_LABEL(cd->panel->status_label_), "Connecting...");
    gtk_label_set_text(GTK_LABEL(cd->panel->progress_label_), "");

    // Update static context
    g_client_panel = cd->panel;

    auto status_cb = [](const char* msg) {
        FD_LOG("Client status: " << msg);
        uint64_t gen = g_recv_gen.load();
        g_idle_add(update_recv_status_idle, new RecvStatusData{g_client_panel->status_label_, msg, gen});
    };

    auto error_cb = [](const char* err) {
        FD_ERR("Client error: " << err);
        uint64_t gen = g_recv_gen.load();
        g_client_panel->transferring_ = false;
        g_idle_add(recv_complete_idle, new RecvReenableData{
            g_client_panel->cancel_button_, g_client_panel->progress_bar_,
            g_client_panel->status_label_, g_client_panel->progress_label_,
            "❌ " + std::string(err), gen
        });
    };

    auto file_request_cb = [](const char* filename, uint64_t size) -> bool {
        FD_LOG("File request: " << filename << " (" << size << " bytes)");
        std::promise<bool> prom;
        auto fut = prom.get_future();
        g_idle_add(file_request_idle, new FileRequestData{
            g_client_panel->parent_window_, filename, size, &prom
        });
        // Wait with periodic timeout so cancellation can unblock us
        while (fut.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready) {
            // If we've been cancelled, reject and return
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
        g_idle_add(update_recv_progress_idle, new RecvProgressData{
            g_client_panel->progress_bar_, g_client_panel->progress_label_, frac, std::string(buf), gen
        });
    };

    auto complete_cb = []() {
        FD_LOG("Client transfer complete");
        uint64_t gen = g_recv_gen.load();
        g_client_panel->transferring_ = false;
        g_idle_add(recv_complete_idle, new RecvReenableData{
            g_client_panel->cancel_button_, g_client_panel->progress_bar_,
            g_client_panel->status_label_, g_client_panel->progress_label_,
            "✅ All files received!", gen
        });
    };

    auto dev = cd->device;
    auto pin_copy = pin;
    auto save_dir_copy = cd->save_dir;

    fd_connect(dev.ip.c_str(), dev.port, pin_copy.c_str(), save_dir_copy.c_str(),
               status_cb, error_cb, file_request_cb, progress_cb, complete_cb);

    delete cd;
}

// ─── DeviceListPanel ────────────────────────────────────────────────────────

DeviceListPanel::DeviceListPanel(GtkWindow* parent_window)
    : parent_window_(parent_window) {

    FD_LOG("DeviceListPanel created");

    panel_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(panel_, 12);
    gtk_widget_set_margin_end(panel_, 12);
    gtk_widget_set_margin_top(panel_, 12);
    gtk_widget_set_margin_bottom(panel_, 12);

    // ─── Header ─────────────────────────────────────────────────────────
    GtkWidget* header = gtk_label_new("📡 Nearby Devices");
    gtk_widget_add_css_class(header, "title-text");
    gtk_label_set_xalign(GTK_LABEL(header), 0.0);
    gtk_box_append(GTK_BOX(panel_), header);

    info_label_ = gtk_label_new("Scanning for FluxDrop senders on your network...");
    gtk_widget_add_css_class(info_label_, "subtitle-text");
    gtk_label_set_xalign(GTK_LABEL(info_label_), 0.0);
    gtk_box_append(GTK_BOX(panel_), info_label_);

    // ─── Save folder picker ─────────────────────────────────────────────
    const char* home = g_get_home_dir();
    std::string downloads = std::string(home) + "/Downloads";
    if (g_file_test(downloads.c_str(), G_FILE_TEST_IS_DIR)) {
        save_dir_ = downloads;
    } else {
        save_dir_ = home;
    }

    GtkWidget* save_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(save_row, 4);
    gtk_widget_set_margin_bottom(save_row, 4);

    GtkWidget* save_icon = gtk_label_new("📂 Save to:");
    gtk_widget_add_css_class(save_icon, "status-text");
    gtk_box_append(GTK_BOX(save_row), save_icon);

    save_label_ = gtk_label_new(save_dir_.c_str());
    gtk_label_set_xalign(GTK_LABEL(save_label_), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(save_label_), PANGO_ELLIPSIZE_START);
    gtk_widget_set_hexpand(save_label_, TRUE);
    gtk_widget_add_css_class(save_label_, "subtitle-text");
    gtk_box_append(GTK_BOX(save_row), save_label_);

    GtkWidget* change_btn = gtk_button_new_with_label("Change");
    gtk_widget_add_css_class(change_btn, "flat");
    g_signal_connect(change_btn, "clicked", G_CALLBACK(+[](GtkButton* /*btn*/, gpointer data) {
        auto* self = static_cast<DeviceListPanel*>(data);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        GtkFileChooserNative* native = gtk_file_chooser_native_new(
            "Select Save Folder", self->parent_window_,
            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Select", "_Cancel");
        g_signal_connect(native, "response", G_CALLBACK(+[](GtkNativeDialog* dialog, int response, gpointer d) {
            if (response == GTK_RESPONSE_ACCEPT) {
                auto* panel = static_cast<DeviceListPanel*>(d);
                GFile* folder = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(dialog));
                if (folder) {
                    char* path = g_file_get_path(folder);
                    if (path) {
                        panel->save_dir_ = path;
                        gtk_label_set_text(GTK_LABEL(panel->save_label_), path);
                        FD_LOG("Save directory changed to: " << path);
                        g_free(path);
                    }
                    g_object_unref(folder);
                }
            }
            g_object_unref(dialog);
        }), self);
        gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
G_GNUC_END_IGNORE_DEPRECATIONS
    }), this);
    gtk_box_append(GTK_BOX(save_row), change_btn);

    gtk_box_append(GTK_BOX(panel_), save_row);

    // ─── Device list ────────────────────────────────────────────────────
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 200);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    list_box_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box_), GTK_SELECTION_SINGLE);
    g_signal_connect(list_box_, "row-activated", G_CALLBACK(row_activated_cb), this);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list_box_);
    gtk_box_append(GTK_BOX(panel_), scroll);

    // ─── Manual "Connect by IP" fallback ────────────────────────────────
    GtkWidget* manual_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(manual_box, 4);
    gtk_widget_set_margin_bottom(manual_box, 4);

    GtkWidget* hint_label = gtk_label_new("💡 Can't find your device?");
    gtk_widget_add_css_class(hint_label, "subtitle-text");
    gtk_label_set_xalign(GTK_LABEL(hint_label), 0.0);
    gtk_box_append(GTK_BOX(manual_box), hint_label);

    struct ManualConnectData {
        DeviceListPanel* panel;
        GtkWidget* ip_entry;
        GtkWidget* port_entry;
        GtkWidget* dialog;
    };

    // Static callbacks extracted to avoid G_CALLBACK macro issues with inline lambdas
    static auto on_connect_manual = +[](GtkButton*, gpointer user) {
        auto* d = static_cast<ManualConnectData*>(user);
        GtkEntryBuffer* ip_buf = gtk_entry_get_buffer(GTK_ENTRY(d->ip_entry));
        GtkEntryBuffer* port_buf = gtk_entry_get_buffer(GTK_ENTRY(d->port_entry));
        std::string ip = gtk_entry_buffer_get_text(ip_buf);
        std::string port_str = gtk_entry_buffer_get_text(port_buf);

        if (ip.empty() || port_str.empty()) return;

        networking::DiscoveredDevice device;
        device.ip = ip;
        device.port = static_cast<unsigned short>(std::stoi(port_str));
        device.session_id = 0;

        gtk_window_close(GTK_WINDOW(d->dialog));
        d->panel->connect_to_device(device);
        delete d;
    };

    static auto on_manual_btn_clicked = +[](GtkButton*, gpointer data) {
        auto* self = static_cast<DeviceListPanel*>(data);
        if (self->transferring_) return;

        GtkWidget* dialog = gtk_window_new();
        gtk_window_set_title(GTK_WINDOW(dialog), "Connect by IP");
        gtk_window_set_default_size(GTK_WINDOW(dialog), 340, 200);
        gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
        gtk_window_set_transient_for(GTK_WINDOW(dialog), self->parent_window_);

        GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_margin_start(vbox, 24);
        gtk_widget_set_margin_end(vbox, 24);
        gtk_widget_set_margin_top(vbox, 24);
        gtk_widget_set_margin_bottom(vbox, 24);

        GtkWidget* title = gtk_label_new("Enter the sender's IP and port");
        gtk_widget_add_css_class(title, "title-text");
        gtk_box_append(GTK_BOX(vbox), title);

        GtkWidget* ip_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(ip_entry), "IP address (e.g. 192.168.43.1)");
        gtk_box_append(GTK_BOX(vbox), ip_entry);

        GtkWidget* port_entry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(port_entry), "Port (shown on sender)");
        gtk_box_append(GTK_BOX(vbox), port_entry);

        GtkWidget* connect_btn = gtk_button_new_with_label("Connect");
        gtk_widget_add_css_class(connect_btn, "suggested-action");
        gtk_box_append(GTK_BOX(vbox), connect_btn);

        gtk_window_set_child(GTK_WINDOW(dialog), vbox);

        auto* mcd = new ManualConnectData{self, ip_entry, port_entry, dialog};
        g_signal_connect(connect_btn, "clicked", G_CALLBACK(on_connect_manual), mcd);

        gtk_window_present(GTK_WINDOW(dialog));
    };

    GtkWidget* manual_btn = gtk_button_new_with_label("🔗 Connect by IP");
    gtk_widget_add_css_class(manual_btn, "flat");
    g_signal_connect(manual_btn, "clicked", G_CALLBACK(on_manual_btn_clicked), this);
    gtk_box_append(GTK_BOX(manual_box), manual_btn);

    gtk_box_append(GTK_BOX(panel_), manual_box);

    // ─── Status / Progress ──────────────────────────────────────────────
    GtkWidget* section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(section, "section-box");

    status_label_ = gtk_label_new("");
    gtk_widget_add_css_class(status_label_, "status-text");
    gtk_box_append(GTK_BOX(section), status_label_);

    progress_bar_ = gtk_progress_bar_new();
    gtk_widget_set_visible(progress_bar_, FALSE);
    gtk_box_append(GTK_BOX(section), progress_bar_);

    progress_label_ = gtk_label_new("");
    gtk_widget_add_css_class(progress_label_, "status-text");
    gtk_box_append(GTK_BOX(section), progress_label_);

    cancel_button_ = gtk_button_new_with_label("⏹ Cancel Transfer");
    gtk_widget_add_css_class(cancel_button_, "destructive-action");
    gtk_widget_set_visible(cancel_button_, FALSE);
    g_signal_connect(cancel_button_, "clicked", G_CALLBACK(+[](GtkButton* /*btn*/, gpointer data) -> void {
        auto* self = static_cast<DeviceListPanel*>(data);
        FD_LOG("Cancel transfer button clicked — non-blocking cancel");

        // Increment generation to invalidate stale callbacks
        g_recv_gen++;
        self->transferring_ = false;

        // Non-blocking cancel — signals stop, closes socket, thread exits on its own
        fd_request_cancel_client();

        // Reset UI immediately
        gtk_widget_set_visible(self->cancel_button_, FALSE);
        gtk_widget_set_visible(self->progress_bar_, FALSE);
        gtk_label_set_text(GTK_LABEL(self->status_label_), "Transfer cancelled.");
        gtk_label_set_text(GTK_LABEL(self->progress_label_), "");
    }), this);
    gtk_box_append(GTK_BOX(section), cancel_button_);

    gtk_box_append(GTK_BOX(panel_), section);
}

DeviceListPanel::~DeviceListPanel() {
    FD_LOG("~DeviceListPanel — cleaning up");
    g_recv_gen++;  // Invalidate pending idles
    stop_discovery();
    if (transferring_) {
        FD_LOG("~DeviceListPanel — cancelling active transfer (blocking)");
        fd_cancel_client();
        transferring_ = false;
    }
    FD_LOG("~DeviceListPanel — done");
}

#include "fluxdrop_core.h"

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
    g_idle_add(add_device_row_idle, new AddRowData{list_box_, device});
}

void DeviceListPanel::row_activated_cb(GtkListBox* /*list_box*/, GtkListBoxRow* row, gpointer user_data) {
    auto* self = static_cast<DeviceListPanel*>(user_data);
    if (self->transferring_) {
        FD_WARN("Row activated but transfer already in progress");
        return;
    }

    GtkWidget* child = gtk_list_box_row_get_child(row);
    if (!child) return;

    auto* device = static_cast<networking::DiscoveredDevice*>(
        g_object_get_data(G_OBJECT(child), "device"));
    if (!device) return;

    FD_LOG("Device selected: " << device->ip << ":" << device->port);
    self->connect_to_device(*device);
}

void DeviceListPanel::connect_to_device(const networking::DiscoveredDevice& device) {
    FD_LOG("Opening PIN dialog for " << device.ip);

    GtkWidget* pin_window = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(pin_window), "Enter PIN");
    gtk_window_set_default_size(GTK_WINDOW(pin_window), 320, 180);
    gtk_window_set_modal(GTK_WINDOW(pin_window), TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(pin_window), parent_window_);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 24);
    gtk_widget_set_margin_end(vbox, 24);
    gtk_widget_set_margin_top(vbox, 24);
    gtk_widget_set_margin_bottom(vbox, 24);

    GtkWidget* prompt = gtk_label_new(("Connect to " + device.ip).c_str());
    gtk_widget_add_css_class(prompt, "title-text");
    gtk_box_append(GTK_BOX(vbox), prompt);

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Enter 4-digit PIN");
    gtk_entry_set_max_length(GTK_ENTRY(entry), 4);
    gtk_box_append(GTK_BOX(vbox), entry);

    GtkWidget* connect_btn = gtk_button_new_with_label("Connect");
    gtk_widget_add_css_class(connect_btn, "suggested-action");
    gtk_box_append(GTK_BOX(vbox), connect_btn);

    gtk_window_set_child(GTK_WINDOW(pin_window), vbox);

    auto* cd = new ConnectData{this, device, entry, pin_window, save_dir_};
    g_signal_connect(connect_btn, "clicked", G_CALLBACK(on_connect_btn_clicked), cd);

    gtk_window_present(GTK_WINDOW(pin_window));
}

} // namespace ui
