# PCManFM-Qt

## Overview

PCManFM-Qt is a Qt-based file manager which uses `GLib` for file management.
It was started as the Qt port of PCManFM, the file manager of [LXDE](https://lxde.org).

This version is a standalone file manager without desktop functionality or Wayland support.

PCManFM-Qt is licensed under the terms of the
[GPLv2](https://www.gnu.org/licenses/gpl-2.0.en.html) or any later version. See
file LICENSE for its full text.  

## Installation

### Compiling source code

The build dependencies are CMake, [lxqt-build-tools](https://github.com/lxqt/lxqt-build-tools),
and [libfm-qt](https://github.com/lxqt/libfm-qt).

GVFS is an optional dependency. It provides important functionalities like Trash support.

**Build Instructions:**

1. Create a build directory:
   ```bash
   mkdir build
   cd build
   ```

2. Configure with CMake:
   ```bash
   cmake ..
   ```

3. Build the project:
   ```bash
   make
   ```

4. Install (optional):
   ```bash
   sudo make install
   ```

The CMake variable `CMAKE_INSTALL_PREFIX` can be set to `/usr` on most operating systems.

### Binary packages

Official binary packages are available in Arch Linux, Debian,
Fedora and openSUSE (Leap and Tumbleweed) and most other distributions.

## Usage

The file manager functionality should be self-explanatory. For advanced functionalities,
see the [wiki](https://github.com/lxqt/pcmanfm-qt/wiki).

All command-line options are explained in detail in `man 1 pcmanfm-qt`.  

## Development

Issues should go to the tracker of PCManFM-Qt at https://github.com/lxqt/pcmanfm-qt/issues.


### Translation

Translations can be done in [LXQt-Weblate](https://translate.lxqt-project.org/projects/lxqt-desktop/pcmanfm-qt/)

<a href="https://translate.lxqt-project.org/projects/lxqt-desktop/pcmanfm-qt/">
<img src="https://translate.lxqt-project.org/widgets/lxqt-desktop/-/pcmanfm-qt/multi-auto.svg" alt="Translation status" />
</a>

