# PCManFM-Qt

## Overview

PCManFM-Qt is a Qt-based file manager. It was started as the Qt port of PCManFM, the file manager of
[LXDE](https://lxde.org).

This fork vendors the libfm-qt code we still rely on (exposed under the `Panel::*` namespace) and is
modernizing toward a Qt/POSIX backend stack. It is Linux-only, Qt 6-only, and ships without desktop
functionality or Wayland shell integration.

PCManFM-Qt is licensed under the terms of the
[GPLv2](https://www.gnu.org/licenses/gpl-2.0.en.html) or any later version. See
file LICENSE for its full text.  

## Installation

### Compiling source code

Build dependencies:

- CMake 3.18+
- Qt 6.6+ (Widgets, DBus, LinguistTools)
- [lxqt-build-tools](https://github.com/lxqt/lxqt-build-tools) 2.x
- GLib/GIO/MenuCache (and related) development packages needed by the vendored libfm-qt code

You do **not** need an external libfm-qt package; the bundled copy in `libfm-qt/` is built in-tree
and linked directly.

**Build Instructions:**

1. Configure out-of-source:
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
   ```

2. Build:
   ```bash
   cmake --build build -j"$(nproc)"
   ```

3. (Optional) run tests:
   ```bash
   (cd build && ctest --output-on-failure)
   ```

4. (Optional) install:
   ```bash
   cmake --install build
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
