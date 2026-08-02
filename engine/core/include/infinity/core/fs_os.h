// infinity/core/fs_os.h
//
// POSIX filesystem backend (F3.5, ADR-023, rule 04): a FileSystem
// implementation over the host OS file API (Linux/macOS). It shares the
// interface and error contract of the in-memory backend (fs_in_memory.h,
// F2.8) and, unlike that backend, performs real IO, so rule 08 budgets apply:
// zero allocations in the data path (caller-owned read/write buffers) and
// streamed directory listings.
//
// Path grammar (backend contract, enforced):
//   - non-empty, well-formed UTF-8 (ADR-023); malformed bytes -> INVALID_UTF8
//   - paths are handed to the host as-is: there is no virtual root, so
//     relative paths resolve against the current working directory, absolute
//     paths are honored, and "." / ".." resolve like the host resolves them
//   - an empty path names no entry: exists() reports false and every other
//     operation reports IO_NOT_FOUND
//
// Fault injection (F2.4, ADR-016): the constructor takes a FaultInjector& and
// every operation probes its key ("fs.exists", "fs.writeFile", ...) with the
// same keys as the in-memory backend, before any validation or OS call, so an
// injected failure masks the operation entirely and leaves the filesystem
// untouched. The injector must outlive this backend. fault_injector.h is a
// test-only header, so it is forward-declared here and included only by the
// implementation.
//
// Portability: the public surface never includes POSIX headers (rule 02); all
// system includes live in fs_os.cpp. The backend compiles on Linux and macOS;
// rename without overwrite uses renameat2(RENAME_NOREPLACE) on Linux with a
// documented check-then-rename fallback elsewhere (see fs_os.cpp).
#pragma once

#include "infinity/core/fs.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace infinity::core::testing {
class FaultInjector;
} // namespace infinity::core::testing

namespace infinity::core {

// POSIX FileSystem backend (F3.5): real file and directory operations over the
// host OS, fault-injectable. See the header brief for the contract.
class PosixFileSystem final : public FileSystem {
public:
    // The injector is consulted by every operation before it acts (ADR-016).
    // It must outlive this backend.
    explicit PosixFileSystem(infinity::core::testing::FaultInjector& injector) noexcept;

    [[nodiscard]] Expected<bool> exists(std::string_view path) noexcept override;
    [[nodiscard]] Expected<uint64_t> fileSize(std::string_view path) noexcept override;
    [[nodiscard]] Expected<size_t> readFile(std::string_view path, void* buffer,
                                            size_t capacity) noexcept override;
    [[nodiscard]] Expected<size_t> writeFile(std::string_view path, const void* data,
                                             size_t size) noexcept override;
    [[nodiscard]] ExpectedVoid remove(std::string_view path) noexcept override;
    [[nodiscard]] ExpectedVoid rename(std::string_view from, std::string_view to) noexcept override;
    [[nodiscard]] ExpectedVoid makeDirectory(std::string_view path) noexcept override;
    [[nodiscard]] ExpectedVoid listDirectory(std::string_view path, DirectoryCallback callback,
                                             void* userData) noexcept override;

private:
    infinity::core::testing::FaultInjector* m_injector; // non-owning, must outlive this backend
};

} // namespace infinity::core
