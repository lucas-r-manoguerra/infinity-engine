// tests/core/fs_os_stub_test.cpp
//
// Non-POSIX host contract for the POSIX filesystem backend (F3.5, ADR-023,
// rule 04): on hosts without the POSIX file API (Windows today), PosixFileSystem
// is a stub and every operation reports UNSUPPORTED, so the type and the API
// stay available and the missing capability is an explicit, documented error
// instead of a link failure. The stub keeps the real backend's contract:
// it probes the injected FaultInjector first (ADR-016) and honors an injected
// fault before reporting UNSUPPORTED. Compiled only on non-POSIX hosts (see
// tests/core/CMakeLists.txt); on POSIX hosts the real backend is exercised by
// fs_os_test.cpp, fs_os_tree_test.cpp and fs_os_fault_test.cpp.
//
// CI note: the Windows job of this PR stopped triggering on pushes for a while
// (GitHub Actions did not deliver pull_request synchronize events); the PR was
// reopened to force the workflow. Empty commits are forbidden (rule 10), so the
// re-trigger commit carries this comment instead of an empty tree.
//
// ADL note (same as error_test.cpp): infinity::core::toString would win ADL
// over doctest's toString for a CoreError operand, so no CHECK ever compares
// a CoreError directly; failsWith() isolates the comparison.
#include "infinity/core/fs.h"
#include "infinity/core/fs_os.h"
#include "infinity/core/testing/fault_injector.h"

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string_view>

#include <doctest/doctest.h>

namespace {

using infinity::core::CoreError;
using infinity::core::DirectoryEntry;
using infinity::core::Expected;
using infinity::core::ExpectedVoid;
using infinity::core::PosixFileSystem;
using infinity::core::testing::FaultInjector;

constexpr std::string_view PATH = "path";
constexpr std::string_view OTHER = "other";

// True when result is an error carrying expected. Isolates CoreError operands
// from CHECK so doctest never stringifies one (ADL note, see file brief).
template <typename T>
[[nodiscard]] bool failsWith(const Expected<T>& result, CoreError expected) noexcept {
    return !result.has_value() &&
           infinity::core::toString(result.error()) == infinity::core::toString(expected);
}

// Collects directory entries; unused on the stub path but keeps the callback
// signature named (clang-tidy readability-named-parameter).
void collectEntries(const DirectoryEntry& entry, void* userData) noexcept {
    (void)entry;
    (void)userData;
}

TEST_CASE("PosixFileSystem stub honors an injected fault before UNSUPPORTED") {
    FaultInjector injector;
    injector.failNext("fs.exists", CoreError::IO_ERROR);
    PosixFileSystem fs(injector);
    CHECK(failsWith(fs.exists(PATH), CoreError::IO_ERROR));
}

TEST_CASE("PosixFileSystem stub reports UNSUPPORTED for exists") {
    FaultInjector injector;
    PosixFileSystem fs(injector);
    CHECK(failsWith(fs.exists(PATH), CoreError::UNSUPPORTED));
}

TEST_CASE("PosixFileSystem stub reports UNSUPPORTED for fileSize") {
    FaultInjector injector;
    PosixFileSystem fs(injector);
    CHECK(failsWith(fs.fileSize(PATH), CoreError::UNSUPPORTED));
}

TEST_CASE("PosixFileSystem stub reports UNSUPPORTED for readFile") {
    FaultInjector injector;
    PosixFileSystem fs(injector);
    CHECK(failsWith(fs.readFile(PATH, nullptr, 0), CoreError::UNSUPPORTED));
}

TEST_CASE("PosixFileSystem stub reports UNSUPPORTED for writeFile") {
    FaultInjector injector;
    PosixFileSystem fs(injector);
    CHECK(failsWith(fs.writeFile(PATH, nullptr, 0), CoreError::UNSUPPORTED));
}

TEST_CASE("PosixFileSystem stub reports UNSUPPORTED for remove") {
    FaultInjector injector;
    PosixFileSystem fs(injector);
    CHECK(failsWith(fs.remove(PATH), CoreError::UNSUPPORTED));
}

TEST_CASE("PosixFileSystem stub reports UNSUPPORTED for rename") {
    FaultInjector injector;
    PosixFileSystem fs(injector);
    CHECK(failsWith(fs.rename(PATH, OTHER), CoreError::UNSUPPORTED));
}

TEST_CASE("PosixFileSystem stub reports UNSUPPORTED for makeDirectory") {
    FaultInjector injector;
    PosixFileSystem fs(injector);
    CHECK(failsWith(fs.makeDirectory(PATH), CoreError::UNSUPPORTED));
}

TEST_CASE("PosixFileSystem stub reports UNSUPPORTED for listDirectory") {
    FaultInjector injector;
    PosixFileSystem fs(injector);
    const ExpectedVoid result = fs.listDirectory(PATH, collectEntries, nullptr);
    CHECK(failsWith(result, CoreError::UNSUPPORTED));
}

} // namespace
