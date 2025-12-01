#include "archive_extract.h"

#include "fs_ops.h"

#include <archive.h>
#include <archive_entry.h>

#include <cerrno>
#include <cstring>
#include <limits>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

namespace PCManFM::ArchiveExtract {
namespace {

using PCManFM::FsOps::Error;
using PCManFM::FsOps::ProgressCallback;
using PCManFM::FsOps::ProgressInfo;

struct Fd {
    int fd;
    explicit Fd(int f = -1) : fd(f) {}
    ~Fd() {
        if (fd >= 0) {
            ::close(fd);
        }
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : fd(other.fd) { other.fd = -1; }
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            if (fd >= 0) {
                ::close(fd);
            }
            fd = other.fd;
            other.fd = -1;
        }
        return *this;
    }
    bool valid() const { return fd >= 0; }
};

inline void set_error(Error& err, const std::string& context) {
    err.code = errno;
    err.message = context + ": " + std::strerror(errno);
}

inline void set_archive_error(Error& err, struct archive* ar, const char* context) {
    err.code = EIO;
    err.message = std::string(context) + ": " + archive_error_string(ar);
}

bool should_continue(const ProgressCallback& cb, const ProgressInfo& info) {
    if (!cb) {
        return true;
    }
    return cb(info);
}

std::string sanitize_path(const char* raw) {
    if (!raw || raw[0] == '\0') {
        return {};
    }
    std::string_view v(raw);
    if (!v.empty() && v.front() == '/') {
        return {};
    }

    std::string result;
    std::vector<std::string> components;
    std::string current;

    for (char c : v) {
        if (c == '/') {
            if (!current.empty()) {
                if (current == ".") {
                    // skip
                }
                else if (current == "..") {
                    if (components.empty()) {
                        return {};
                    }
                    components.pop_back();
                }
                else {
                    components.push_back(std::move(current));
                }
                current.clear();
            }
        }
        else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        if (current == ".") {
            // skip
        }
        else if (current == "..") {
            if (components.empty()) {
                return {};
            }
            components.pop_back();
        }
        else {
            components.push_back(std::move(current));
        }
    }

    for (std::size_t i = 0; i < components.size(); ++i) {
        if (i > 0) {
            result.push_back('/');
        }
        result.append(components[i]);
    }
    return result;
}

std::string parent_dir(const std::string& path) {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return {};
    }
    if (pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}

bool ensure_destination_root(const std::string& destinationDir, Error& err) {
    struct stat st{};
    if (::lstat(destinationDir.c_str(), &st) == 0) {
        err.code = EEXIST;
        err.message = "Destination already exists";
        return false;
    }

    if (!FsOps::make_dir_parents(parent_dir(destinationDir), err)) {
        return false;
    }

    if (::mkdir(destinationDir.c_str(), 0777) != 0) {
        set_error(err, "mkdir");
        return false;
    }
    return true;
}

bool ensure_parent_dirs(const std::string& root, const std::string& rel, Error& err) {
    if (rel.empty()) {
        return true;
    }
    std::string full = root;
    full.push_back('/');
    full += rel;
    return FsOps::make_dir_parents(full, err);
}

bool apply_xattrs(int fd, const std::string& path, archive_entry* entry, const Options& opts, Error& err) {
    if (!opts.keepXattrs) {
        return true;
    }

    const char* name = nullptr;
    const void* value = nullptr;
    std::size_t size = 0;
    archive_entry_xattr_reset(entry);
    while (archive_entry_xattr_next(entry, &name, &value, &size) == ARCHIVE_OK) {
        if (!name || !value) {
            continue;
        }
        int rc = (fd >= 0) ? ::fsetxattr(fd, name, value, size, 0) : ::setxattr(path.c_str(), name, value, size, 0);
        if (rc != 0 && errno != ENOTSUP && errno != EPERM) {
            set_error(err, "setxattr");
            return false;
        }
    }
    return true;
}

void apply_metadata(int fd, const std::string& path, archive_entry* entry, const Options& opts, bool isSymlink) {
    if (opts.keepPermissions && !isSymlink) {
        const mode_t m = archive_entry_perm(entry);
        if (m != 0) {
            if (fd >= 0) {
                ::fchmod(fd, m);
            }
            else if (!path.empty()) {
                ::chmod(path.c_str(), m);
            }
        }
    }

    if (opts.keepOwnership) {
        const uid_t uid = archive_entry_uid(entry);
        const gid_t gid = archive_entry_gid(entry);
        if (fd >= 0) {
            ::fchown(fd, uid, gid);
        }
        else if (!path.empty()) {
            if (isSymlink) {
                ::lchown(path.c_str(), uid, gid);
            }
            else {
                ::chown(path.c_str(), uid, gid);
            }
        }
    }

    struct timespec times[2];
    times[0].tv_sec = archive_entry_atime(entry);
    times[0].tv_nsec = archive_entry_atime_nsec(entry);
    times[1].tv_sec = archive_entry_mtime(entry);
    times[1].tv_nsec = archive_entry_mtime_nsec(entry);
    if (!path.empty()) {
        const int flags = isSymlink ? AT_SYMLINK_NOFOLLOW : 0;
        ::utimensat(AT_FDCWD, path.c_str(), times, flags);
    }
}

unsigned thread_count_from_opts(const Options& opts) {
    if (!opts.enableFilterThreads) {
        return 1;
    }
    if (opts.maxFilterThreads > 0) {
        return opts.maxFilterThreads;
    }
    const unsigned hc = std::thread::hardware_concurrency();
    return hc > 0 ? hc : 1;
}

void configure_filter_threads(struct archive* ar, const Options& opts) {
    const unsigned threads = thread_count_from_opts(opts);
    if (threads <= 1) {
        return;
    }

    const std::string value = std::to_string(threads);
    constexpr const char* filters[] = {"zstd", "xz", "gzip", "bzip2", "lz4"};
    for (const char* f : filters) {
        archive_read_set_filter_option(ar, f, "threads", value.c_str());
    }
}

bool open_reader(const std::string& archivePath, const Options& opts, struct archive*& out, Error& err) {
    struct archive* ar = archive_read_new();
    if (!ar) {
        err.code = ENOMEM;
        err.message = "Failed to allocate archive reader";
        return false;
    }
    archive_read_support_filter_all(ar);
    archive_read_support_format_all(ar);
    configure_filter_threads(ar, opts);
    if (archive_read_open_filename(ar, archivePath.c_str(), 128 * 1024) != ARCHIVE_OK) {
        set_archive_error(err, ar, "archive_read_open_filename");
        archive_read_free(ar);
        return false;
    }
    out = ar;
    return true;
}

bool scan_archive(const std::string& archivePath, const Options& opts, ProgressInfo& progress, Error& err) {
    struct archive* ar = nullptr;
    if (!open_reader(archivePath, opts, ar, err)) {
        return false;
    }

    archive_entry* entry = nullptr;
    int r = ARCHIVE_OK;
    while ((r = archive_read_next_header(ar, &entry)) == ARCHIVE_OK) {
        const char* rawPath = archive_entry_pathname(entry);
        const std::string rel = sanitize_path(rawPath);
        if (rel.empty()) {
            err.code = EINVAL;
            err.message = "Unsafe path in archive entry";
            archive_read_close(ar);
            archive_read_free(ar);
            return false;
        }
        const auto type = archive_entry_filetype(entry);
        if (type == AE_IFREG) {
            const la_int64_t sz = archive_entry_size(entry);
            if (sz > 0) {
                const std::uint64_t s = static_cast<std::uint64_t>(sz);
                if (s <= std::numeric_limits<std::uint64_t>::max() - progress.bytesTotal) {
                    progress.bytesTotal += s;
                }
            }
        }
        progress.filesTotal += 1;
        archive_read_data_skip(ar);
    }

    if (r != ARCHIVE_EOF && r != ARCHIVE_OK) {
        set_archive_error(err, ar, "archive_read_next_header");
        archive_read_close(ar);
        archive_read_free(ar);
        return false;
    }

    archive_read_close(ar);
    archive_read_free(ar);
    return true;
}

bool write_all(int fd, const void* data, std::size_t size, off_t offset, Error& err) {
    const std::uint8_t* ptr = static_cast<const std::uint8_t*>(data);
    std::size_t remaining = size;
    off_t off = offset;
    while (remaining > 0) {
        const ssize_t n = ::pwrite(fd, ptr, remaining, off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_error(err, "write");
            return false;
        }
        remaining -= static_cast<std::size_t>(n);
        ptr += n;
        off += n;
    }
    return true;
}

bool extract_regular_file(struct archive* ar,
                          archive_entry* entry,
                          const std::string& fullPath,
                          const std::string& relPath,
                          const std::string& destinationDir,
                          const Options& opts,
                          ProgressInfo& progress,
                          const ProgressCallback& cb,
                          Error& err) {
    if (!ensure_parent_dirs(destinationDir, parent_dir(relPath), err)) {
        return false;
    }

    int flags = O_WRONLY | O_CREAT | O_CLOEXEC;
    if (opts.overwriteExisting) {
        flags |= O_TRUNC;
    }
    else {
        flags |= O_EXCL;
    }

    mode_t mode = archive_entry_perm(entry);
    if (mode == 0) {
        mode = 0666;
    }

    Fd fd(::open(fullPath.c_str(), flags, mode));
    if (!fd.valid()) {
        set_error(err, "open");
        return false;
    }

    const void* buff = nullptr;
    std::size_t size = 0;
    la_int64_t offset = 0;
    while (true) {
        const la_int64_t r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF) {
            break;
        }
        if (r != ARCHIVE_OK) {
            set_archive_error(err, ar, "archive_read_data_block");
            return false;
        }

        if (size > 0 && buff) {
            if (!write_all(fd.fd, buff, size, static_cast<off_t>(offset), err)) {
                return false;
            }
            progress.bytesDone += static_cast<std::uint64_t>(size);
            progress.currentPath = relPath;
            if (!should_continue(cb, progress)) {
                err.code = ECANCELED;
                err.message = "Cancelled";
                return false;
            }
        }
    }

