#include "ui/transfer_dialog.hpp"

namespace ui {

TransferDialog::TransferDialog(GtkWindow* parent, const std::string& title) {
    dialog_ = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog_), title.c_str());
    gtk_window_set_default_size(GTK_WINDOW(dialog_), 400, 200);
    gtk_window_set_modal(GTK_WINDOW(dialog_), TRUE);
    if (parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog_), parent);
    }

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(vbox, 24);
    gtk_widget_set_margin_end(vbox, 24);
    gtk_widget_set_margin_top(vbox, 24);
    gtk_widget_set_margin_bottom(vbox, 24);

    filename_label_ = gtk_label_new("");
    gtk_widget_add_css_class(filename_label_, "title-text");
    gtk_label_set_ellipsize(GTK_LABEL(filename_label_), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(vbox), filename_label_);

    status_label_ = gtk_label_new("Preparing...");
    gtk_widget_add_css_class(status_label_, "status-text");
    gtk_box_append(GTK_BOX(vbox), status_label_);

    progress_bar_ = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress_bar_), FALSE);
    gtk_box_append(GTK_BOX(vbox), progress_bar_);

    progress_label_ = gtk_label_new("0%");
    gtk_widget_add_css_class(progress_label_, "status-text");
    gtk_box_append(GTK_BOX(vbox), progress_label_);

    gtk_window_set_child(GTK_WINDOW(dialog_), vbox);
}

void TransferDialog::set_filename(const std::string& filename) {
    gtk_label_set_text(GTK_LABEL(filename_label_), filename.c_str());
}

void TransferDialog::set_progress(double fraction, const std::string& text) {
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar_), fraction);
    gtk_label_set_text(GTK_LABEL(progress_label_), text.c_str());
}

void TransferDialog::set_status(const std::string& status) {
    gtk_label_set_text(GTK_LABEL(status_label_), status.c_str());
}

void TransferDialog::show() {
    gtk_window_present(GTK_WINDOW(dialog_));
}

void TransferDialog::close() {
    gtk_window_close(GTK_WINDOW(dialog_));
}

} // namespace ui
