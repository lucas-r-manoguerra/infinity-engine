// infinity/core/src/fs_os_detail.cpp
//
// Implementations of the stateless POSIX filesystem backend helpers declared
// in fs_os_detail.h (F3.5, ADR-023, rules 04/08/11). Split out of fs_os.cpp
// to keep each file under the ~300-line file limit (rule 02).
//
// Portability: mapErrno, pathExists and entryIsDirectory need the POSIX file
// API (errno, stat, d_type), so they are compiled on Linux and macOS only and
// stubbed elsewhere (Windows today) to keep the symbols linkable. validatePath
// and isDescendant are pure string contracts and compile on every host.
#include "fs_os_detail.h"

#include "infinity/core/utf8.h"

#include <string>
#include <string_view>

#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace infinity::core::fs_os_detail {

#if defined(__unix__) || defined(__APPLE__)

CoreError mapErrno(int error) noexcept {
    switch (error) {
    case ENOENT:
        return CoreError::IO_NOT_FOUND;
    case EACCES:
    case EPERM:
        return CoreError::IO_PERMISSION_DENIED;
    case EEXIST:
        return CoreError::IO_ALREADY_EXISTS;
    case ENOTEMPTY:
    case EINVAL:
        return CoreError::IO_INVALID_DATA;
    case ENOTDIR:
    case EISDIR:
        return CoreError::IO_WRONG_TYPE;
    default:
        return CoreError::IO_ERROR;
    }
}

bool pathExists(const std::string& path) noexcept {
    struct stat status{};
    return ::stat(path.c_str(), &status) == 0;
}

bool entryIsDirectory(const std::string& parent, std::string_view name,
                      unsigned char dType) noexcept {
    if (dType == DT_DIR) {
        return true;
    }
    if (dType == DT_REG) {
        return false;
    }
    const std::string child = parent + "/" + std::string(name);
    struct stat status{};
    return ::stat(child.c_str(), &status) == 0 && S_ISDIR(status.st_mode);
}

#else

// Non-POSIX hosts (Windows today): never reached (fs_os.cpp stubs every
// operation to NOT_SUPPORTED before calling a helper), but the symbols must
// exist so the static library links. Parameters are named and ignored to
// satisfy -Werror and clang-tidy.
CoreError mapErrno(int error) noexcept {
    (void)error;
    return CoreError::IO_ERROR;
}

bool pathExists(const std::string& path) noexcept {
    (void)path;
    return false;
}

bool entryIsDirectory(const std::string& parent, std::string_view name,
                      unsigned char dType) noexcept {
    (void)parent;
    (void)name;
    (void)dType;
    return false;
}

#endif // defined(__unix__) || defined(__APPLE__)

ExpectedVoid validatePath(std::string_view path) noexcept {
    if (path.empty()) {
        return std::unexpected(CoreError::IO_NOT_FOUND);
    }
    if (!isValidUtf8(path)) {
        return std::unexpected(CoreError::INVALID_UTF8);
    }
    return {};
}

bool isDescendant(std::string_view dir, std::string_view child) noexcept {
    return child.size() > dir.size() && child.starts_with(dir) && child[dir.size()] == '/';
}

} // namespace infinity::core::fs_os_detail
