#pragma once

#include <QMainWindow>
#include <QTabWidget>

namespace ui {

class FileSenderPanel;
class DeviceListPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    QTabWidget* tab_widget_;
    FileSenderPanel* send_panel_;
    DeviceListPanel* receive_panel_;

    void setup_stylesheet();
};

} // namespace ui