    Error xerr;
    if (!apply_xattrs(fd.fd, fullPath, entry, opts, xerr)) {
        err = xerr;
        return false;
    }
    apply_metadata(fd.fd, fullPath, entry, opts, false);
    progress.filesDone += 1;
    return true;
}

bool extract_directory(archive_entry* entry,
                       const std::string& fullPath,
                       const std::string& relPath,
                       const std::string& destinationDir,
                       const Options& opts,
                       ProgressInfo& progress,
                       Error& err) {
    mode_t mode = archive_entry_perm(entry);
    if (mode == 0) {
        mode = 0777;
    }

    if (!ensure_parent_dirs(destinationDir, parent_dir(relPath), err)) {
        return false;
    }

    if (::mkdir(fullPath.c_str(), mode) != 0 && errno != EEXIST) {
        set_error(err, "mkdir");
        return false;
    }

    apply_metadata(-1, fullPath, entry, opts, false);
    progress.filesDone += 1;
    return true;
}

bool extract_symlink(archive_entry* entry,
                     const std::string& fullPath,
                     const std::string& relPath,
                     const std::string& destinationDir,
                     const Options& opts,
                     ProgressInfo& progress,
                     Error& err) {
    if (!opts.keepSymlinks) {
        return true;
    }

    const char* target = archive_entry_symlink(entry);
    if (!target) {
        return true;
    }

    if (!ensure_parent_dirs(destinationDir, parent_dir(relPath), err)) {
        return false;
    }

    if (opts.overwriteExisting) {
        ::unlink(fullPath.c_str());
    }

    if (::symlink(target, fullPath.c_str()) != 0) {
        set_error(err, "symlink");
        return false;
    }

    apply_metadata(-1, fullPath, entry, opts, true);
    progress.filesDone += 1;
    return true;
}

