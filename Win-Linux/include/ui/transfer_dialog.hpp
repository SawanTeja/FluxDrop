#pragma once

#include <gtk/gtk.h>
#include <string>

namespace ui {

class TransferDialog {
public:
    TransferDialog(GtkWindow* parent, const std::string& title);

    void set_filename(const std::string& filename);
    void set_progress(double fraction, const std::string& text);
    void set_status(const std::string& status);
    void show();
    void close();

    GtkWidget* get_widget() const { return dialog_; }

private:
    GtkWidget* dialog_;
    GtkWidget* filename_label_;
    GtkWidget* progress_bar_;
    GtkWidget* progress_label_;
    GtkWidget* status_label_;
};

} // namespace ui
