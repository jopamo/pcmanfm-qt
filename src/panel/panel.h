/*
 * Panel namespace aliases for in-tree libfm-qt fork
 * src/panel/panel.h
 */
#ifndef PANEL_PANEL_H
#define PANEL_PANEL_H

#include <libfm-qt6/libfmqt.h>
#include <libfm-qt6/browsehistory.h>
#include <libfm-qt6/cachedfoldermodel.h>
#include <libfm-qt6/core/archiver.h>
#include <libfm-qt6/core/bookmarks.h>
#include <libfm-qt6/core/fileinfo.h>
#include <libfm-qt6/core/fileinfojob.h>
#include <libfm-qt6/core/filepath.h>
#include <libfm-qt6/core/folder.h>
#include <libfm-qt6/core/folderconfig.h>
#include <libfm-qt6/core/iconinfo.h>
#include <libfm-qt6/core/mimetype.h>
#include <libfm-qt6/core/job.h>
#include <libfm-qt6/core/thumbnailjob.h>
#include <libfm-qt6/core/terminal.h>
#include <libfm-qt6/core/gobjectptr.h>
#include <libfm-qt6/core/legacy/fm-config.h>
#include <libfm-qt6/editbookmarksdialog.h>
#include <libfm-qt6/filelauncher.h>
#include <libfm-qt6/filemenu.h>
#include <libfm-qt6/fileoperation.h>
#include <libfm-qt6/filepropsdialog.h>
#include <libfm-qt6/filesearchdialog.h>
#include <libfm-qt6/foldermenu.h>
#include <libfm-qt6/foldermodel.h>
#include <libfm-qt6/folderview.h>
#include <libfm-qt6/pathbar.h>
#include <libfm-qt6/pathedit.h>
#include <libfm-qt6/proxyfoldermodel.h>
#include <libfm-qt6/sidepane.h>
#include <libfm-qt6/utilities.h>
#include <libfm-qt6/createnewmenu.h>

namespace Panel {
using Lib = Fm::LibFmQt;
using LibFmQt = Fm::LibFmQt;
using GErrorPtr = Fm::GErrorPtr;
using GAppInfoPtr = Fm::GAppInfoPtr;
using FilePathHash = Fm::FilePathHash;
using FilePath = Fm::FilePath;
using FileInfo = Fm::FileInfo;
using FileInfoList = Fm::FileInfoList;
using FilePathList = Fm::FilePathList;
using FileInfoJob = Fm::FileInfoJob;
using FilePropsDialog = Fm::FilePropsDialog;
using FileSearchDialog = Fm::FileSearchDialog;
using EditBookmarksDialog = Fm::EditBookmarksDialog;
using Bookmarks = Fm::Bookmarks;
using BookmarkItem = Fm::BookmarkItem;
using BrowseHistory = Fm::BrowseHistory;
using BrowseHistoryItem = Fm::BrowseHistoryItem;
using Folder = Fm::Folder;
using FolderModel = Fm::FolderModel;
using ProxyFolderModel = Fm::ProxyFolderModel;
using ProxyFolderModelFilter = Fm::ProxyFolderModelFilter;
using CachedFolderModel = Fm::CachedFolderModel;
using FolderView = Fm::FolderView;
using FolderMenu = Fm::FolderMenu;
using FileMenu = Fm::FileMenu;
using FileLauncher = Fm::FileLauncher;
using FileOperation = Fm::FileOperation;
using FolderConfig = Fm::FolderConfig;
using IconInfo = Fm::IconInfo;
using MimeType = Fm::MimeType;
using ThumbnailJob = Fm::ThumbnailJob;
using Archiver = Fm::Archiver;
using PathBar = Fm::PathBar;
using PathEdit = Fm::PathEdit;
using SidePane = Fm::SidePane;
using CreateNewMenu = Fm::CreateNewMenu;
using Job = Fm::Job;

using Fm::allKnownTerminals;
using Fm::changeFileName;
using Fm::formatFileSize;
using Fm::internalTerminals;
using Fm::launchTerminal;
using Fm::setDefaultTerminal;
}  // namespace Panel

#endif  // PANEL_PANEL_H
