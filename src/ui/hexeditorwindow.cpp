/*
 * Hex editor window
 * src/ui/hexeditorwindow.cpp
 */

#include "hexeditorwindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QStringList>
#include <QStatusBar>
#include <QToolBar>
#include <QTimer>
#include <QLabel>
#include <QDockWidget>
#include <QFormLayout>

#include <algorithm>
#include <cctype>
#include <vector>
#include <cstring>

namespace PCManFM {

namespace {

QByteArray parseHexString(const QString& text, bool& okOut) {
    QByteArray out;
    QString cleaned = text;
    cleaned.remove(QLatin1Char(' '));
    cleaned.remove(QLatin1Char('\t'));
    cleaned.remove(QLatin1Char('\n'));
    if (cleaned.isEmpty() || (cleaned.size() % 2) != 0) {
        okOut = false;
        return {};
    }
    out.resize(cleaned.size() / 2);
    bool ok = true;
    for (int i = 0; i < cleaned.size(); i += 2) {
        bool byteOk = false;
        const int value = cleaned.mid(i, 2).toInt(&byteOk, 16);
        if (!byteOk) {
            ok = false;
            break;
        }
        out[i / 2] = static_cast<char>(value);
    }
    okOut = ok;
    if (!ok) {
        out.clear();
    }
    return out;
}

}  // namespace

HexEditorWindow::HexEditorWindow(QWidget* parent) : QMainWindow(parent), doc_(std::make_unique<HexDocument>()) {
    setupUi();
    updateWindowTitle();
}

HexEditorWindow::~HexEditorWindow() = default;

void HexEditorWindow::setupUi() {
    view_ = new HexEditorView(doc_.get(), this);
    setCentralWidget(view_);

    auto* toolbar = addToolBar(tr("Hex Editor"));
    toolbar->setMovable(false);

    saveAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("document-save")), tr("Save"));
    saveAction_->setShortcut(QKeySequence::Save);
    connect(saveAction_, &QAction::triggered, this, [this] { doSave(false); });

    saveAsAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("document-save-as")), tr("Save As…"));
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction_, &QAction::triggered, this, [this] { doSave(true); });

    toolbar->addSeparator();

    undoAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-undo")), tr("Undo"));
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, [this] {
        QString err;
        if (!doc_->undo(err) && !err.isEmpty()) {
            QMessageBox::warning(this, tr("Undo failed"), err);
        }
    });

    redoAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-redo")), tr("Redo"));
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, [this] {
        QString err;
        if (!doc_->redo(err) && !err.isEmpty()) {
            QMessageBox::warning(this, tr("Redo failed"), err);
        }
    });

    toolbar->addSeparator();

    copyAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Copy"));
    copyAction_->setShortcut(QKeySequence::Copy);
    connect(copyAction_, &QAction::triggered, view_, &HexEditorView::copySelectionToClipboard);

    copyHexAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("code-context")), tr("Copy as Hex"));
    connect(copyHexAction_, &QAction::triggered, view_, &HexEditorView::copySelectionAsHexToClipboard);

    pasteAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-paste")), tr("Paste"));
    pasteAction_->setShortcut(QKeySequence::Paste);
    connect(pasteAction_, &QAction::triggered, this, [this] {
        QString err;
        if (!view_->pasteFromClipboard(err) && !err.isEmpty()) {
            QMessageBox::warning(this, tr("Paste failed"), err);
        }
    });

    deleteAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), tr("Delete"));
    deleteAction_->setShortcut(QKeySequence::Delete);
    connect(deleteAction_, &QAction::triggered, this, [this] {
        QString err;
        if (!view_->deleteSelection(err) && !err.isEmpty()) {
            QMessageBox::warning(this, tr("Delete failed"), err);
        }
    });

    toolbar->addSeparator();

    findAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-find")), tr("Find…"));
    findAction_->setShortcut(QKeySequence::Find);
    connect(findAction_, &QAction::triggered, this, [this] { performFind(true); });

    findNextAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-down")), tr("Find Next"));
    findNextAction_->setShortcut(QKeySequence::FindNext);
    connect(findNextAction_, &QAction::triggered, this, [this] { performFind(true); });

    findPrevAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-up")), tr("Find Previous"));
    findPrevAction_->setShortcut(QKeySequence::FindPrevious);
    connect(findPrevAction_, &QAction::triggered, this, [this] { performFind(false); });

    gotoAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-jump")), tr("Go to Offset…"));
    gotoAction_->setShortcut(QKeySequence(tr("Ctrl+G")));
    connect(gotoAction_, &QAction::triggered, this, &HexEditorWindow::goToOffset);

    toolbar->addSeparator();

    replaceAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-find-replace")), tr("Replace…"));
    replaceAction_->setShortcut(QKeySequence::Replace);
    connect(replaceAction_, &QAction::triggered, this, [this] { performReplace(false); });

    replaceAllAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-find-replace")), tr("Replace All"));
    connect(replaceAllAction_, &QAction::triggered, this, [this] { performReplace(true); });

    findAllAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("edit-find")), tr("Find All"));
    connect(findAllAction_, &QAction::triggered, this, [this] {
        performFind(true);
        std::vector<std::uint64_t> offsets;
        QString err;
        if (!doc_->findAll(lastSearch_, offsets, err)) {
            if (!err.isEmpty()) {
                QMessageBox::warning(this, tr("Find All"), err);
            }
            return;
        }
        QMessageBox::information(this, tr("Find All"),
                                 tr("Found %1 occurrence(s).").arg(static_cast<int>(offsets.size())));
    });

    toolbar->addSeparator();

    nextModifiedAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-next")), tr("Next Modified"));
    connect(nextModifiedAction_, &QAction::triggered, this, [this] { jumpToModified(true); });
    prevModifiedAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("go-previous")), tr("Previous Modified"));
    connect(prevModifiedAction_, &QAction::triggered, this, [this] { jumpToModified(false); });

    insertToggleAction_ = toolbar->addAction(QIcon::fromTheme(QStringLiteral("insert-text")), tr("Insert mode"));
    insertToggleAction_->setCheckable(true);
    insertToggleAction_->setChecked(false);
    connect(insertToggleAction_, &QAction::toggled, this, [this](bool checked) { view_->setInsertMode(checked); });

    connect(view_, &HexEditorView::cursorChanged, this, &HexEditorWindow::updateStatus);
    connect(view_, &HexEditorView::modeChanged, this, [this](bool checked) {
        insertToggleAction_->setChecked(checked);
        updateWindowTitle();
    });
    connect(view_, &HexEditorView::statusMessage, this, [this](const QString& msg) { statusBar()->showMessage(msg); });

    connect(doc_.get(), &HexDocument::changed, this, [this]() {
        updateWindowTitle();
        updateActionStates(view_ && view_->selection().has_value());
    });
    connect(doc_.get(), &HexDocument::saved, this, &HexEditorWindow::updateWindowTitle);

    changeTimer_ = new QTimer(this);
    changeTimer_->setInterval(2000);
    connect(changeTimer_, &QTimer::timeout, this, &HexEditorWindow::checkExternalChanges);
    changeTimer_->start();

    modeLabel_ = new QLabel(this);
    modifiedLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(modeLabel_);
    statusBar()->addPermanentWidget(modifiedLabel_);

    inspectorDock_ = new QDockWidget(tr("Inspect"), this);
    inspectorDock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* inspector = new QWidget(inspectorDock_);
    auto* form = new QFormLayout(inspector);
    inspectorOffset_ = new QLabel(tr("0x0 / 0"), inspector);
    inspectorU8_ = new QLabel(QString(), inspector);
    inspectorI8_ = new QLabel(QString(), inspector);
    inspectorU16LE_ = new QLabel(QString(), inspector);
    inspectorU16BE_ = new QLabel(QString(), inspector);
    inspectorU32LE_ = new QLabel(QString(), inspector);
    inspectorU32BE_ = new QLabel(QString(), inspector);
    inspectorU64LE_ = new QLabel(QString(), inspector);
    inspectorU64BE_ = new QLabel(QString(), inspector);
    inspectorFloat_ = new QLabel(QString(), inspector);
    inspectorDouble_ = new QLabel(QString(), inspector);
    inspectorUtf8_ = new QLabel(QString(), inspector);

    form->addRow(tr("Offset"), inspectorOffset_);
    form->addRow(tr("U8"), inspectorU8_);
    form->addRow(tr("I8"), inspectorI8_);
    form->addRow(tr("U16 LE"), inspectorU16LE_);
    form->addRow(tr("U16 BE"), inspectorU16BE_);
    form->addRow(tr("U32 LE"), inspectorU32LE_);
    form->addRow(tr("U32 BE"), inspectorU32BE_);
    form->addRow(tr("U64 LE"), inspectorU64LE_);
    form->addRow(tr("U64 BE"), inspectorU64BE_);
    form->addRow(tr("Float"), inspectorFloat_);
    form->addRow(tr("Double"), inspectorDouble_);
    form->addRow(tr("UTF-8"), inspectorUtf8_);

    inspector->setLayout(form);
    inspectorDock_->setWidget(inspector);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock_);

    updateActionStates(false);
    statusBar()->showMessage(tr("Ready"));
}

