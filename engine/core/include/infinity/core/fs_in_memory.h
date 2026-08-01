// infinity/core/fs_in_memory.h
//
// Deterministic in-memory filesystem backend (F2.8, ADR-023, rules 06/11).
// The reference backend for tests and determinism mode: a flat dictionary of
// every entry keyed by its full path, with all state in the object - no
// mutable globals, no environment, no wall clock (rule 11, ADR-056). The same
// operation sequence always yields the same tree and the same listing order
// (sorted by path). Real OS backends (std::filesystem/OS-specific) land in
// F3.5 (ROADMAP); this backend is not a performance path, so rule 08 budgets
// apply to the OS backends, not here.
//
// Path grammar (backend contract, enforced):
//   - non-empty, well-formed UTF-8 (ADR-023); malformed bytes -> INVALID_UTF8
//   - relative to the virtual root: no leading or trailing '/'
//   - '/' separates components; empty components, "." and ".." are rejected
//     with IO_INVALID_DATA
//   - an empty path names no entry: exists() reports false and every other
//     operation reports IO_NOT_FOUND. The virtual root itself cannot be
//     addressed or listed.
//
// Fault injection (F2.4, ADR-016): the constructor takes a FaultInjector& and
// every operation probes its key ("fs.exists", "fs.writeFile", ...) before any
// validation or mutation, so an injected failure masks the operation entirely
// and leaves the tree untouched. This is the constructor-injection pattern
// F2.4 prefers; the injector must outlive this backend. fault_injector.h is a
// test-only header, so it is forward-declared here and included only by the
// implementation (its own contract).
#pragma once

#include "infinity/core/fs.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace infinity::core::testing {
class FaultInjector;
} // namespace infinity::core::testing

namespace infinity::core {

// In-memory FileSystem backend (F2.8): deterministic, allocation-free in the
// data path, fault-injectable. See the header brief for the contract.
class InMemoryFileSystem final : public FileSystem {
public:
    // The injector is consulted by every operation before it acts (ADR-016).
    // It must outlive this backend.
    explicit InMemoryFileSystem(infinity::core::testing::FaultInjector& injector) noexcept;

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

    // Empties the tree back to a fresh, empty filesystem (state reset for
    // deterministic test isolation).
    void clear() noexcept;

private:
    // One entry of the virtual tree. A directory holds no contents; a file's
    // contents are its bytes.
    struct Entry {
        bool isDirectory = false;
        std::vector<uint8_t> contents;
    };

    // Validates the path against the backend grammar and returns the canonical
    // key, or the caller error: INVALID_UTF8 (malformed bytes), IO_INVALID_DATA
    // (grammar violation), IO_NOT_FOUND (empty path).
    [[nodiscard]] static Expected<std::string> normalize(std::string_view path) noexcept;

    // Returns the entry stored at key, or nullptr when absent.
    [[nodiscard]] const Entry* find(const std::string& key) const noexcept;

    // Verifies that the parent of key exists and is a directory, returning the
    // parent's key ("" for a top-level entry: the root always exists). Errors:
    // IO_NOT_FOUND (parent missing), IO_WRONG_TYPE (parent is a file).
    [[nodiscard]] Expected<std::string>
    requireParentDirectory(const std::string& key) const noexcept;

    infinity::core::testing::FaultInjector* m_injector; // non-owning, must outlive this backend
    std::map<std::string, Entry> m_entries;
};

} // namespace infinity::core
