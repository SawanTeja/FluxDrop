#include "ui/main_window.hpp"
#include "ui/file_sender.hpp"
#include "ui/device_list.hpp"
#include <iostream>

namespace ui {

static const char* CSS_STYLE = R"(
window {
    background-color: #1a1a2e;
}

headerbar {
    background: linear-gradient(to right, #16213e, #0f3460);
    color: #e0e0e0;
    border-bottom: 1px solid #533483;
}

headerbar title {
    color: #e0e0e0;
    font-weight: bold;
}

stackswitcher button {
    color: #a0a0c0;
    background: transparent;
    border: none;
    border-radius: 8px;
    padding: 6px 16px;
    margin: 4px;
}

stackswitcher button:checked {
    background-color: #533483;
    color: white;
}

stackswitcher button:hover:not(:checked) {
    background-color: rgba(83, 52, 131, 0.3);
}

.drop-zone {
    background-color: #0f3460;
    border: 2px dashed #533483;
    border-radius: 16px;
    padding: 40px;
    margin: 16px;
}

.drop-zone-active {
    border-color: #e94560;
    background-color: #16213e;
}

.device-row {
    background-color: #16213e;
    border-radius: 10px;
    padding: 12px 16px;
    margin: 4px 8px;
}

.device-row:hover {
    background-color: #0f3460;
}

.pin-display {
    font-size: 32px;
    font-weight: bold;
    color: #e94560;
    padding: 16px;
}

.status-text {
    color: #a0a0b0;
    font-size: 14px;
}

.title-text {
    color: #e0e0e0;
    font-size: 18px;
    font-weight: bold;
}

.subtitle-text {
    color: #808090;
    font-size: 13px;
}

.file-item {
    background-color: #16213e;
    border-radius: 8px;
    padding: 8px 12px;
    margin: 2px 8px;
    color: #c0c0d0;
}

label {
    color: #c0c0d0;
}

button.suggested-action {
    background-color: #533483;
    color: white;
    border-radius: 10px;
    padding: 8px 20px;
    border: none;
    font-weight: bold;
}

button.suggested-action:hover {
    background-color: #6a42a0;
}

button.destructive-action {
    background-color: #e94560;
    color: white;
    border-radius: 10px;
    padding: 8px 20px;
    border: none;
}

button.destructive-action:hover {
    background-color: #ff5a75;
}

button.flat {
    color: #a0a0c0;
    background: transparent;
    border: none;
}

button.flat:hover {
    background-color: rgba(83, 52, 131, 0.3);
}

progressbar trough {
    background-color: #16213e;
    border-radius: 6px;
    min-height: 10px;
}

progressbar progress {
    background: linear-gradient(to right, #533483, #e94560);
    border-radius: 6px;
    min-height: 10px;
}

scrolledwindow {
    background-color: transparent;
}

entry {
    background-color: #16213e;
    color: #e0e0e0;
    border: 1px solid #533483;
    border-radius: 8px;
    padding: 8px 12px;
}

entry:focus {
    border-color: #e94560;
}

.section-box {
    background-color: rgba(15, 52, 96, 0.3);
    border-radius: 12px;
    padding: 16px;
    margin: 8px;
}
)";

void MainWindow::setup_css() {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, CSS_STYLE);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

void MainWindow::on_destroy(GtkWidget* widget, gpointer data) {
    auto* self = static_cast<MainWindow*>(data);
    if (self->receive_panel_) {
        self->receive_panel_->stop_discovery();
    }
}

MainWindow::MainWindow(GtkApplication* app) {
    setup_css();

    window_ = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window_), "FluxDrop");
    gtk_window_set_default_size(GTK_WINDOW(window_), 520, 650);

    // Header bar
    header_bar_ = gtk_header_bar_new();
    gtk_window_set_titlebar(GTK_WINDOW(window_), header_bar_);

    // Title label
    GtkWidget* title_label = gtk_label_new("FluxDrop");
    gtk_widget_add_css_class(title_label, "title-text");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header_bar_), title_label);

    // Stack
    stack_ = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack_), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT_RIGHT);
    gtk_stack_set_transition_duration(GTK_STACK(stack_), 300);

    // Create panels
    send_panel_ = new FileSenderPanel(GTK_WINDOW(window_));
    receive_panel_ = new DeviceListPanel(GTK_WINDOW(window_));

    gtk_stack_add_titled(GTK_STACK(stack_), send_panel_->get_widget(), "send", "Send File");
    gtk_stack_add_titled(GTK_STACK(stack_), receive_panel_->get_widget(), "receive", "Receive");

    // Stack switcher in header
    GtkWidget* switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack_));
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header_bar_), switcher);

    gtk_window_set_child(GTK_WINDOW(window_), stack_);

    g_signal_connect(window_, "destroy", G_CALLBACK(on_destroy), this);

    // Start device discovery automatically
    receive_panel_->start_discovery();

    gtk_window_present(GTK_WINDOW(window_));
}

MainWindow::~MainWindow() {
    delete send_panel_;
    delete receive_panel_;
}

static void activate_callback(GtkApplication* app, gpointer /*user_data*/) {
    // MainWindow allocated on heap, cleaned up on window destroy
    new MainWindow(app);
}

int run_gui(int argc, char* argv[]) {
    GtkApplication* app = gtk_application_new("dev.fluxdrop.app", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate_callback), nullptr);
    int status = g_application_run(G_APPLICATION(app), 0, nullptr);
    g_object_unref(app);
    return status;
}

} // namespace ui
