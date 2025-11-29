#include "bulkrename.h"

#include <libfm-qt6/utilities.h>

#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>

namespace PCManFM {

BulkRenameDialog::BulkRenameDialog(QWidget* parent, Qt::WindowFlags flags) : QDialog(parent, flags) {
    ui.setupUi(this);
    ui.lineEdit->setFocus();

    if (auto* okButton = ui.buttonBox->button(QDialogButtonBox::Ok)) {
        connect(okButton, &QAbstractButton::clicked, this, &QDialog::accept);
    }
    if (auto* cancelButton = ui.buttonBox->button(QDialogButtonBox::Cancel)) {
        connect(cancelButton, &QAbstractButton::clicked, this, &QDialog::reject);
    }

    // make the groupboxes mutually exclusive so only one rename mode is active
    connect(ui.serialGroupBox, &QGroupBox::clicked, this, [this](bool checked) {
        if (!checked) {
            ui.serialGroupBox->setChecked(true);
        }
        ui.replaceGroupBox->setChecked(false);
        ui.caseGroupBox->setChecked(false);
    });

    connect(ui.replaceGroupBox, &QGroupBox::clicked, this, [this](bool checked) {
        if (!checked) {
            ui.replaceGroupBox->setChecked(true);
        }
        ui.serialGroupBox->setChecked(false);
        ui.caseGroupBox->setChecked(false);
    });

    connect(ui.caseGroupBox, &QGroupBox::clicked, this, [this](bool checked) {
        if (!checked) {
            ui.caseGroupBox->setChecked(true);
        }
        ui.serialGroupBox->setChecked(false);
        ui.replaceGroupBox->setChecked(false);
    });

    resize(minimumSize());
    // limit vertical resizing to keep the dialog compact
    setMaximumHeight(minimumSizeHint().height());
}

void BulkRenameDialog::setState(const QString& baseName, const QString& findStr, const QString& replaceStr,
                                bool replacement, bool caseChange, bool zeroPadding, bool respectLocale, bool regex,
                                bool toUpperCase, int start, Qt::CaseSensitivity cs) {
    if (!baseName.isEmpty()) {
        ui.lineEdit->setText(baseName);
    }

    ui.spinBox->setValue(start);
    ui.zeroBox->setChecked(zeroPadding);
    ui.localeBox->setChecked(respectLocale);

    // only one mode can be active at a time
    if (replacement || caseChange) {
        ui.serialGroupBox->setChecked(false);
        if (replacement) {
            ui.replaceGroupBox->setChecked(true);
        } else {
            ui.caseGroupBox->setChecked(true);
        }
    }

    ui.findLineEdit->setText(findStr);
    ui.replaceLineEdit->setText(replaceStr);
    ui.caseBox->setChecked(cs == Qt::CaseSensitive);
    ui.regexBox->setChecked(regex);

    if (toUpperCase) {
        ui.upperCaseButton->setChecked(true);
    } else {
        ui.lowerCaseButton->setChecked(true);
    }
}

void BulkRenameDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);

    // when the default base name ends with '#', select everything before it
    if (ui.lineEdit->text().endsWith(QLatin1Char('#'))) {
        QTimer::singleShot(0, this, [this]() { ui.lineEdit->setSelection(0, ui.lineEdit->text().size() - 1); });
    }
}

BulkRenamer::BulkRenamer(const Fm::FileInfoList& files, QWidget* parent) {
    // no point in bulk renaming a single file
    if (files.size() <= 1) {
        return;
    }

    bool replacement = false;
    bool caseChange = false;
    QString baseName;
    QString findStr;
    QString replaceStr;
    int start = 0;
    bool zeroPadding = false;
    bool respectLocale = false;
    bool regex = false;
    bool toUpperCase = true;
    Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    QLocale locale;
    bool showDlg = true;

    while (showDlg) {
        BulkRenameDialog dlg(parent);
        dlg.setState(baseName, findStr, replaceStr, replacement, caseChange, zeroPadding, respectLocale, regex,
                     toUpperCase, start, cs);

        switch (dlg.exec()) {
            case QDialog::Accepted:
                // serial rename
                baseName = dlg.getBaseName();
                start = dlg.getStart();
                zeroPadding = dlg.getZeroPadding();
                respectLocale = dlg.getRespectLocale();
                locale = dlg.locale();

                // replacement
                replacement = dlg.getReplace();
                findStr = dlg.getFindStr();
                replaceStr = dlg.getReplaceStr();
                cs = dlg.getCase();
                regex = dlg.getRegex();

                // case transformation
                caseChange = dlg.getCaseChange();
                toUpperCase = dlg.getUpperCase();
                locale = dlg.locale();
                break;
            default:
                // user cancelled
                return;
        }

        if (replacement) {
            showDlg = !renameByReplacing(files, findStr, replaceStr, cs, regex, parent);
        } else if (caseChange) {
            showDlg = !renameByChangingCase(files, locale, toUpperCase, parent);
        } else {
            showDlg = !rename(files, baseName, locale, start, zeroPadding, respectLocale, parent);
        }
    }
}

BulkRenamer::~BulkRenamer() = default;

static QString effectiveFileName(const std::shared_ptr<Fm::FileInfo>& file) {
    // use edit name to better handle non UTF-8 filenames when possible
    auto editName = QString::fromUtf8(g_file_info_get_edit_name(file->gFileInfo().get()));
    if (!editName.isEmpty()) {
        return editName;
    }
    return QString::fromStdString(file->name());
}

