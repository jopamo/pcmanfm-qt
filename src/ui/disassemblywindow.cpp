/*
 * Capstone-based disassembly viewer
 * src/ui/disassemblywindow.cpp
 */

#include "disassemblywindow.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTextCursor>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtEndian>

#include <cstring>
#include <QStringList>

namespace PCManFM {

namespace {
// Avoid loading huge binaries into memory at once; trim to a reasonable preview size.
constexpr qsizetype kMaxBytes = 512 * 1024;  // 512 KiB
}  // namespace

DisassemblyWindow::DisassemblyWindow(QWidget* parent) : QMainWindow(parent) {
    setupUi();
}

void DisassemblyWindow::setupUi() {
    setWindowTitle(tr("Disassembly"));
    setAttribute(Qt::WA_DeleteOnClose);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    pathLabel_ = new QLabel(central);
    pathLabel_->setWordWrap(true);
    layout->addWidget(pathLabel_);

    configLabel_ = new QLabel(central);
    configLabel_->setWordWrap(true);
    layout->addWidget(configLabel_);

    output_ = new QPlainTextEdit(central);
    output_->setReadOnly(true);
    output_->setLineWrapMode(QPlainTextEdit::NoWrap);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(output_);

    setCentralWidget(central);

    auto* toolbar = addToolBar(tr("Disassembly"));
    toolbar->setMovable(false);

    auto* copyAction = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Copy All"));
    connect(copyAction, &QAction::triggered, this, [this] {
        if (output_) {
            QApplication::clipboard()->setText(output_->toPlainText());
        }
    });

    auto* reloadAction = toolbar->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), tr("Reload"));
    connect(reloadAction, &QAction::triggered, this, [this] {
        if (currentPath_.isEmpty()) {
            return;
        }
        QString error;
        if (!openFile(currentPath_, error) && !error.isEmpty()) {
            QMessageBox::warning(this, tr("Disassembly"), error);
        }
    });
}

bool DisassemblyWindow::openFile(const QString& path, QString& errorOut) {
    errorOut.clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errorOut = tr("Unable to open %1").arg(path);
        return false;
    }

    const QByteArray data = file.read(kMaxBytes);
    if (data.isEmpty()) {
        errorOut = tr("File is empty or could not be read.");
        return false;
    }

    const bool truncated = !file.atEnd();
    auto cfg = detectConfig(data).value_or(defaultConfig());
    if (cfg.label.isEmpty()) {
        cfg.label = tr("Default Capstone configuration");
    }

    QString disassembly;
    if (!disassemble(data, cfg, disassembly, errorOut)) {
        return false;
    }

    if (output_) {
        output_->setPlainText(disassembly);
        output_->moveCursor(QTextCursor::Start);
    }

    updateLabels(path, cfg, truncated, data.size());
    currentPath_ = path;

    if (truncated) {
        statusBar()->showMessage(tr("Showing first %1 bytes (truncated).").arg(data.size()));
    }
    else {
        statusBar()->clearMessage();
    }

    return true;
}

std::optional<DisassemblyWindow::CapstoneConfig> DisassemblyWindow::detectConfig(const QByteArray& data) const {
    if (data.size() < 20) {
        return std::nullopt;
    }

    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.constData());
    if (!(bytes[0] == 0x7F && bytes[1] == 'E' && bytes[2] == 'L' && bytes[3] == 'F')) {
        return std::nullopt;
    }

    const quint8 elfClass = bytes[4];
    const quint8 elfData = bytes[5];
    const bool littleEndian = elfData == 1;
    const bool is64 = elfClass == 2;

    quint16 machine = 0;
    std::memcpy(&machine, bytes + 18, sizeof machine);
    machine = littleEndian ? qFromLittleEndian(machine) : qFromBigEndian(machine);

    CapstoneConfig cfg;
    cfg.baseAddress = parseElfEntry(data, is64, littleEndian);

    switch (machine) {
        case 0x3E:  // EM_X86_64
            cfg.arch = CS_ARCH_X86;
            cfg.mode = CS_MODE_64;
            cfg.label = tr("ELF: x86-64");
            break;
        case 0x03:  // EM_386
            cfg.arch = CS_ARCH_X86;
            cfg.mode = CS_MODE_32;
            cfg.label = tr("ELF: x86-32");
            break;
        case 0x28:  // EM_ARM
            cfg.arch = CS_ARCH_ARM;
            cfg.mode = CS_MODE_ARM;
            cfg.label = tr("ELF: ARM");
            break;
        case 0xB7:  // EM_AARCH64
            cfg.arch = CS_ARCH_AARCH64;
            cfg.mode = CS_MODE_ARM;
            cfg.label = tr("ELF: AArch64");
            break;
        case 0x08:  // EM_MIPS
            cfg.arch = CS_ARCH_MIPS;
            cfg.mode = is64 ? CS_MODE_MIPS64 : CS_MODE_MIPS32;
            cfg.label = is64 ? tr("ELF: MIPS64") : tr("ELF: MIPS32");
            break;
        case 0x14:  // EM_PPC
            cfg.arch = CS_ARCH_PPC;
            cfg.mode = CS_MODE_32;
            cfg.label = tr("ELF: PowerPC");
            break;
        case 0x15:  // EM_PPC64
            cfg.arch = CS_ARCH_PPC;
            cfg.mode = CS_MODE_64;
            cfg.label = tr("ELF: PowerPC64");
            break;
        case 0xF3:  // EM_RISCV
            cfg.arch = CS_ARCH_RISCV;
            cfg.mode = is64 ? CS_MODE_RISCV64 : CS_MODE_RISCV32;
            cfg.label = is64 ? tr("ELF: RISC-V64") : tr("ELF: RISC-V32");
            break;
        default:
            return std::nullopt;
    }

    if (!littleEndian) {
        cfg.mode = static_cast<cs_mode>(cfg.mode | CS_MODE_BIG_ENDIAN);
        cfg.label += tr(" (BE)");
    }

    return cfg;
}

