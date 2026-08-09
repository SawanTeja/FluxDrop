#include "ui/main_window.hpp"
#include "ui/file_sender.hpp"
#include "ui/device_list.hpp"
#include "logger.hpp"
#include <QApplication>
#include <QIcon>

namespace ui {

static const char* GLOBAL_STYLESHEET = R"(
/* ── Main window ────────────────────────────────────────────── */
QMainWindow {
    background-color: #1a1a2e;
}

/* ── Tab widget (replaces GtkStack + GtkStackSwitcher) ──────── */
QTabWidget::pane {
    border: none;
    background-color: #1a1a2e;
}

QTabBar {
    background-color: #202030;
    qproperty-drawBase: 0;
}

QTabBar::tab {
    color: #a0a0c0;
    background: transparent;
    border: none;
    border-radius: 8px;
    padding: 6px 16px;
    margin: 4px;
    font-size: 13px;
}

QTabBar::tab:selected {
    background-color: #533483;
    color: white;
}

QTabBar::tab:hover:!selected {
    background-color: rgba(83, 52, 131, 76);
}

/* ── Labels ─────────────────────────────────────────────────── */
QLabel {
    color: #c0c0d0;
}

QLabel[cssClass="title-text"] {
    color: #e0e0e0;
    font-size: 18px;
    font-weight: bold;
}

QLabel[cssClass="subtitle-text"] {
    color: #808090;
    font-size: 13px;
}

QLabel[cssClass="status-text"] {
    color: #a0a0b0;
    font-size: 14px;
}

QLabel[cssClass="pin-display"] {
    font-size: 32px;
    font-weight: bold;
    color: #e94560;
    padding: 16px;
}

/* ── Buttons ────────────────────────────────────────────────── */
QPushButton[cssClass="suggested-action"] {
    background-color: #533483;
    color: white;
    border-radius: 10px;
    padding: 8px 20px;
    border: none;
    font-weight: bold;
    font-size: 13px;
}

QPushButton[cssClass="suggested-action"]:hover {
    background-color: #6a42a0;
}

QPushButton[cssClass="suggested-action"]:disabled {
    background-color: #3a2460;
    color: #808090;
}

QPushButton[cssClass="destructive-action"] {
    background-color: #e94560;
    color: white;
    border-radius: 10px;
    padding: 8px 20px;
    border: none;
    font-weight: bold;
    font-size: 13px;
}

QPushButton[cssClass="destructive-action"]:hover {
    background-color: #ff5a75;
}

QPushButton[cssClass="flat"] {
    color: #a0a0c0;
    background: transparent;
    border: none;
    font-size: 13px;
}

QPushButton[cssClass="flat"]:hover {
    background-color: rgba(83, 52, 131, 76);
    border-radius: 8px;
}

/* ── Progress bar ───────────────────────────────────────────── */
QProgressBar {
    background-color: #16213e;
    border-radius: 6px;
    min-height: 10px;
    max-height: 10px;
    text-align: center;
    color: transparent;
    border: none;
}

QProgressBar::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #533483, stop:1 #e94560);
    border-radius: 6px;
}

/* ── Line edits (entries) ───────────────────────────────────── */
QLineEdit {
    background-color: #16213e;
    color: #e0e0e0;
    border: 1px solid #533483;
    border-radius: 8px;
    padding: 8px 12px;
    font-size: 13px;
}

QLineEdit:focus {
    border-color: #e94560;
}

/* ── List widget (replaces GtkListBox) ──────────────────────── */
QListWidget {
    background-color: #16213e;
    border: 1px solid #533483;
    border-radius: 8px;
    outline: none;
}

QListWidget::item {
    background-color: transparent;
    color: #c0c0d0;
    border-radius: 8px;
    padding: 8px 12px;
    margin: 2px 4px;
}

QListWidget::item:selected {
    background-color: #0f3460;
}

QListWidget::item:hover {
    background-color: #0f3460;
}

/* ── Scroll bars ────────────────────────────────────────────── */
QScrollBar:vertical {
    background: transparent;
    width: 8px;
    margin: 0;
}

QScrollBar::handle:vertical {
    background: #533483;
    border-radius: 4px;
    min-height: 20px;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}

/* ── Drop zone ──────────────────────────────────────────────── */
QFrame[cssClass="drop-zone"] {
    background-color: #0f3460;
    border: 2px dashed #533483;
    border-radius: 16px;
    padding: 40px;
}

QFrame[cssClass="drop-zone-active"] {
    border-color: #e94560;
    background-color: #16213e;
}

/* ── Device row ─────────────────────────────────────────────── */
QFrame[cssClass="device-row"] {
    background-color: #16213e;
    border-radius: 10px;
    padding: 12px 16px;
}

QFrame[cssClass="device-row"]:hover {
    background-color: #0f3460;
}

/* ── Section box ────────────────────────────────────────────── */
QFrame[cssClass="section-box"] {
    background-color: rgba(15, 52, 96, 76);
    border-radius: 12px;
    padding: 16px;
}

/* ── Dialogs ────────────────────────────────────────────────── */
QDialog {
    background-color: #1a1a2e;
}
)";

void MainWindow::setup_stylesheet() {
    qApp->setStyleSheet(GLOBAL_STYLESHEET);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {

    setup_stylesheet();

    setWindowTitle("FluxDrop");
    resize(520, 650);
    setWindowIcon(QIcon("assets/fluxdroplogo.ico"));

    tab_widget_ = new QTabWidget(this);
    tab_widget_->setDocumentMode(true);

    send_panel_ = new FileSenderPanel(this);
    receive_panel_ = new DeviceListPanel(this);

    tab_widget_->addTab(send_panel_, "Send File");
    tab_widget_->addTab(receive_panel_, "Receive");

    setCentralWidget(tab_widget_);

    receive_panel_->start_discovery();

    FD_LOG("MainWindow created");
}

MainWindow::~MainWindow() {
    FD_LOG("~MainWindow — cleaning up");
    if (receive_panel_) {
        receive_panel_->stop_discovery();
    }
    FD_LOG("~MainWindow — done");
}

} // namespace ui
