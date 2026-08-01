// infinity/core/fs.h
//
// Filesystem interface (F2.8, ADR-023, rule 04). Platform-agnostic file and
// directory operations that every subsystem touching disk goes through:
//
//   Encoding   - ADR-023 makes UTF-8 the only encoding in the engine: every
//                path is a UTF-8 std::string_view. A malformed path is a
//                recoverable caller error (INVALID_UTF8, utf8.h); an empty
//                path names no entry and is reported as IO_NOT_FOUND.
//   Errors     - Every operation reports through std::expected<T, CoreError>
//                (rule 04). The IO_* codes carry the whole contract:
//                IO_NOT_FOUND (path or an ancestor is absent), IO_WRONG_TYPE
//                (the path exists but with the wrong kind, file vs directory),
//                IO_ALREADY_EXISTS (the path exists where the operation
//                requires it absent), IO_INVALID_DATA (remaining per-method
//                state or format violations), and IO_ERROR (unspecified
//                backend failure). Callers handle, translate or close errors;
//                swallowing one is a bug (rule 04).
//   Allocation - None in the data path: readFile/writeFile take caller-owned
//                buffers, so transferring file data never allocates (rule 03).
//                listDirectory streams entries through a callback and never
//                materializes a listing (rule 08).
//   Ownership  - Pure interface: backends are injected, never owned, and must
//                outlive their users. Copying would slice the backend, so
//                FileSystem is non-copyable, exactly like Allocator
//                (allocator.h).
//   Determinism- Backends keep all state in the object (rule 11, ADR-056): no
//                mutable globals, no environment, no wall clock. The
//                in-memory backend (fs_in_memory.h) is the deterministic
//                reference for tests; real OS backends land in F3.5.
//
// Path grammar is backend-specific (separator, absolute vs relative) and is
// documented by each backend; the interface only requires non-empty
// well-formed UTF-8.
#pragma once

#include "infinity/core/error.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace infinity::core {

// One child reported to a listDirectory callback: the entry name (UTF-8,
// ADR-023) and whether it is a directory. The name views backend-owned memory
// and is valid only during the callback invocation; the callback must not
// call back into the filesystem (reentrancy is undefined behavior).
struct DirectoryEntry {
    std::string_view name;    ///< UTF-8 child name, valid during the callback call
    bool isDirectory = false; ///< true when the child is a directory
};

// Callback invoked once per entry of a listed directory. Receives the entry
// and the opaque userData the caller passed to listDirectory; userData lets
// the callback capture state without allocating or closing over anything.
using DirectoryCallback = void (*)(const DirectoryEntry& entry, void* userData);

// Abstract file and directory operations (F2.8, ADR-023, rule 04). All
// methods are noexcept: failures are reported through the expected, never by
// throwing (rule 04). Results that must not be ignored are [[nodiscard]].
class FileSystem {
public:
    // Polymorphic base: copying would slice the backend, so filesystems are
    // always passed by reference (rule 03: no state sharing across backends).
    FileSystem(const FileSystem&) = delete;
    FileSystem& operator=(const FileSystem&) = delete;

    virtual ~FileSystem() = default;

    // Reports whether path exists (file or directory). A missing path reports
    // false, never an error; only an invalid or unreachable path errors.
    // Errors: INVALID_UTF8 (malformed path), IO_ERROR.
    [[nodiscard]] virtual Expected<bool> exists(std::string_view path) noexcept = 0;

    // Returns the size in bytes of the file at path.
    // Errors: IO_NOT_FOUND (missing), IO_WRONG_TYPE (path is a directory),
    //         INVALID_UTF8, IO_ERROR.
    [[nodiscard]] virtual Expected<uint64_t> fileSize(std::string_view path) noexcept = 0;

    // Reads the whole file at path into buffer, returning the number of bytes
    // read (== file size). buffer must hold at least capacity bytes and is
    // left untouched when the read fails. A partial read is never silently
    // accepted: capacity smaller than the file reports INVALID_SIZE, so
    // callers size the buffer with fileSize() first.
    // Errors: IO_NOT_FOUND, IO_WRONG_TYPE (directory), INVALID_SIZE (capacity
    //         < file size), INVALID_UTF8, IO_ERROR.
    [[nodiscard]] virtual Expected<size_t> readFile(std::string_view path, void* buffer,
                                                    size_t capacity) noexcept = 0;

    // Writes exactly size bytes from data to path, creating it or truncating
    // and overwriting an existing file, and returns size. data must hold size
    // bytes (ignored when size is zero). The parent directory must exist.
    // Errors: IO_NOT_FOUND (parent missing), IO_WRONG_TYPE (path is a
    //         directory or an ancestor is a file), INVALID_UTF8, IO_ERROR.
    [[nodiscard]] virtual Expected<size_t> writeFile(std::string_view path, const void* data,
                                                     size_t size) noexcept = 0;

    // Removes the file or empty directory at path. A non-empty directory is
    // rejected: removal never recurses.
    // Errors: IO_NOT_FOUND, IO_INVALID_DATA (directory is not empty),
    //         INVALID_UTF8, IO_ERROR.
    [[nodiscard]] virtual ExpectedVoid remove(std::string_view path) noexcept = 0;

    // Moves the entry at from to to. to must not exist (no silent overwrite,
    // rule 11); from must exist. Moving a directory into its own subtree is
    // rejected.
    // Errors: IO_NOT_FOUND (from missing, or to's parent missing),
    //         IO_ALREADY_EXISTS (to exists), IO_INVALID_DATA (directory moved
    //         into its own subtree), INVALID_UTF8, IO_ERROR.
    [[nodiscard]] virtual ExpectedVoid rename(std::string_view from,
                                              std::string_view to) noexcept = 0;

    // Creates the directory at path; every parent component must already
    // exist.
    // Errors: IO_NOT_FOUND (parent missing), IO_ALREADY_EXISTS (path exists),
    //         IO_WRONG_TYPE (an ancestor is a file), INVALID_UTF8, IO_ERROR.
    [[nodiscard]] virtual ExpectedVoid makeDirectory(std::string_view path) noexcept = 0;

    // Invokes callback once per entry directly under path, with the entry's
    // name and type, in backend-defined deterministic order (sorted for the
    // in-memory backend). Iteration never allocates per entry (rule 08):
    // entries are streamed, not collected. callback must not be nullptr.
    // Errors: IO_NOT_FOUND, IO_WRONG_TYPE (path is a file), INVALID_UTF8,
    //         IO_ERROR.
    [[nodiscard]] virtual ExpectedVoid
    listDirectory(std::string_view path, DirectoryCallback callback, void* userData) noexcept = 0;

protected:
    FileSystem() = default;
};

} // namespace infinity::core