bool HexEditorWindow::openFile(const QString& path, QString& errorOut) {
    lastSearch_.clear();
    const bool ok = doc_->openFile(path, errorOut);
    if (ok) {
        view_->setCursorOffset(0);
        view_->clearSelection();
        updateWindowTitle();
        updateActionStates(false);
        if (!doc_->isRegularFile()) {
            QMessageBox::warning(this, tr("Hex editor"),
                                 tr("The selected path is not a regular file; editing may not be safe."));
        }
        updateInspector(0);
    }
    return ok;
}

void HexEditorWindow::updateWindowTitle() {
    QString title = tr("Hex Editor");
    if (doc_ && !doc_->path().isEmpty()) {
        QFileInfo info(doc_->path());
        title = info.fileName().isEmpty() ? doc_->path() : info.fileName();
    }
    if (doc_ && doc_->modified()) {
        title = QStringLiteral("* %1").arg(title);
    }
    setWindowTitle(title);
}

void HexEditorWindow::updateStatus(std::uint64_t offset, int byteValue, bool hasSelection) {
    QStringList parts;
    parts << tr("Offset 0x%1 (%2)").arg(offset, 0, 16).arg(offset);
    if (byteValue >= 0) {
        parts << tr("Byte 0x%1 (%2)").arg(byteValue, 2, 16, QLatin1Char('0')).arg(byteValue);
    }
    if (hasSelection && view_) {
        const auto sel = view_->selection();
        if (sel) {
            parts << tr("%1 bytes selected").arg(sel->second);
        }
    }
    const bool insert = view_ && view_->insertMode();
    parts << (insert ? tr("INS") : tr("OVR"));
    statusBar()->showMessage(parts.join(QStringLiteral(" | ")));
    if (modeLabel_) {
        modeLabel_->setText(insert ? tr("Insert") : tr("Overwrite"));
    }
    const bool modified = doc_ && doc_->isModified(offset);
    if (modifiedLabel_) {
        modifiedLabel_->setText(modified ? tr("Modified") : QString());
    }
    updateInspector(offset);
    updateActionStates(hasSelection);
}