bool BulkRenamer::rename(const Fm::FileInfoList& files, QString& baseName, const QLocale& locale, int start,
                         bool zeroPadding, bool respectLocale, QWidget* parent) {
    // maximum width of the numeric counter used for padding
    int numSpace = zeroPadding ? QString::number(start + files.size()).size() : 0;

    // numeric padding character, optionally using the locale zero digit
    const QChar zero = respectLocale ? (!locale.zeroDigit().isEmpty() ? locale.zeroDigit().at(0) : QLatin1Char('0'))
                                     : QLatin1Char('0');

    // number formatting specifier, with optional locale aware formatting
    const QString specifier = respectLocale ? QStringLiteral("%L1") : QStringLiteral("%1");

    // inject a '#' placeholder if none exists so we always have a counter position
    if (!baseName.contains(QLatin1Char('#'))) {
        int end = baseName.lastIndexOf(QLatin1Char('.'));
        if (end == -1) {
            end = baseName.size();
        }
        baseName.insert(end, QLatin1Char('#'));
    }

    QProgressDialog progress(QObject::tr("Renaming files..."), QObject::tr("Abort"), 0, files.size(), parent);
    progress.setWindowModality(Qt::WindowModal);

    int i = 0;
    int failed = 0;

    const QRegularExpression extension(QStringLiteral("\\.[^.#]+$"));
    const bool noExtension = (baseName.indexOf(extension) == -1);

    for (const auto& file : files) {
        progress.setValue(i);
        if (progress.wasCanceled()) {
            progress.close();
            QMessageBox::warning(parent, QObject::tr("Warning"), QObject::tr("Renaming is aborted."));
            return true;
        }

        QString fileName = effectiveFileName(file);
        QString newName = baseName;

        // keep the original extension if the new pattern has none
        if (noExtension) {
            QRegularExpressionMatch match;
            if (fileName.indexOf(extension, 0, &match) > -1) {
                newName += match.captured();
            }
        }

        newName.replace(QLatin1Char('#'), specifier.arg(start + i, numSpace, 10, zero));

        if (newName == fileName || !Fm::changeFileName(file->path(), newName, nullptr, false)) {
            ++failed;
        }

        ++i;
    }

    progress.setValue(i);

    if (failed == i) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("No file could be renamed."));
        return false;
    } else if (failed > 0) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Some files could not be renamed."));
    }

    return true;
}

bool BulkRenamer::renameByReplacing(const Fm::FileInfoList& files, const QString& findStr, const QString& replaceStr,
                                    Qt::CaseSensitivity cs, bool regex, QWidget* parent) {
    if (findStr.isEmpty()) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Nothing to find."));
        return false;
    }

    QRegularExpression regexFind;
    if (regex) {
        regexFind = QRegularExpression(findStr, cs == Qt::CaseSensitive ? QRegularExpression::NoPatternOption
                                                                        : QRegularExpression::CaseInsensitiveOption);
        if (!regexFind.isValid()) {
            QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Invalid regular expression."));
            return false;
        }
    }

    QProgressDialog progress(QObject::tr("Renaming files..."), QObject::tr("Abort"), 0, files.size(), parent);
    progress.setWindowModality(Qt::WindowModal);

    int i = 0;
    int failed = 0;

    for (const auto& file : files) {
        progress.setValue(i);
        if (progress.wasCanceled()) {
            progress.close();
            QMessageBox::warning(parent, QObject::tr("Warning"), QObject::tr("Renaming is aborted."));
            return true;
        }

        QString fileName = effectiveFileName(file);
        QString newName = fileName;

        if (regex) {
            newName.replace(regexFind, replaceStr);
        } else {
            newName.replace(findStr, replaceStr, cs);
        }

        if (newName.isEmpty() || newName == fileName || !Fm::changeFileName(file->path(), newName, nullptr, false)) {
            ++failed;
        }

        ++i;
    }

    progress.setValue(i);

    if (failed == i) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("No file could be renamed."));
        return false;
    } else if (failed > 0) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Some files could not be renamed."));
    }

    return true;
}

bool BulkRenamer::renameByChangingCase(const Fm::FileInfoList& files, const QLocale& locale, bool toUpperCase,
                                       QWidget* parent) {
    QProgressDialog progress(QObject::tr("Renaming files..."), QObject::tr("Abort"), 0, files.size(), parent);
    progress.setWindowModality(Qt::WindowModal);

    int i = 0;
    int failed = 0;

    for (const auto& file : files) {
        progress.setValue(i);
        if (progress.wasCanceled()) {
            progress.close();
            QMessageBox::warning(parent, QObject::tr("Warning"), QObject::tr("Renaming is aborted."));
            return true;
        }

        QString fileName = effectiveFileName(file);
        QString newName = toUpperCase ? locale.toUpper(fileName) : locale.toLower(fileName);

        if (newName.isEmpty() || newName == fileName || !Fm::changeFileName(file->path(), newName, nullptr, false)) {
            ++failed;
        }

        ++i;
    }

    progress.setValue(i);

    if (failed == i) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("No file could be renamed."));
        return false;
    } else if (failed > 0) {
        QMessageBox::critical(parent, QObject::tr("Error"), QObject::tr("Some files could not be renamed."));
    }

    return true;
}

}  // namespace PCManFM