DisassemblyWindow::CapstoneConfig DisassemblyWindow::defaultConfig() const {
    CapstoneConfig cfg;
    cfg.arch = CS_ARCH_X86;
    cfg.mode = (sizeof(void*) == 8) ? CS_MODE_64 : CS_MODE_32;
    cfg.label = (cfg.mode == CS_MODE_64) ? tr("Default: x86-64") : tr("Default: x86-32");
    cfg.baseAddress = 0;
    return cfg;
}

quint64 DisassemblyWindow::parseElfEntry(const QByteArray& data, bool is64Bit, bool littleEndian) const {
    if (is64Bit) {
        if (data.size() < 32) {
            return 0;
        }
        quint64 entry = 0;
        std::memcpy(&entry, data.constData() + 24, sizeof entry);
        return littleEndian ? qFromLittleEndian(entry) : qFromBigEndian(entry);
    }

    if (data.size() < 28) {
        return 0;
    }

    quint32 entry = 0;
    std::memcpy(&entry, data.constData() + 24, sizeof entry);
    return static_cast<quint64>(littleEndian ? qFromLittleEndian(entry) : qFromBigEndian(entry));
}

bool DisassemblyWindow::disassemble(const QByteArray& data,
                                    const CapstoneConfig& cfg,
                                    QString& outText,
                                    QString& errorOut) const {
    csh handle = 0;
    cs_err openErr = cs_open(cfg.arch, cfg.mode, &handle);
    if (openErr != CS_ERR_OK) {
        errorOut = tr("Capstone init failed: %1").arg(QString::fromLatin1(cs_strerror(openErr)));
        return false;
    }

    cs_option(handle, CS_OPT_DETAIL, CS_OPT_OFF);

    cs_insn* insn = nullptr;
    const size_t count = cs_disasm(handle, reinterpret_cast<const uint8_t*>(data.constData()),
                                   static_cast<size_t>(data.size()), cfg.baseAddress, 0, &insn);

    if (count == 0) {
        errorOut = tr("Capstone failed to disassemble: %1").arg(QString::fromLatin1(cs_strerror(cs_errno(handle))));
        cs_close(&handle);
        return false;
    }

    QStringList lines;
    lines.reserve(static_cast<int>(count));
    for (size_t i = 0; i < count; ++i) {
        const cs_insn& in = insn[i];
        QString bytes;
        for (uint8_t j = 0; j < in.size; ++j) {
            bytes += QStringLiteral("%1 ").arg(in.bytes[j], 2, 16, QLatin1Char('0'));
        }
        if (!bytes.isEmpty()) {
            bytes.chop(1);  // remove trailing space
        }

        const QString mnemonic = QString::fromLatin1(in.mnemonic);
        const QString operands = QString::fromLatin1(in.op_str);
        QString line = QStringLiteral("0x%1  %2  %3 %4").arg(in.address, 0, 16).arg(bytes).arg(mnemonic).arg(operands);
        lines.append(line.trimmed());
    }

    outText = lines.join(QLatin1Char('\n'));
    cs_free(insn, count);
    cs_close(&handle);
    return true;
}

void DisassemblyWindow::updateLabels(const QString& path,
                                     const CapstoneConfig& cfg,
                                     bool truncated,
                                     qsizetype bytesRead) {
    if (pathLabel_) {
        pathLabel_->setText(tr("Path: %1").arg(path));
    }
    if (configLabel_) {
        const QString truncatedText = truncated ? tr(" (showing first %1 bytes)").arg(bytesRead) : QString();
        configLabel_->setText(tr("Capstone: %1%2").arg(cfg.label, truncatedText));
    }
    setWindowTitle(tr("Disassembly - %1").arg(QFileInfo(path).fileName()));
}

}  // namespace PCManFM