bool extract_hardlink(archive_entry* entry,
                      const std::string& fullPath,
                      const std::string& relPath,
                      const std::string& destinationDir,
                      ProgressInfo& progress,
                      Error& err) {
    const char* target = archive_entry_hardlink(entry);
    if (!target) {
        return true;
    }
    const std::string sanitized = sanitize_path(target);
    if (sanitized.empty()) {
        return true;
    }

    if (!ensure_parent_dirs(destinationDir, parent_dir(relPath), err)) {
        return false;
    }

    std::string targetFull = destinationDir;
    targetFull.push_back('/');
    targetFull += sanitized;

    if (::link(targetFull.c_str(), fullPath.c_str()) != 0) {
        set_error(err, "link");
        return false;
    }
    progress.filesDone += 1;
    return true;
}

}  // namespace

bool extract_archive(const std::string& archivePath,
                     const std::string& destinationDir,
                     ProgressInfo& progress,
                     const ProgressCallback& callback,
                     Error& err,
                     const Options& opts) {
    progress = {};
    err = {};

    if (archivePath.empty() || destinationDir.empty()) {
        err.code = EINVAL;
        err.message = "Invalid archive or destination path";
        return false;
    }

    if (!ensure_destination_root(destinationDir, err)) {
        return false;
    }

    ProgressInfo scanProgress;
    if (!scan_archive(archivePath, opts, scanProgress, err)) {
        FsOps::Error cleanupErr;
        ProgressInfo cleanupProg;
        FsOps::delete_path(destinationDir, cleanupProg, ProgressCallback(), cleanupErr);
        return false;
    }
    progress.bytesTotal = scanProgress.bytesTotal;
    progress.filesTotal = scanProgress.filesTotal;

    struct archive* ar = nullptr;
    if (!open_reader(archivePath, opts, ar, err)) {
        FsOps::Error cleanupErr;
        ProgressInfo cleanupProg;
        FsOps::delete_path(destinationDir, cleanupProg, ProgressCallback(), cleanupErr);
        return false;
    }

    archive_entry* entry = nullptr;
    bool ok = true;
    while (archive_read_next_header(ar, &entry) == ARCHIVE_OK) {
        const char* rawPath = archive_entry_pathname(entry);
        std::string rel = sanitize_path(rawPath);
        if (rel.empty()) {
            err.code = EINVAL;
            err.message = "Unsafe path in archive entry";
            ok = false;
            break;
        }

        std::string fullPath = destinationDir;
        fullPath.push_back('/');
        fullPath += rel;

        progress.currentPath = rel;
        if (!should_continue(callback, progress)) {
            err.code = ECANCELED;
            err.message = "Cancelled";
            ok = false;
            break;
        }

        const char* hardlink = archive_entry_hardlink(entry);
        if (hardlink) {
            if (!extract_hardlink(entry, fullPath, rel, destinationDir, progress, err)) {
                ok = false;
                break;
            }
            archive_read_data_skip(ar);
            continue;
        }

        const auto type = archive_entry_filetype(entry);
        switch (type) {
            case AE_IFREG: {
                if (!extract_regular_file(ar, entry, fullPath, rel, destinationDir, opts, progress, callback, err)) {
                    ok = false;
                }
                break;
            }
            case AE_IFDIR: {
                if (!extract_directory(entry, fullPath, rel, destinationDir, opts, progress, err)) {
                    ok = false;
                }
                archive_read_data_skip(ar);
                break;
            }
            case AE_IFLNK: {
                if (!extract_symlink(entry, fullPath, rel, destinationDir, opts, progress, err)) {
                    ok = false;
                }
                archive_read_data_skip(ar);
                break;
            }
            default: {
                archive_read_data_skip(ar);  // unsupported special files or metadata entries
                break;
            }
        }

        if (!ok) {
            break;
        }
    }

    archive_read_close(ar);
    archive_read_free(ar);

    if (!ok) {
        FsOps::Error cleanupErr;
        ProgressInfo cleanupProg;
        FsOps::delete_path(destinationDir, cleanupProg, ProgressCallback(), cleanupErr);
    }

    return ok;
}

}  // namespace PCManFM::ArchiveExtract
