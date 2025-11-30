/*
 * Qt-based file properties dialog for PCManFM-Qt
 * pcmanfm-qt/src/ui/filepropertiesdialog.h
 */

#pragma once

#include <QDialog>
#include <QList>

#include "../core/ifileinfo.h"

class QTabWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QCheckBox;

class FilePropertiesDialog : public QDialog {
    Q_OBJECT

   public:
    explicit FilePropertiesDialog(const QList<std::shared_ptr<IFileInfo>>& fileInfos, QWidget* parent = nullptr);
    explicit FilePropertiesDialog(std::shared_ptr<IFileInfo> fileInfo, QWidget* parent = nullptr);

   private:
    void onApplyClicked();
    void onCancelClicked();

   private:
    void setupUI();
    void populateFileInfo();
    void setupPermissionsTab();
    void setupGeneralTab();

    QList<std::shared_ptr<IFileInfo>> m_fileInfos;
    QTabWidget* m_tabWidget;
    QLabel* m_iconLabel;
    QLabel* m_nameLabel;
    QLabel* m_typeLabel;
    QLabel* m_sizeLabel;
    QLabel* m_locationLabel;
    QLabel* m_modifiedLabel;
    QPushButton* m_applyButton;
    QPushButton* m_cancelButton;
    QLineEdit* m_ownerEdit;
    QLineEdit* m_groupEdit;
    QCheckBox* m_ownerRead;
    QCheckBox* m_ownerWrite;
    QCheckBox* m_ownerExec;
    QCheckBox* m_groupRead;
    QCheckBox* m_groupWrite;
    QCheckBox* m_groupExec;
    QCheckBox* m_otherRead;
    QCheckBox* m_otherWrite;
    QCheckBox* m_otherExec;
    QCheckBox* m_recursiveCheck;
};