// infinity/core/src/fs_os.cpp
//
// POSIX filesystem backend (F3.5, ADR-023, rules 04/08/11). Every operation
// probes the injected FaultInjector first (ADR-016), then validates the path
// (empty -> IO_NOT_FOUND, malformed UTF-8 -> INVALID_UTF8), then calls the
// host. Data paths never allocate; errors map via fs_os_detail::mapErrno.
// rename never overwrites: renameat2(RENAME_NOREPLACE) on Linux, else a
// check-then-rename fallback (accepted TOCTOU race).
#include "infinity/core/fs_os.h"
#include "infinity/core/testing/fault_injector.h"
#include "infinity/core/utf8.h"

#include "fs_os_detail.h"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

namespace infinity::core {

PosixFileSystem::PosixFileSystem(infinity::core::testing::FaultInjector& injector) noexcept
    : m_injector(&injector) {}

Expected<bool> PosixFileSystem::exists(std::string_view path) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.exists");
    if (!probe.has_value()) {
        return std::unexpected(probe.error());
    }
    if (path.empty()) {
        return false;
    }
    if (!isValidUtf8(path)) {
        return std::unexpected(CoreError::INVALID_UTF8);
    }
    const std::string cPath(path);
    struct stat status{};
    if (::stat(cPath.c_str(), &status) != 0) {
        if (errno == ENOENT || errno == ENOTDIR) {
            return false; // missing, or a parent component is not a directory
        }
        return std::unexpected(fs_os_detail::mapErrno(errno));
    }
    return true;
}

Expected<uint64_t> PosixFileSystem::fileSize(std::string_view path) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.fileSize");
    if (!probe.has_value()) {
        return std::unexpected(probe.error());
    }
    const ExpectedVoid validated = fs_os_detail::validatePath(path);
    if (!validated.has_value()) {
        return std::unexpected(validated.error());
    }
    const std::string cPath(path);
    struct stat status{};
    if (::stat(cPath.c_str(), &status) != 0) {
        return std::unexpected(fs_os_detail::mapErrno(errno));
    }
    if (S_ISDIR(status.st_mode)) {
        return std::unexpected(CoreError::IO_WRONG_TYPE);
    }
    return static_cast<uint64_t>(status.st_size);
}

Expected<std::size_t> PosixFileSystem::readFile(std::string_view path, void* buffer,
                                                std::size_t capacity) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.readFile");
    if (!probe.has_value()) {
        return std::unexpected(probe.error());
    }
    const ExpectedVoid validated = fs_os_detail::validatePath(path);
    if (!validated.has_value()) {
        return std::unexpected(validated.error());
    }
    const std::string cPath(path);
    const int fd = ::open(cPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(fs_os_detail::mapErrno(errno));
    }
    const auto closeFd = [&]() noexcept { ::close(fd); };

    struct stat status{};
    if (::fstat(fd, &status) != 0) {
        const CoreError error = fs_os_detail::mapErrno(errno);
        closeFd();
        return std::unexpected(error);
    }
    if (S_ISDIR(status.st_mode)) {
        closeFd();
        return std::unexpected(CoreError::IO_WRONG_TYPE);
    }
    const auto size = static_cast<std::size_t>(status.st_size);
    if (capacity < size) {
        closeFd();
        return std::unexpected(CoreError::INVALID_SIZE); // buffer left untouched
    }
    if (size == 0) {
        closeFd();
        return 0; // empty file: nothing to copy, buffer left untouched
    }
    assert(buffer != nullptr);
    std::size_t total = 0;
    while (total < size) {
        const ssize_t n = ::read(fd, static_cast<char*>(buffer) + total, size - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            const CoreError error = fs_os_detail::mapErrno(errno);
            closeFd();
            return std::unexpected(error);
        }
        if (n == 0) {
            break; // unexpected EOF (the file shrank): return what was read
        }
        total += static_cast<std::size_t>(n);
    }
    closeFd();
    return total;
}

Expected<std::size_t> PosixFileSystem::writeFile(std::string_view path, const void* data,
                                                 std::size_t size) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.writeFile");
    if (!probe.has_value()) {
        return std::unexpected(probe.error());
    }
    const ExpectedVoid validated = fs_os_detail::validatePath(path);
    if (!validated.has_value()) {
        return std::unexpected(validated.error());
    }
    const std::string cPath(path);
    const int fd =
        ::open(cPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, fs_os_detail::WRITE_MODE);
    if (fd < 0) {
        return std::unexpected(fs_os_detail::mapErrno(errno));
    }
    if (size == 0) {
        ::close(fd);
        return 0; // created/truncated to an empty file, data ignored
    }
    assert(data != nullptr);
    const auto* bytes = static_cast<const char*>(data);
    std::size_t total = 0;
    while (total < size) {
        const ssize_t n = ::write(fd, bytes + total, size - total);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            const CoreError error = fs_os_detail::mapErrno(errno);
            ::close(fd);
            return std::unexpected(error);
        }
        total += static_cast<std::size_t>(n);
    }
    ::close(fd);
    return size;
}

