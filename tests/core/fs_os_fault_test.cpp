// tests/core/fs_os_fault_test.cpp
//
// POSIX FileSystem backend contract tests (F3.5, ADR-023, rules 04/06/08/11):
// fault-injection only (rule 01, one file = one task) — one case per operation
// proving the backend fails with the injected error before mutating the
// filesystem, and only when the probe fires. An error declared without a test
// is a known defect (rule 04), so every injected probe has its own TEST_CASE.
// Each test cleans up after itself: the TempDir fixture removes the whole tree
// on destruction, even when the test fails (best-effort teardown). File
// operations live in fs_os_test.cpp; tree operations in fs_os_tree_test.cpp.
//
// ADL note (same as error_test.cpp): infinity::core::toString would win ADL
// over doctest's toString for a CoreError operand, so no CHECK ever compares
// a CoreError directly; failsWith() isolates the comparison.
#include "infinity/core/fs.h"
#include "infinity/core/fs_os.h"
#include "infinity/core/testing/fault_injector.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <doctest/doctest.h>

namespace {

using infinity::core::CoreError;
using infinity::core::DirectoryEntry;
using infinity::core::Expected;
using infinity::core::PosixFileSystem;
using infinity::core::testing::FaultInjector;

// Callback for listDirectory fault tests. The probe fires before iteration
// starts, so this must never be invoked; keeping it a real function (instead
// of nullptr) matches the public API shape of the operation under test.
void collectEntries(const DirectoryEntry& entry, void* userData) noexcept {
    // The probe fires before iteration starts, so this must never be invoked;
    // keeping it a real function (instead of nullptr) matches the public API
    // shape of the operation under test.
    (void)entry;
    (void)userData;
}

// True when result is an error carrying expected. Isolates CoreError operands
// from CHECK so doctest never stringifies one (ADL note, see file brief).
template <typename T>
[[nodiscard]] bool failsWith(const Expected<T>& result, CoreError expected) noexcept {
    return !result.has_value() &&
           infinity::core::toString(result.error()) == infinity::core::toString(expected);
}

// Removes path and everything under it (files and directories, depth-first,
// symlinks as links). Best-effort: leftover entries after a failure are
// acceptable teardown noise. Raw POSIX on purpose: cleanup must never go
// through the backend under test (fault probes could fail mid-teardown).
void removeAll(const std::string& path) noexcept {
    struct stat status{};
    if (::lstat(path.c_str(), &status) != 0) {
        return;
    }
    if (S_ISDIR(status.st_mode)) {
        DIR* directory = ::opendir(path.c_str());
        if (directory != nullptr) {
            while (const dirent* entry = ::readdir(directory)) { // NOLINT(concurrency-mt-unsafe)
                const std::string_view name(entry->d_name);
                if (name == "." || name == "..") {
                    continue;
                }
                removeAll(path + "/" + std::string(name));
            }
            ::closedir(directory);
        }
        ::rmdir(path.c_str());
        return;
    }
    ::unlink(path.c_str());
}

// Unique temp directory under /tmp, removed (whole tree) on destruction even
// when the test fails (best-effort teardown). Never uses std::filesystem.
class TempDir {
public:
    TempDir() {
        std::string tmpl = "/tmp/infinity_fs_os_XXXXXX";
        std::vector<char> buffer(tmpl.begin(), tmpl.end());
        buffer.push_back('\0');
        if (::mkdtemp(buffer.data()) != nullptr) {
            m_path.assign(buffer.data());
        }
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    ~TempDir() {
        if (!m_path.empty()) {
            removeAll(m_path);
        }
    }

    [[nodiscard]] bool valid() const noexcept { return !m_path.empty(); }

    // Returns root + "/" + relative (relative must not be empty).
    [[nodiscard]] std::string join(std::string_view relative) const {
        return m_path + "/" + std::string(relative);
    }

private:
    std::string m_path;
};

// Test fixture: a backend wired to its own injector plus a unique temp root.
struct FsFixture {
    FaultInjector injector;
    PosixFileSystem fs{injector};
    TempDir dir;

