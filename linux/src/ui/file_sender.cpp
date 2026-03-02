#include "ui/file_sender.hpp"
#include "ui/transfer_dialog.hpp"
#include "transfer.hpp"
#include "security.hpp"
#include "protocol/packet.hpp"
#include "protocol/file_meta.hpp"
#include <iostream>
#include <filesystem>
#include "fluxdrop_core.h"

namespace fs = std::filesystem;

namespace ui {

// ─── Idle callback data structs ─────────────────────────────────────────────

struct StatusUpdateData {
    GtkWidget* label;
    std::string text;
};

struct ProgressUpdateData {
    GtkWidget* progress_bar;
    GtkWidget* progress_label;
    double fraction;
    std::string text;
};

struct PinUpdateData {
    GtkWidget* pin_label;
    GtkWidget* status_label;
    std::string pin_text;
    std::string status_text;
};

struct TransferCompleteData {
    GtkWidget* status_label;
    GtkWidget* progress_bar;
    GtkWidget* progress_label;
    GtkWidget* send_button;
    std::string message;
};

// ─── Idle callbacks (run on main thread) ────────────────────────────────────

static gboolean update_status_idle(gpointer data) {
    auto* d = static_cast<StatusUpdateData*>(data);
    if (GTK_IS_LABEL(d->label))
        gtk_label_set_text(GTK_LABEL(d->label), d->text.c_str());
    delete d;
    return G_SOURCE_REMOVE;
}

static gboolean update_progress_idle(gpointer data) {
    auto* d = static_cast<ProgressUpdateData*>(data);
    if (GTK_IS_PROGRESS_BAR(d->progress_bar))
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(d->progress_bar), d->fraction);
    if (GTK_IS_LABEL(d->progress_label))
        gtk_label_set_text(GTK_LABEL(d->progress_label), d->text.c_str());
    delete d;
    return G_SOURCE_REMOVE;
}

static gboolean update_pin_idle(gpointer data) {
    auto* d = static_cast<PinUpdateData*>(data);
    if (GTK_IS_LABEL(d->pin_label))
        gtk_label_set_text(GTK_LABEL(d->pin_label), d->pin_text.c_str());
    if (GTK_IS_LABEL(d->status_label))
        gtk_label_set_text(GTK_LABEL(d->status_label), d->status_text.c_str());
    delete d;
    return G_SOURCE_REMOVE;
}

static gboolean transfer_complete_idle(gpointer data) {
    auto* d = static_cast<TransferCompleteData*>(data);
    if (GTK_IS_LABEL(d->status_label))
        gtk_label_set_text(GTK_LABEL(d->status_label), d->message.c_str());
    if (GTK_IS_PROGRESS_BAR(d->progress_bar))
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(d->progress_bar), 1.0);
    if (GTK_IS_LABEL(d->progress_label))
        gtk_label_set_text(GTK_LABEL(d->progress_label), "Done");
    if (GTK_IS_WIDGET(d->send_button))
        gtk_widget_set_sensitive(d->send_button, TRUE);
    delete d;
    return G_SOURCE_REMOVE;
}

// ─── FileSenderPanel ────────────────────────────────────────────────────────