ExpectedVoid PosixFileSystem::remove(std::string_view path) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.remove");
    if (!probe.has_value()) {
        return probe;
    }
    const ExpectedVoid validated = fs_os_detail::validatePath(path);
    if (!validated.has_value()) {
        return std::unexpected(validated.error());
    }
    const std::string cPath(path);
    struct stat status{};
    if (::stat(cPath.c_str(), &status) != 0) {
        return std::unexpected(fs_os_detail::mapErrno(errno));
    }
    if (S_ISDIR(status.st_mode)) {
        if (::rmdir(cPath.c_str()) != 0) {
            if (errno == ENOTEMPTY || errno == EEXIST) {
                return std::unexpected(CoreError::IO_INVALID_DATA); // not empty
            }
            return std::unexpected(fs_os_detail::mapErrno(errno));
        }
        return {};
    }
    if (::unlink(cPath.c_str()) != 0) {
        return std::unexpected(fs_os_detail::mapErrno(errno));
    }
    return {};
}

ExpectedVoid PosixFileSystem::rename(std::string_view from, std::string_view to) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.rename");
    if (!probe.has_value()) {
        return probe;
    }
    const ExpectedVoid validatedFrom = fs_os_detail::validatePath(from);
    if (!validatedFrom.has_value()) {
        return std::unexpected(validatedFrom.error());
    }
    const ExpectedVoid validatedTo = fs_os_detail::validatePath(to);
    if (!validatedTo.has_value()) {
        return std::unexpected(validatedTo.error());
    }
    const std::string fromCString(from);
    const std::string toCString(to);

#if defined(__linux__)
    // RENAME_NOREPLACE makes the move fail with EEXIST when the destination
    // exists, so rename never overwrites (rule 11), atomically and race-free.
    const long syscallResult = ::syscall(SYS_renameat2, AT_FDCWD, fromCString.c_str(), AT_FDCWD,
                                         toCString.c_str(), RENAME_NOREPLACE);
    if (syscallResult == 0) {
        return {};
    }
    const int error = errno;
    // EINVAL: subtree move (rejected) or no RENAME_NOREPLACE support
    // (falls through to the fallback below).
    if (error == EINVAL && fs_os_detail::isDescendant(from, to)) {
        return std::unexpected(CoreError::IO_INVALID_DATA);
    }
    if (error != ENOSYS && error != EINVAL) {
        return std::unexpected(fs_os_detail::mapErrno(error));
    }
#endif
    // Portable fallback (macOS / filesystem without RENAME_NOREPLACE):
    // check-then-rename, non-atomic (accepted race, F3.5); never probes
    // "fs.exists" again (the probe above already served this operation).
    if (fs_os_detail::pathExists(toCString)) {
        return std::unexpected(CoreError::IO_ALREADY_EXISTS);
    }
    if (::rename(fromCString.c_str(), toCString.c_str()) != 0) {
        return std::unexpected(fs_os_detail::mapErrno(errno));
    }
    return {};
}

ExpectedVoid PosixFileSystem::makeDirectory(std::string_view path) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.makeDirectory");
    if (!probe.has_value()) {
        return probe;
    }
    const ExpectedVoid validated = fs_os_detail::validatePath(path);
    if (!validated.has_value()) {
        return std::unexpected(validated.error());
    }
    const std::string cPath(path);
    if (::mkdir(cPath.c_str(), fs_os_detail::DIRECTORY_MODE) != 0) {
        return std::unexpected(fs_os_detail::mapErrno(errno));
    }
    return {};
}

ExpectedVoid PosixFileSystem::listDirectory(std::string_view path, DirectoryCallback callback,
                                            void* userData) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.listDirectory");
    if (!probe.has_value()) {
        return probe;
    }
    const ExpectedVoid validated = fs_os_detail::validatePath(path);
    if (!validated.has_value()) {
        return std::unexpected(validated.error());
    }
    assert(callback != nullptr);
    const std::string cPath(path);
    DIR* directory = ::opendir(cPath.c_str());
    if (directory == nullptr) {
        return std::unexpected(fs_os_detail::mapErrno(errno));
    }
    const auto closeDirectory = [&]() noexcept { ::closedir(directory); };

    // Names collected up front: listing is sorted before any callback fires,
    // since readdir order is filesystem-dependent (determinism, rule 11).
    std::vector<std::pair<std::string, bool>> names;
    errno = 0;
    while (const dirent* entry = ::readdir(directory)) { // NOLINT(concurrency-mt-unsafe)
        const std::string_view name(entry->d_name);
        if (name == "." || name == "..") {
            continue;
        }
        if (!isValidUtf8(name)) {
            closeDirectory();
            return std::unexpected(CoreError::INVALID_UTF8);
        }
        names.emplace_back(std::string(name),
                           fs_os_detail::entryIsDirectory(cPath, name, entry->d_type));
    }
    if (errno != 0) {
        const CoreError error = fs_os_detail::mapErrno(errno);
        closeDirectory();
        return std::unexpected(error);
    }
    closeDirectory();

    std::ranges::sort(
        names, [](const std::pair<std::string, bool>& lhs,
                  const std::pair<std::string, bool>& rhs) { return lhs.first < rhs.first; });
    for (const auto& [name, isDirectory] : names) {
        const DirectoryEntry listed{.name = name, .isDirectory = isDirectory};
        callback(listed, userData);
    }
    return {};
}

} // namespace infinity::core
