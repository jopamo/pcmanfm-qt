/*
 * Capstone-based disassembly viewer
 * src/ui/disassemblywindow.h
 */

#ifndef PCMANFM_DISASSEMBLYWINDOW_H
#define PCMANFM_DISASSEMBLYWINDOW_H

#include <QMainWindow>
#include <QString>

#include <optional>

#include <capstone/capstone.h>

class QLabel;
class QPlainTextEdit;

namespace PCManFM {

class DisassemblyWindow : public QMainWindow {
    Q_OBJECT

   public:
    explicit DisassemblyWindow(QWidget* parent = nullptr);
    ~DisassemblyWindow() override = default;

    bool openFile(const QString& path, QString& errorOut);

   private:
    struct CapstoneConfig {
        cs_arch arch = CS_ARCH_X86;
        cs_mode mode = CS_MODE_64;
        QString label;
        quint64 baseAddress = 0;
    };

    void setupUi();
    std::optional<CapstoneConfig> detectConfig(const QByteArray& data) const;
    CapstoneConfig defaultConfig() const;
    quint64 parseElfEntry(const QByteArray& data, bool is64Bit, bool littleEndian) const;
    bool disassemble(const QByteArray& data, const CapstoneConfig& cfg, QString& outText, QString& errorOut) const;
    void updateLabels(const QString& path, const CapstoneConfig& cfg, bool truncated, qsizetype bytesRead);

    QPlainTextEdit* output_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* configLabel_ = nullptr;
    QString currentPath_;
};

}  // namespace PCManFM

#endif  // PCMANFM_DISASSEMBLYWINDOW_H
