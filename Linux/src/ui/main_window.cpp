#include "ui/main_window.hpp"
#include "logger.hpp"
#include "ui/session_panel.hpp"

namespace ui {

static const char* CSS_STYLE = R"(
window {
    background-color: #1a1a2e;
}

.top-panel {
    background-color: #202030;
    border-bottom: 1px solid #533483;
    padding: 8px;
}

.drop-zone {
    background-color: #0f3460;
    border: 2px dashed #533483;
    border-radius: 16px;
    padding: 40px;
    margin: 8px;
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
    margin: 2px 4px;
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
    font-size: 13px;
}

button.suggested-action:hover {
    background-color: #6a42a0;
}

button.suggested-action:disabled {
    background-color: #3a2460;
    color: #808090;
}

button.destructive-action {
    background-color: #e94560;
    color: white;
    border-radius: 10px;
    padding: 8px 20px;
    border: none;
    font-weight: bold;
    font-size: 13px;
}

button.destructive-action:hover {
    background-color: #ff5a75;
}

button.flat {
    color: #a0a0c0;
    background: transparent;
    border: none;
    font-size: 13px;
    padding: 6px 12px;
    border-radius: 8px;
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

scrolledwindow.file-list-scroll {
    background-color: #16213e;
    border: 1px solid #533483;
    border-radius: 8px;
}

list.file-list {
    background-color: transparent;
}

entry {
    background-color: #16213e;
    color: #e0e0e0;
    border: 1px solid #533483;
    border-radius: 8px;
    padding: 8px 12px;
    font-size: 13px;
}

entry:focus {
    border-color: #e94560;
}

.section-box {
    background-color: rgba(15, 52, 96, 0.3);
    border-radius: 12px;
    padding: 16px;
    margin: 8px 0;
}
)";

void MainWindow::setup_css() {
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);

    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, CSS_STYLE);
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void MainWindow::on_destroy(GtkWidget* /*widget*/, gpointer data) {
    FD_LOG("MainWindow::on_destroy — cleaning up");
    auto* self = static_cast<MainWindow*>(data);
    delete self;
    FD_LOG("MainWindow::on_destroy — done");
}

MainWindow::MainWindow(GtkApplication* app) {
    setup_css();

    window_ = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window_), "FluxDrop");
    gtk_window_set_default_size(GTK_WINDOW(window_), 520, 650);

    GtkIconTheme* theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    gtk_icon_theme_add_search_path(theme, "assets");
    gtk_window_set_default_icon_name("fluxdroplogo");
    gtk_window_set_icon_name(GTK_WINDOW(window_), "fluxdroplogo");

    session_panel_ = new SessionPanel(GTK_WINDOW(window_));
    gtk_window_set_child(GTK_WINDOW(window_), session_panel_->get_widget());

    g_signal_connect(window_, "destroy", G_CALLBACK(on_destroy), this);

    gtk_window_present(GTK_WINDOW(window_));
    FD_LOG("MainWindow created");
}

MainWindow::~MainWindow() {
    FD_LOG("~MainWindow — deleting panels");
    delete session_panel_;
    session_panel_ = nullptr;
    FD_LOG("~MainWindow — done");
}

static void activate_callback(GtkApplication* app, gpointer /*user_data*/) {
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
