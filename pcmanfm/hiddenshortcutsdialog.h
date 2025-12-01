/*
 * Dialog for showing hidden shortcuts
 * pcmanfm/hiddenshortcutsdialog.h
 */

#ifndef PCMANFM_HIDDENSHORTCUTSDIALOG_H
#define PCMANFM_HIDDENSHORTCUTSDIALOG_H

#include <QDialog>

namespace PCManFM {

class HiddenShortcutsDialog : public QDialog {
    Q_OBJECT

   public:
    explicit HiddenShortcutsDialog(QWidget* parent = nullptr);
    ~HiddenShortcutsDialog();
};

}  // namespace PCManFM

#endif  // PCMANFM_HIDDENSHORTCUTSDIALOG_H
