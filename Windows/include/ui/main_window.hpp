#pragma once

#include <QMainWindow>

namespace ui {

class SessionPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

  private:
    SessionPanel* session_panel_;

    void setup_stylesheet();
};

} // namespace ui