FileSenderPanel::FileSenderPanel(GtkWindow* parent_window)
    : parent_window_(parent_window) {

    panel_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(panel_, 12);
    gtk_widget_set_margin_end(panel_, 12);
    gtk_widget_set_margin_top(panel_, 12);
    gtk_widget_set_margin_bottom(panel_, 12);

    // ─── Drop zone ──────────────────────────────────────────────────────
    drop_area_ = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(drop_area_, "drop-zone");
    gtk_widget_set_vexpand(drop_area_, FALSE);

    drop_label_ = gtk_label_new("🗂️  Drag & Drop Files Here");
    gtk_widget_add_css_class(drop_label_, "title-text");
    gtk_box_append(GTK_BOX(drop_area_), drop_label_);

    GtkWidget* sub_label = gtk_label_new("or use the buttons below");
    gtk_widget_add_css_class(sub_label, "subtitle-text");
    gtk_box_append(GTK_BOX(drop_area_), sub_label);

    gtk_box_append(GTK_BOX(panel_), drop_area_);

    // ─── Drag & drop target ─────────────────────────────────────────────
    GtkDropTarget* drop_target = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(drop_target, "drop", G_CALLBACK(on_drop), this);
    gtk_widget_add_controller(drop_area_, GTK_EVENT_CONTROLLER(drop_target));

    // ─── Buttons row ────────────────────────────────────────────────────
    GtkWidget* btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(btn_box, 4);

    choose_file_button_ = gtk_button_new_with_label("📄 Choose Files");
    gtk_widget_add_css_class(choose_file_button_, "suggested-action");
    g_signal_connect(choose_file_button_, "clicked", G_CALLBACK(on_choose_file), this);
    gtk_box_append(GTK_BOX(btn_box), choose_file_button_);

    choose_folder_button_ = gtk_button_new_with_label("📁 Choose Folder");
    gtk_widget_add_css_class(choose_folder_button_, "suggested-action");
    g_signal_connect(choose_folder_button_, "clicked", G_CALLBACK(on_choose_folder), this);
    gtk_box_append(GTK_BOX(btn_box), choose_folder_button_);

    gtk_box_append(GTK_BOX(panel_), btn_box);

    // ─── File list ──────────────────────────────────────────────────────
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 120);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    file_list_box_ = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(file_list_box_), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), file_list_box_);
    gtk_box_append(GTK_BOX(panel_), scroll);

    // ─── Action row ─────────────────────────────────────────────────────
    GtkWidget* action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(action_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(action_box, 4);

    send_button_ = gtk_button_new_with_label("Start Sharing");
    gtk_widget_add_css_class(send_button_, "suggested-action");
    gtk_widget_set_sensitive(send_button_, FALSE);
    g_signal_connect(send_button_, "clicked", G_CALLBACK(on_send_clicked), this);
    gtk_box_append(GTK_BOX(action_box), send_button_);

    clear_button_ = gtk_button_new_with_label("🗑️ Clear");
    gtk_widget_add_css_class(clear_button_, "destructive-action");
    g_signal_connect(clear_button_, "clicked", G_CALLBACK(on_clear_clicked), this);
    gtk_box_append(GTK_BOX(action_box), clear_button_);

    // Cancel button (hidden by default, shown when sharing)
    GtkWidget* cancel_button = gtk_button_new_with_label("⏹ Cancel Sharing");
    gtk_widget_add_css_class(cancel_button, "destructive-action");
    gtk_widget_set_visible(cancel_button, FALSE);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_cancel_clicked), this);
    gtk_box_append(GTK_BOX(action_box), cancel_button);
    // Store as data on send_button so we can toggle visibility
    g_object_set_data(G_OBJECT(send_button_), "cancel-btn", cancel_button);

    gtk_box_append(GTK_BOX(panel_), action_box);

    // ─── PIN display ────────────────────────────────────────────────────
    pin_label_ = gtk_label_new("");
    gtk_widget_add_css_class(pin_label_, "pin-display");
    gtk_widget_set_visible(pin_label_, FALSE);
    gtk_box_append(GTK_BOX(panel_), pin_label_);

    // ─── Status / Progress ──────────────────────────────────────────────
    status_label_ = gtk_label_new("");
    gtk_widget_add_css_class(status_label_, "status-text");
    gtk_box_append(GTK_BOX(panel_), status_label_);

    progress_bar_ = gtk_progress_bar_new();
    gtk_widget_set_visible(progress_bar_, FALSE);
    gtk_box_append(GTK_BOX(panel_), progress_bar_);

    progress_label_ = gtk_label_new("");
    gtk_widget_add_css_class(progress_label_, "status-text");
    gtk_box_append(GTK_BOX(panel_), progress_label_);
}

FileSenderPanel::~FileSenderPanel() {
    if (cancel_flag_) cancel_flag_->store(true);
    server_running_ = false;
    fd_cancel_server();
}

void FileSenderPanel::add_path(const std::string& path) {
    fs::path p(path);
    if (fs::is_directory(p)) {
        for (const auto& entry : fs::recursive_directory_iterator(p)) {
            if (entry.is_regular_file()) {
                queued_files_.push_back(entry.path().string());
            }
        }
    } else if (fs::is_regular_file(p)) {
        queued_files_.push_back(path);
    }
    update_file_list_ui();
}

void FileSenderPanel::clear_files() {
    queued_files_.clear();
    update_file_list_ui();
}

void FileSenderPanel::update_file_list_ui() {
    // Clear existing rows
    GtkWidget* child;
    while ((child = gtk_widget_get_first_child(file_list_box_)) != nullptr) {
        gtk_list_box_remove(GTK_LIST_BOX(file_list_box_), child);
    }

    for (const auto& file : queued_files_) {
        fs::path p(file);
        std::string display = "📄 " + p.filename().string();

        // Get file size
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

        GtkWidget* label = gtk_label_new(display.c_str());
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_add_css_class(label, "file-item");
        gtk_list_box_append(GTK_LIST_BOX(file_list_box_), label);
    }

    gtk_widget_set_sensitive(send_button_, !queued_files_.empty() && !server_running_);
}

