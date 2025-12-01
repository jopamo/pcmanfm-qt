/*
 * Capstone-based disassembly viewer
 * src/ui/disassemblywindow.h
 */

#ifndef PCMANFM_DISASSEMBLYWINDOW_H
#define PCMANFM_DISASSEMBLYWINDOW_H

#include <QMainWindow>
#include <QString>

#include <memory>
#include <optional>

class QLabel;
class QTableView;

namespace PCManFM {

class BinaryDocument;
class DisasmModel;

class DisassemblyWindow : public QMainWindow {
    Q_OBJECT

   public:
    explicit DisassemblyWindow(QWidget* parent = nullptr);
    ~DisassemblyWindow() override = default;

    bool openFile(const QString& path, QString& errorOut);

   private:
    void setupUi();
    bool refresh(quint64 offset = 0);
    void updateLabels(const QString& path, bool truncated, qsizetype bytesRead);

    std::unique_ptr<BinaryDocument> doc_;
    std::unique_ptr<DisasmModel> model_;
    QTableView* view_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QString currentPath_;
};

}  // namespace PCManFM

#endif  // PCMANFM_DISASSEMBLYWINDOW_H