void HexEditorWindow::updateActionStates(bool hasSelection) {
    const bool hasDoc = doc_ != nullptr;
    const bool modified = hasDoc && doc_->modified();
    if (saveAction_) {
        saveAction_->setEnabled(modified);
    }
    if (undoAction_) {
        undoAction_->setEnabled(hasDoc);
    }
    if (redoAction_) {
        redoAction_->setEnabled(hasDoc);
    }
    if (copyAction_) {
        copyAction_->setEnabled(hasSelection);
    }
    if (copyHexAction_) {
        copyHexAction_->setEnabled(hasSelection);
    }
    if (deleteAction_) {
        deleteAction_->setEnabled(hasSelection);
    }
    if (findAction_) {
        findAction_->setEnabled(hasDoc);
    }
    if (findAllAction_) {
        findAllAction_->setEnabled(hasDoc);
    }
    if (replaceAction_) {
        replaceAction_->setEnabled(hasDoc);
    }
    if (replaceAllAction_) {
        replaceAllAction_->setEnabled(hasDoc);
    }
    if (nextModifiedAction_) {
        nextModifiedAction_->setEnabled(hasDoc);
    }
    if (prevModifiedAction_) {
        prevModifiedAction_->setEnabled(hasDoc);
    }
}

bool HexEditorWindow::promptToSave() {
    if (!doc_ || !doc_->modified()) {
        return true;
    }
    const auto res = QMessageBox::question(
        this, tr("Unsaved changes"), tr("The file has unsaved changes. Do you want to save them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (res == QMessageBox::Cancel) {
        return false;
    }
    if (res == QMessageBox::Discard) {
        return true;
    }

    doSave(false);
    if (doc_->modified()) {
        return false;
    }
    return true;
}

void HexEditorWindow::doSave(bool saveAs) {
    if (!doc_) {
        return;
    }
    QString err;
    QString targetPath = doc_->path();
    if (saveAs) {
        const QString chosen = QFileDialog::getSaveFileName(this, tr("Save File"), targetPath);
        if (chosen.isEmpty()) {
            return;
        }
        targetPath = chosen;
        QFileInfo info(targetPath);
        if (!info.isFile() && info.exists()) {
            QMessageBox::warning(this, tr("Save failed"), tr("Target is not a regular file."));
            return;
        }
        if (!doc_->saveAs(targetPath, err)) {
            QMessageBox::warning(this, tr("Save failed"), err);
            return;
        }
    }
    else {
        if (!doc_->save(err)) {
            QMessageBox::warning(this, tr("Save failed"), err);
            return;
        }
    }
    updateWindowTitle();
    updateActionStates(view_ && view_->selection().has_value());
}

void HexEditorWindow::checkExternalChanges() {
    if (!doc_ || doc_->path().isEmpty()) {
        return;
    }

    bool changed = false;
    QString err;
    if (!doc_->hasExternalChange(changed, err)) {
        if (!err.isEmpty()) {
            statusBar()->showMessage(err);
        }
        return;
    }
    quint64 fingerprint = 0;
    if (!doc_->currentFingerprint(fingerprint, err)) {
        if (!err.isEmpty()) {
            statusBar()->showMessage(err);
        }
        return;
    }

    if (!changed) {
        suppressExternalPrompt_ = false;
        lastExternalFingerprint_ = fingerprint;
        return;
    }
    if (suppressExternalPrompt_ && fingerprint == lastExternalFingerprint_) {
        return;
    }
    lastExternalFingerprint_ = fingerprint;

    const QMessageBox::StandardButton res = QMessageBox::warning(
        this, tr("File changed on disk"),
        tr("The file has changed on disk. Reloading will discard your unsaved edits. What do you want to do?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Save, QMessageBox::Yes);

    if (res == QMessageBox::No) {
        suppressExternalPrompt_ = true;
        return;
    }
    if (res == QMessageBox::Save) {
        doSave(false);
        suppressExternalPrompt_ = false;
        return;
    }

    if (res == QMessageBox::Yes) {
        if (!doc_->modified()) {
            if (!doc_->reload(err)) {
                QMessageBox::warning(this, tr("Reload failed"), err);
                return;
            }
            view_->setCursorOffset(0);
            view_->clearSelection();
            updateWindowTitle();
            updateInspector(0);
            return;
        }

        const auto confirm = QMessageBox::question(this, tr("Reload file"),
                                                   tr("Discard your unsaved changes and reload the file from disk?"),
                                                   QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (confirm == QMessageBox::Yes) {
            if (!doc_->reload(err)) {
                QMessageBox::warning(this, tr("Reload failed"), err);
                return;
            }
            view_->setCursorOffset(0);
            view_->clearSelection();
            updateWindowTitle();
            updateInspector(0);
        }
    }
}

QByteArray HexEditorWindow::parseSearchInput(const QString& input, bool& okOut) const {
    okOut = false;
    if (input.trimmed().isEmpty()) {
        return {};
    }
    bool okHex = false;
    const QByteArray hex = parseHexString(input.trimmed(), okHex);
    if (okHex) {
        okOut = true;
        return hex;
    }
    okOut = true;
    return input.toUtf8();
}

QByteArray HexEditorWindow::promptForPattern(const QString& title, bool& okOut) {
    okOut = false;
    QStringList history = statusBar()->property("hexSearchHistory").toStringList();
    const QString initial = history.isEmpty() ? QString() : history.back();
    const QString input = QInputDialog::getText(this, title, tr("Enter hex (e.g. 0A FF) or text to search for:"),
                                                QLineEdit::Normal, initial, &okOut);
    if (!okOut || input.isEmpty()) {
        return {};
    }
    bool parseOk = false;
    QByteArray pattern = parseSearchInput(input, parseOk);
    if (parseOk && !pattern.isEmpty()) {
        if (!history.contains(input)) {
            history.append(input);
            if (history.size() > 10) {
                history.pop_front();
            }
            statusBar()->setProperty("hexSearchHistory", history);
        }
    }
    okOut = parseOk;
    return pattern;
}

QByteArray HexEditorWindow::promptForReplace(bool& okOut) {
    QStringList history = statusBar()->property("hexReplaceHistory").toStringList();
    const QString initial = history.isEmpty() ? QString() : history.back();
    const QString input = QInputDialog::getText(this, tr("Replace"), tr("Replace with (hex or text):"),
                                                QLineEdit::Normal, initial, &okOut);
    if (!okOut) {
        return {};
    }
    bool parseOk = false;
    QByteArray pattern = parseSearchInput(input, parseOk);
    if (parseOk) {
        if (!history.contains(input)) {
            history.append(input);
            if (history.size() > 10) {
                history.pop_front();
            }
            statusBar()->setProperty("hexReplaceHistory", history);
        }
    }
    okOut = parseOk;
    return pattern;
}

void HexEditorWindow::performFind(bool forward) {
    if (!doc_ || !view_) {
        return;
    }
    if (sender() == findAction_) {
        lastSearch_.clear();
    }
    if (lastSearch_.isEmpty()) {
        bool ok = false;
        lastSearch_ = promptForPattern(tr("Find"), ok);
        if (!ok || lastSearch_.isEmpty()) {
            return;
        }
    }

    const auto sel = view_->selection();
    std::uint64_t start = view_->cursorOffset();
    if (sel) {
        start = forward ? sel->first + sel->second : (sel->first == 0 ? 0 : sel->first - 1);
    }

    std::uint64_t found = 0;
    QString error;
    const bool ok = forward ? doc_->findForward(lastSearch_, start, found, error)
                            : doc_->findBackward(lastSearch_, start, found, error);
    if (ok) {
        view_->setSelection(found, static_cast<std::uint64_t>(lastSearch_.size()));
        statusBar()->showMessage(tr("Found at offset 0x%1").arg(found, 0, 16));
        return;
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Find"), error);
    }
    else {
        QMessageBox::information(this, tr("Find"), tr("Pattern not found."));
    }
}

void HexEditorWindow::performReplace(bool replaceAll) {
    if (!doc_ || !view_) {
        return;
    }
    const bool freshInput = lastSearch_.isEmpty() || sender() == replaceAction_;
    if (freshInput) {
        bool ok = false;
        lastSearch_ = promptForPattern(tr("Replace"), ok);
        if (!ok || lastSearch_.isEmpty()) {
            return;
        }
        lastReplace_ = promptForReplace(ok);
        if (!ok) {
            lastSearch_.clear();
            return;
        }
    }

    if (lastSearch_.isEmpty()) {
        return;
    }

    QString error;
    if (replaceAll) {
        std::vector<std::uint64_t> offsets;
        if (!doc_->findAll(lastSearch_, offsets, error)) {
            if (!error.isEmpty()) {
                QMessageBox::warning(this, tr("Replace All"), error);
            }
            return;
        }
        std::size_t count = 0;
        for (auto it = offsets.rbegin(); it != offsets.rend(); ++it) {
            if (!doc_->erase(*it, static_cast<std::uint64_t>(lastSearch_.size()), error)) {
                break;
            }
            if (!lastReplace_.isEmpty()) {
                if (!doc_->insert(*it, lastReplace_, error)) {
                    break;
                }
            }
            count++;
        }
        statusBar()->showMessage(tr("Replaced %1 occurrence(s).").arg(static_cast<int>(count)));
        return;
    }

    auto sel = view_->selection();
    std::uint64_t start = view_->cursorOffset();
    if (sel) {
        start = sel->first + sel->second;
    }
    std::uint64_t found = 0;
    if (!doc_->findForward(lastSearch_, start, found, error)) {
        if (!error.isEmpty()) {
            QMessageBox::warning(this, tr("Replace"), error);
        }
        else {
            QMessageBox::information(this, tr("Replace"), tr("Pattern not found."));
        }
        return;
    }
    if (!doc_->erase(found, static_cast<std::uint64_t>(lastSearch_.size()), error)) {
        QMessageBox::warning(this, tr("Replace"), error);
        return;
    }
    if (!lastReplace_.isEmpty()) {
        if (!doc_->insert(found, lastReplace_, error)) {
            QMessageBox::warning(this, tr("Replace"), error);
            return;
        }
    }
    view_->setSelection(found, static_cast<std::uint64_t>(lastReplace_.size()));
    view_->setCursorOffset(found + static_cast<std::uint64_t>(lastReplace_.size()));
    statusBar()->showMessage(tr("Replaced at offset 0x%1").arg(found, 0, 16));
}

void HexEditorWindow::jumpToModified(bool forward) {
    if (!doc_ || !view_) {
        return;
    }
    std::uint64_t start = view_->cursorOffset();
    std::uint64_t found = 0;
    if (!doc_->nextModifiedOffset(start, forward, found)) {
        statusBar()->showMessage(tr("No further modified bytes."));
        return;
    }
    view_->setSelection(found, 1);
    view_->setCursorOffset(found, true);
    statusBar()->showMessage(tr("Jumped to modified byte at 0x%1").arg(found, 0, 16));
}

void HexEditorWindow::updateInspector(std::uint64_t offset) {
    if (!doc_ || !inspectorDock_) {
        return;
    }

    auto setText = [](QLabel* label, const QString& text) {
        if (label) {
            label->setText(text);
        }
    };

    QString error;
    QByteArray data;
    if (!doc_->readBytes(offset, 16, data, error)) {
        setText(inspectorOffset_, tr("N/A"));
        setText(inspectorU8_, tr("N/A"));
        setText(inspectorI8_, tr("N/A"));
        setText(inspectorU16LE_, tr("N/A"));
        setText(inspectorU16BE_, tr("N/A"));
        setText(inspectorU32LE_, tr("N/A"));
        setText(inspectorU32BE_, tr("N/A"));
        setText(inspectorU64LE_, tr("N/A"));
        setText(inspectorU64BE_, tr("N/A"));
        setText(inspectorFloat_, tr("N/A"));
        setText(inspectorDouble_, tr("N/A"));
        setText(inspectorUtf8_, tr("N/A"));
        return;
    }

    setText(inspectorOffset_, tr("0x%1 / %2").arg(offset, 0, 16).arg(offset));

    auto readLE = [&](int bytes, quint64& out) -> bool {
        if (data.size() < bytes) {
            return false;
        }
        out = 0;
        for (int i = bytes - 1; i >= 0; --i) {
            out = (out << 8) | static_cast<unsigned char>(data.at(i));
        }
        return true;
    };
    auto readBE = [&](int bytes, quint64& out) -> bool {
        if (data.size() < bytes) {
            return false;
        }
        out = 0;
        for (int i = 0; i < bytes; ++i) {
            out = (out << 8) | static_cast<unsigned char>(data.at(i));
        }
        return true;
    };

    if (!data.isEmpty()) {
        const quint8 u8 = static_cast<quint8>(data.at(0));
        const qint8 i8 = static_cast<qint8>(data.at(0));
        setText(inspectorU8_, tr("%1 (0x%2)").arg(u8).arg(u8, 0, 16));
        setText(inspectorI8_, QString::number(i8));
    }
    else {
        setText(inspectorU8_, tr("N/A"));
        setText(inspectorI8_, tr("N/A"));
    }

    quint64 v = 0;
    setText(inspectorU16LE_, readLE(2, v) ? tr("%1 (0x%2)").arg(v).arg(v, 0, 16) : tr("N/A"));
    setText(inspectorU16BE_, readBE(2, v) ? tr("%1 (0x%2)").arg(v).arg(v, 0, 16) : tr("N/A"));
    setText(inspectorU32LE_, readLE(4, v) ? tr("%1 (0x%2)").arg(v).arg(v, 0, 16) : tr("N/A"));
    setText(inspectorU32BE_, readBE(4, v) ? tr("%1 (0x%2)").arg(v).arg(v, 0, 16) : tr("N/A"));
    setText(inspectorU64LE_, readLE(8, v) ? tr("%1 (0x%2)").arg(v).arg(v, 0, 16) : tr("N/A"));
    setText(inspectorU64BE_, readBE(8, v) ? tr("%1 (0x%2)").arg(v).arg(v, 0, 16) : tr("N/A"));

    if (data.size() >= static_cast<int>(sizeof(float))) {
        float f = 0.0f;
        std::memcpy(&f, data.constData(), sizeof(float));
        setText(inspectorFloat_, QString::number(f));
    }
    else {
        setText(inspectorFloat_, tr("N/A"));
    }

    if (data.size() >= static_cast<int>(sizeof(double))) {
        double d = 0.0;
        std::memcpy(&d, data.constData(), sizeof(double));
        setText(inspectorDouble_, QString::number(d));
    }
    else {
        setText(inspectorDouble_, tr("N/A"));
    }

    QString utf8 = QString::fromUtf8(data);
    if (utf8.size() > 32) {
        utf8 = utf8.left(32) + QStringLiteral("…");
    }
    setText(inspectorUtf8_, utf8);
}

void HexEditorWindow::goToOffset() {
    if (!doc_) {
        return;
    }
    bool ok = false;
    const QString input = QInputDialog::getText(
        this, tr("Go to offset"), tr("Enter offset (decimal or hex, e.g. 0x10):"), QLineEdit::Normal, QString(), &ok);
    if (!ok || input.isEmpty()) {
        return;
    }
    QString trimmed = input.trimmed();
    int base = 10;
    if (trimmed.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        base = 16;
        trimmed = trimmed.mid(2);
    }
    bool parseOk = false;
    const std::uint64_t offset = trimmed.toULongLong(&parseOk, base);
    if (!parseOk) {
        QMessageBox::warning(this, tr("Go to offset"), tr("Invalid number."));
        return;
    }
    const std::uint64_t target = std::min<std::uint64_t>(offset, doc_->size());
    view_->clearSelection();
    view_->setCursorOffset(target);
}

void HexEditorWindow::closeEvent(QCloseEvent* event) {
    if (!promptToSave()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

}  // namespace PCManFM