void FileSenderPanel::start_server() {
    if (queued_files_.empty() || server_running_) return;

    server_running_ = true;

    // Toggle button visibility
    gtk_widget_set_visible(send_button_, FALSE);
    gtk_widget_set_visible(clear_button_, FALSE);
    GtkWidget* cancel_btn = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(send_button_), "cancel-btn"));
    if (cancel_btn) gtk_widget_set_visible(cancel_btn, TRUE);

    gtk_widget_set_sensitive(choose_file_button_, FALSE);
    gtk_widget_set_sensitive(choose_folder_button_, FALSE);
    gtk_widget_set_visible(progress_bar_, TRUE);
    gtk_widget_set_visible(pin_label_, TRUE);

    // Prepare arrays for C API
    std::vector<const char*> c_paths;
    for (const auto& filepath : queued_files_) {
        c_paths.push_back(filepath.c_str());
    }

    // Capture widget pointers for callbacks
    GtkWidget* pin_lbl = pin_label_;
    GtkWidget* status_lbl = status_label_;
    GtkWidget* progress_br = progress_bar_;
    GtkWidget* progress_lbl = progress_label_;
    GtkWidget* send_btn = send_button_;
    GtkWidget* clear_btn_widget = clear_button_;
    GtkWidget* choose_file_btn = choose_file_button_;
    GtkWidget* choose_folder_btn = choose_folder_button_;

    // Set callback lambdas (Note: using global capture-less lambdas or passing static context is typically required for pure C APIs,
    // but our C API is in the same process and currently allows std::function bound lambdas since they are invoked synchronously
    // in the wrapped C++ implementation. Actually, `fd_start_server` takes raw function pointers, so we must use static functions
    // or captureless lambdas. Since we need context, we have to stash the context pointers globally or accept the limitation.
    // Wait, the C API doesn't have a `void* user_data`! This is a flaw in the C API design for GUI integration.
    // Let's create a static thread-local or global reference just for this panel since there's only one sender panel.)

    // --- Temporary Hack for Context-less C callbacks ---
    static FileSenderPanel* current_panel;
    static GtkWidget* g_pin_lbl;
    static GtkWidget* g_status_lbl;
    static GtkWidget* g_progress_br;
    static GtkWidget* g_progress_lbl;
    static GtkWidget* g_send_btn;
    static GtkWidget* g_clear_btn_widget;
    static GtkWidget* g_cancel_btn;
    static GtkWidget* g_choose_file_btn;
    static GtkWidget* g_choose_folder_btn;
    static std::atomic<bool>* g_running_ptr;
    current_panel = this;
    g_pin_lbl = pin_lbl;
    g_status_lbl = status_lbl;
    g_progress_br = progress_br;
    g_progress_lbl = progress_lbl;
    g_send_btn = send_btn;
    g_clear_btn_widget = clear_btn_widget;
    g_cancel_btn = cancel_btn;
    g_choose_file_btn = choose_file_btn;
    g_choose_folder_btn = choose_folder_btn;
    g_running_ptr = &server_running_;

    auto ready_cb = [](const char* ip, int port, int pin) {
        auto* d = new PinUpdateData{
            g_pin_lbl, g_status_lbl,
            "PIN: " + std::to_string(pin),
            "Listening on " + std::string(ip) + ":" + std::to_string(port) + " — Waiting for receiver..."
        };
        g_idle_add(update_pin_idle, d);
    };

    auto status_cb = [](const char* msg) {
        g_idle_add(update_status_idle, new StatusUpdateData{g_status_lbl, msg});
    };

    auto error_cb = [](const char* err) {
        g_idle_add(transfer_complete_idle, new TransferCompleteData{
            g_status_lbl, g_progress_br, g_progress_lbl, g_send_btn,
            "❌ Error: " + std::string(err)
        });
        
        struct ReenableData { GtkWidget *send, *clear, *cancel, *file, *folder; };
        g_idle_add(+[](gpointer data) -> gboolean {
            auto* d = static_cast<ReenableData*>(data);
            if (GTK_IS_WIDGET(d->send)) { gtk_widget_set_visible(d->send, TRUE); gtk_widget_set_sensitive(d->send, TRUE); }
            if (GTK_IS_WIDGET(d->clear)) gtk_widget_set_visible(d->clear, TRUE);
            if (GTK_IS_WIDGET(d->cancel)) gtk_widget_set_visible(d->cancel, FALSE);
            if (GTK_IS_WIDGET(d->file)) gtk_widget_set_sensitive(d->file, TRUE);
            if (GTK_IS_WIDGET(d->folder)) gtk_widget_set_sensitive(d->folder, TRUE);
            delete d;
            return G_SOURCE_REMOVE;
        }, new ReenableData{g_send_btn, g_clear_btn_widget, g_cancel_btn, g_choose_file_btn, g_choose_folder_btn});
        *g_running_ptr = false;
    };

    auto progress_cb = [](const char* filename, uint64_t transferred, uint64_t total, double speed) {
        double frac = (total > 0) ? (static_cast<double>(transferred) / total) : 0.0;
        int pct = static_cast<int>(frac * 100);
        char buf[128];
        snprintf(buf, sizeof(buf), "%d%% — %.1f MB/s — %s", pct, speed, filename);
        g_idle_add(update_progress_idle, new ProgressUpdateData{g_progress_br, g_progress_lbl, frac, buf});
    };

    auto complete_cb = []() {
        g_idle_add(transfer_complete_idle, new TransferCompleteData{
            g_status_lbl, g_progress_br, g_progress_lbl, g_send_btn,
            "✅ All files transferred successfully!"
        });
        
        struct ReenableData { GtkWidget *send, *clear, *cancel, *file, *folder; };
        g_idle_add(+[](gpointer data) -> gboolean {
            auto* d = static_cast<ReenableData*>(data);
            if (GTK_IS_WIDGET(d->send)) { gtk_widget_set_visible(d->send, TRUE); gtk_widget_set_sensitive(d->send, TRUE); }
            if (GTK_IS_WIDGET(d->clear)) gtk_widget_set_visible(d->clear, TRUE);
            if (GTK_IS_WIDGET(d->cancel)) gtk_widget_set_visible(d->cancel, FALSE);
            if (GTK_IS_WIDGET(d->file)) gtk_widget_set_sensitive(d->file, TRUE);
            if (GTK_IS_WIDGET(d->folder)) gtk_widget_set_sensitive(d->folder, TRUE);
            delete d;
            return G_SOURCE_REMOVE;
        }, new ReenableData{g_send_btn, g_clear_btn_widget, g_cancel_btn, g_choose_file_btn, g_choose_folder_btn});
        *g_running_ptr = false;
    };

    fd_start_server(c_paths.data(), c_paths.size(),
                    ready_cb, status_cb, error_cb, progress_cb, complete_cb);
}

