// infinity/core/src/fs_os_detail.h
//
// Internal helpers for the POSIX filesystem backend (F3.5, ADR-023). Lives in
// src/ because it is not part of the public API (rule 01): only fs_os.cpp
// includes it, so it stays outside the exported interface of infinity_core.
//
// This file holds the stateless parts of the backend: the errno -> CoreError
// contract mapping (rule 04), the path contract validation (ADR-023) and the
// path/entry classification helpers (rename subtree check, d_type+stat entry
// classification). fs_os.cpp keeps only the eight FileSystem operations.
#pragma once

#include "infinity/core/fs.h"

#include <string>
#include <string_view>

namespace infinity::core::fs_os_detail {

constexpr int DIRECTORY_MODE = 0755; // rwxr-xr-x, masked by the process umask
constexpr int WRITE_MODE = 0644;     // rw-r--r--, masked by the process umask

// Maps a host errno to the CoreError contract (rule 04). ENOENT, ENOTEMPTY,
// EINVAL and friends map to the documented IO_* codes; everything else is an
// unspecified backend failure (IO_ERROR).
[[nodiscard]] CoreError mapErrno(int error) noexcept;

// Validates the backend path contract: non-empty and well-formed UTF-8
// (ADR-023). POSIX accepts arbitrary bytes, so validation must happen before
// the OS ever sees the path. An empty path names no entry.
[[nodiscard]] ExpectedVoid validatePath(std::string_view path) noexcept;

// Reports whether path resolves to an existing entry. Unlike exists(), this
// never reports errors and never probes the injector: it serves the rename()
// no-replace fallback, which must not consume an "fs.exists" probe.
[[nodiscard]] bool pathExists(const std::string& path) noexcept;

// True when child is a descendant of dir (dir/...): moving a directory into
// its own subtree is rejected by the rename contract. Path-string check only;
// the OS still guards the authoritative resolution.
[[nodiscard]] bool isDescendant(std::string_view dir, std::string_view child) noexcept;

// Classifies a directory child. d_type is authoritative on filesystems that
// report it; DT_UNKNOWN and DT_LNK (symlinks) fall back to a stat, so a
// symlink to a directory counts as a directory, consistent with the
// stat-based checks elsewhere in the backend.
[[nodiscard]] bool entryIsDirectory(const std::string& parent, std::string_view name,
                                    unsigned char dType) noexcept;

} // namespace infinity::core::fs_os_detail
