// infinity/core/src/fs_os_detail.cpp
//
// Implementations of the stateless POSIX filesystem backend helpers declared
// in fs_os_detail.h (F3.5, ADR-023, rules 04/08/11). Split out of fs_os.cpp
// to keep each file under the ~300-line file limit (rule 02).
#include "fs_os_detail.h"

#include "infinity/core/utf8.h"

#include <cerrno>
#include <string>
#include <string_view>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace infinity::core::fs_os_detail {

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

ExpectedVoid validatePath(std::string_view path) noexcept {
    if (path.empty()) {
        return std::unexpected(CoreError::IO_NOT_FOUND);
    }
    if (!isValidUtf8(path)) {
        return std::unexpected(CoreError::INVALID_UTF8);
    }
    return {};
}

bool pathExists(const std::string& path) noexcept {
    struct stat status{};
    return ::stat(path.c_str(), &status) == 0;
}

bool isDescendant(std::string_view dir, std::string_view child) noexcept {
    return child.size() > dir.size() && child.starts_with(dir) && child[dir.size()] == '/';
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

} // namespace infinity::core::fs_os_detail