    // Writes data to path, creating the missing ancestor directories first.
    // Returns false when any step fails.
    [[nodiscard]] bool write(std::string_view relative, std::string_view data) {
        const std::string path = dir.join(relative);
        const Expected<std::size_t> result = fs.writeFile(path, data.data(), data.size());
        return result.has_value() && *result == data.size();
    }
};

} // namespace

TEST_CASE("exists fails when the operation probe fails") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    fixture.injector.failNext("fs.exists", CoreError::IO_ERROR);
    CHECK(failsWith(fixture.fs.exists(fixture.dir.join("anything")), CoreError::IO_ERROR));
}

TEST_CASE("fileSize fails when the operation probe fails") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(fixture.write("f.txt", "x"));
    fixture.injector.failNext("fs.fileSize", CoreError::IO_ERROR);
    CHECK(failsWith(fixture.fs.fileSize(fixture.dir.join("f.txt")), CoreError::IO_ERROR));
}

TEST_CASE("readFile fails when the operation probe fails") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(fixture.write("f.txt", "x"));
    std::array<char, 4> buffer{};
    fixture.injector.failNext("fs.readFile", CoreError::IO_ERROR);
    CHECK(failsWith(fixture.fs.readFile(fixture.dir.join("f.txt"), buffer.data(), buffer.size()),
                    CoreError::IO_ERROR));
}

TEST_CASE("writeFile fails before mutating when the operation probe fails") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string path = fixture.dir.join("new.txt");
    fixture.injector.failNext("fs.writeFile", CoreError::IO_ERROR);
    const Expected<std::size_t> result = fixture.fs.writeFile(path, "data", 4);
    CHECK(failsWith(result, CoreError::IO_ERROR));
    const Expected<bool> exists = fixture.fs.exists(path);
    CHECK(exists.has_value());
    CHECK_FALSE(*exists);
}

TEST_CASE("remove fails before mutating when the operation probe fails") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string path = fixture.dir.join("keep.txt");
    CHECK(fixture.write("keep.txt", "x"));
    fixture.injector.failNext("fs.remove", CoreError::IO_ERROR);
    CHECK(failsWith(fixture.fs.remove(path), CoreError::IO_ERROR));
    const Expected<bool> exists = fixture.fs.exists(path);
    CHECK(exists.has_value());
    CHECK(*exists);
}

TEST_CASE("rename fails before mutating when the operation probe fails") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(fixture.write("source.txt", "x"));
    const std::string source = fixture.dir.join("source.txt");
    const std::string dest = fixture.dir.join("dest.txt");
    fixture.injector.failNext("fs.rename", CoreError::IO_ERROR);
    CHECK(failsWith(fixture.fs.rename(source, dest), CoreError::IO_ERROR));
    const Expected<bool> sourceExists = fixture.fs.exists(source);
    CHECK(sourceExists.has_value());
    CHECK(*sourceExists);
    const Expected<bool> destExists = fixture.fs.exists(dest);
    CHECK(destExists.has_value());
    CHECK_FALSE(*destExists);
}

TEST_CASE("makeDirectory fails before mutating when the operation probe fails") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string path = fixture.dir.join("newdir");
    fixture.injector.failNext("fs.makeDirectory", CoreError::IO_ERROR);
    CHECK(failsWith(fixture.fs.makeDirectory(path), CoreError::IO_ERROR));
    const Expected<bool> exists = fixture.fs.exists(path);
    CHECK(exists.has_value());
    CHECK_FALSE(*exists);
}

TEST_CASE("listDirectory fails when the operation probe fails") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string dirPath = fixture.dir.join("dir");
    CHECK(fixture.fs.makeDirectory(dirPath).has_value());
    fixture.injector.failNext("fs.listDirectory", CoreError::IO_ERROR);
    CHECK(
        failsWith(fixture.fs.listDirectory(dirPath, collectEntries, nullptr), CoreError::IO_ERROR));
}