void FileSenderPanel::cancel_server() {
    fd_cancel_server();
    gtk_label_set_text(GTK_LABEL(pin_label_), "");
    gtk_widget_set_visible(pin_label_, FALSE);
    gtk_widget_set_visible(progress_bar_, FALSE);
    gtk_label_set_text(GTK_LABEL(progress_label_), "");
    gtk_label_set_text(GTK_LABEL(status_label_), "Sharing cancelled.");
}

// ─── GTK Callbacks ──────────────────────────────────────────────────────────

void FileSenderPanel::on_choose_file(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<FileSenderPanel*>(user_data);

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkFileChooserNative* native = gtk_file_chooser_native_new(
        "Select Files", self->parent_window_,
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(native), TRUE);

    g_signal_connect(native, "response", G_CALLBACK(+[](GtkNativeDialog* dialog, int response, gpointer data) {
        if (response == GTK_RESPONSE_ACCEPT) {
            auto* panel = static_cast<FileSenderPanel*>(data);
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
    }), self);

    gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
G_GNUC_END_IGNORE_DEPRECATIONS
}

void FileSenderPanel::on_choose_folder(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<FileSenderPanel*>(user_data);

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkFileChooserNative* native = gtk_file_chooser_native_new(
        "Select Folder", self->parent_window_,
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Open", "_Cancel");

    g_signal_connect(native, "response", G_CALLBACK(+[](GtkNativeDialog* dialog, int response, gpointer data) {
        if (response == GTK_RESPONSE_ACCEPT) {
            auto* panel = static_cast<FileSenderPanel*>(data);
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
    }), self);

    gtk_native_dialog_show(GTK_NATIVE_DIALOG(native));
G_GNUC_END_IGNORE_DEPRECATIONS
}

void FileSenderPanel::on_send_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<FileSenderPanel*>(user_data);
    self->start_server();
}

void FileSenderPanel::on_clear_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<FileSenderPanel*>(user_data);
    self->clear_files();
    gtk_widget_set_visible(self->pin_label_, FALSE);
    gtk_widget_set_visible(self->progress_bar_, FALSE);
    gtk_label_set_text(GTK_LABEL(self->status_label_), "");
    gtk_label_set_text(GTK_LABEL(self->progress_label_), "");
}

void FileSenderPanel::on_cancel_clicked(GtkButton* /*button*/, gpointer user_data) {
    auto* self = static_cast<FileSenderPanel*>(user_data);
    self->cancel_server();
}

gboolean FileSenderPanel::on_drop(GtkDropTarget* /*target*/, const GValue* value,
                                   double /*x*/, double /*y*/, gpointer user_data) {
    auto* self = static_cast<FileSenderPanel*>(user_data);

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

} // namespace ui
