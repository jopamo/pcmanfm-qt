/*
 * File launcher header
 * pcmanfm/launcher.h
 */

#ifndef PCMANFM_LAUNCHER_H
#define PCMANFM_LAUNCHER_H

#include <libfm-qt6/filelauncher.h>

namespace PCManFM {

class MainWindow;

class Launcher : public Fm::FileLauncher {
   public:
    Launcher(MainWindow* mainWindow = nullptr);
    ~Launcher();

    bool hasMainWindow() const { return mainWindow_ != nullptr; }

    void openInNewTab() { openInNewTab_ = true; }

    bool openWithDefaultFileManager() const { return openWithDefaultFileManager_; }
    void setOpenWithDefaultFileManager(bool open) { openWithDefaultFileManager_ = open; }

   protected:
    bool openFolder(GAppLaunchContext* ctx, const Fm::FileInfoList& folderInfos, Fm::GErrorPtr& err) override;
    void launchedFiles(const Fm::FileInfoList& files) const override;
    void launchedPaths(const Fm::FilePathList& paths) const override;

   private:
    MainWindow* mainWindow_;
    bool openInNewTab_;
    bool openWithDefaultFileManager_;
};

}  // namespace PCManFM

#endif  // PCMANFM_LAUNCHER_H
