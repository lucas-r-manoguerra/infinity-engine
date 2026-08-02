// tests/core/fs_os_test.cpp
//
// POSIX FileSystem backend contract tests (F3.5, ADR-023, rules 04/06/08/11):
// real round-trips against a unique temp directory (mkdtemp, no
// std::filesystem), every documented error branch per operation, deterministic
// sorted listings, UTF-8 paths, and one fault-injection case per operation (an
// error declared without a test is a known defect, rule 04). Each test cleans
// up after itself: the TempDir fixture removes the whole tree on destruction,
// even when the test fails (best-effort teardown).
//
// File operations only (rule 01, one file = one task): write/read/size/exists
// round-trips, truncation, empty files, UTF-8 paths and per-operation error
// branches. Tree operations (remove/rename/makeDirectory/listDirectory) live in
// fs_os_tree_test.cpp; probe fault injection lives in fs_os_fault_test.cpp.
//
// ADL note (same as error_test.cpp): infinity::core::toString would win ADL
// over doctest's toString for a CoreError operand, so no CHECK ever compares
// a CoreError directly; failsWith() isolates the comparison.
#include "infinity/core/fs.h"
#include "infinity/core/fs_os.h"
#include "infinity/core/testing/fault_injector.h"

#include <array>
#include <cstddef>
#include <cstdint>
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
using infinity::core::ExpectedVoid;
using infinity::core::PosixFileSystem;
using infinity::core::testing::FaultInjector;

constexpr std::string_view BAD_PATH = "bad\xFF\xFE.txt"; // not well-formed UTF-8

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

// Captures the entries a listDirectory callback receives, through the
// userData pointer, so iteration itself never allocates in the backend.
struct CollectedEntries {
    std::vector<std::pair<std::string, bool>> entries; // name, isDirectory
};

void collectEntries(const DirectoryEntry& entry, void* userData) {
    auto* collected = static_cast<CollectedEntries*>(userData);
    collected->entries.emplace_back(std::string(entry.name), entry.isDirectory);
}

} // namespace

TEST_CASE("write, stat, read and remove round-trip byte-exactly") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());

    const std::array<uint8_t, 4> bytes{0x00, 0xFF, 0x10, 0x41};
    const std::string path = fixture.dir.join("data.bin");
    const Expected<std::size_t> written = fixture.fs.writeFile(path, bytes.data(), bytes.size());
    CHECK(written.has_value());
    CHECK(*written == bytes.size());

    const Expected<bool> exists = fixture.fs.exists(path);
    CHECK(exists.has_value());
    CHECK(*exists);

    const Expected<uint64_t> size = fixture.fs.fileSize(path);
    CHECK(size.has_value());
    CHECK(*size == bytes.size());

    std::array<uint8_t, 4> buffer{};
    const Expected<std::size_t> read = fixture.fs.readFile(path, buffer.data(), buffer.size());
    CHECK(read.has_value());
    CHECK(*read == bytes.size());
    CHECK(buffer == bytes);

    CHECK(fixture.fs.remove(path).has_value());
    const Expected<bool> gone = fixture.fs.exists(path);
    CHECK(gone.has_value());
    CHECK_FALSE(*gone);
}

TEST_CASE("writeFile truncates and overwrites an existing file") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string path = fixture.dir.join("f.txt");
    CHECK(fixture.fs.writeFile(path, "hello", 5).has_value());
    const Expected<std::size_t> second = fixture.fs.writeFile(path, "hi", 2);
    CHECK(second.has_value());
    CHECK(*second == 2);

    std::array<char, 8> buffer{};
    const Expected<std::size_t> read = fixture.fs.readFile(path, buffer.data(), buffer.size());
    CHECK(read.has_value());
    CHECK(*read == 2);
    CHECK(std::string_view(buffer.data(), *read) == "hi");
}

TEST_CASE("writeFile of zero bytes creates an empty file") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string path = fixture.dir.join("empty.bin");
    const Expected<std::size_t> result = fixture.fs.writeFile(path, nullptr, 0);
    CHECK(result.has_value());
    CHECK(*result == 0);
    const Expected<uint64_t> size = fixture.fs.fileSize(path);
    CHECK(size.has_value());
    CHECK(*size == 0);
}

TEST_CASE("readFile of an empty file returns zero without touching the buffer") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string path = fixture.dir.join("empty.txt");
    CHECK(fixture.fs.writeFile(path, nullptr, 0).has_value());

    std::array<char, 8> buffer{'Q', 'Q', 'Q', 'Q', 'Q', 'Q', 'Q', 'Q'};
    const Expected<std::size_t> read = fixture.fs.readFile(path, buffer.data(), buffer.size());
    CHECK(read.has_value());
    CHECK(*read == 0);
    CHECK(buffer[0] == 'Q');
}

TEST_CASE("UTF-8 paths round-trip through write, list and read") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    constexpr std::string_view NAME = "caf\xC3\xA9.txt"; // café.txt
    const std::string path = fixture.dir.join("data");
    CHECK(fixture.fs.makeDirectory(path).has_value());
    CHECK(fixture.write("data/" + std::string(NAME), "bonjour"));

    CollectedEntries collected;
    const ExpectedVoid listed = fixture.fs.listDirectory(path, collectEntries, &collected);
    CHECK(listed.has_value());
    CHECK(collected.entries.size() == 1);
    if (collected.entries.size() != 1) {
        return;
    }
    CHECK(collected.entries[0].first == NAME);

    std::array<char, 16> buffer{};
    const Expected<std::size_t> read = fixture.fs.readFile(
        fixture.dir.join("data/" + std::string(NAME)), buffer.data(), buffer.size());
    CHECK(read.has_value());
    CHECK(std::string_view(buffer.data(), *read) == "bonjour");
}

TEST_CASE("writeFile reports IO_NOT_FOUND when the parent directory is missing") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(failsWith(fixture.fs.writeFile(fixture.dir.join("no/parent.txt"), "x", 1),
                    CoreError::IO_NOT_FOUND));
}

TEST_CASE("writeFile reports IO_WRONG_TYPE when an ancestor path component is a file") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string filePath = fixture.dir.join("file.txt");
    CHECK(fixture.fs.writeFile(filePath, "x", 1).has_value());

    CHECK(failsWith(fixture.fs.writeFile(fixture.dir.join("file.txt/child.txt"), "x", 1),
                    CoreError::IO_WRONG_TYPE));
}

TEST_CASE("fileSize reports IO_NOT_FOUND for a missing path and IO_WRONG_TYPE for a directory") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(failsWith(fixture.fs.fileSize(fixture.dir.join("missing.txt")), CoreError::IO_NOT_FOUND));

    const std::string dirPath = fixture.dir.join("dir");
    CHECK(fixture.fs.makeDirectory(dirPath).has_value());
    CHECK(failsWith(fixture.fs.fileSize(dirPath), CoreError::IO_WRONG_TYPE));
}

TEST_CASE("readFile reports IO_NOT_FOUND and IO_WRONG_TYPE") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    std::array<char, 4> buffer{};
    CHECK(failsWith(
        fixture.fs.readFile(fixture.dir.join("missing.txt"), buffer.data(), buffer.size()),
        CoreError::IO_NOT_FOUND));

    const std::string dirPath = fixture.dir.join("dir");
    CHECK(fixture.fs.makeDirectory(dirPath).has_value());
    CHECK(failsWith(fixture.fs.readFile(dirPath, buffer.data(), buffer.size()),
                    CoreError::IO_WRONG_TYPE));
}

TEST_CASE("readFile reports INVALID_SIZE when the buffer is too small and leaves it untouched") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string path = fixture.dir.join("f.txt");
    CHECK(fixture.fs.writeFile(path, "hello", 5).has_value());

    std::array<char, 3> buffer{'A', 'B', 'C'};
    CHECK(failsWith(fixture.fs.readFile(path, buffer.data(), buffer.size()),
                    CoreError::INVALID_SIZE));
    CHECK(buffer[0] == 'A');
    CHECK(buffer[1] == 'B');
    CHECK(buffer[2] == 'C');
}

TEST_CASE("operations report INVALID_UTF8 for malformed paths") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string bad = fixture.dir.join(BAD_PATH);
    std::array<char, 4> buffer{};

    CHECK(failsWith(fixture.fs.exists(bad), CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.fileSize(bad), CoreError::INVALID_UTF8));
    CHECK(
        failsWith(fixture.fs.readFile(bad, buffer.data(), buffer.size()), CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.writeFile(bad, "x", 1), CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.remove(bad), CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.rename(bad, fixture.dir.join("ok.txt")), CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.makeDirectory(bad), CoreError::INVALID_UTF8));
    CHECK(
        failsWith(fixture.fs.listDirectory(bad, collectEntries, nullptr), CoreError::INVALID_UTF8));
}

TEST_CASE("exists reports false for a missing path and true for files and directories") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const Expected<bool> missing = fixture.fs.exists(fixture.dir.join("nope.txt"));
    CHECK(missing.has_value());
    CHECK_FALSE(*missing);

    const std::string filePath = fixture.dir.join("file.txt");
    CHECK(fixture.fs.writeFile(filePath, "x", 1).has_value());
    const Expected<bool> file = fixture.fs.exists(filePath);
    CHECK(file.has_value());
    CHECK(*file);

    const std::string dirPath = fixture.dir.join("dir");
    CHECK(fixture.fs.makeDirectory(dirPath).has_value());
    const Expected<bool> dir = fixture.fs.exists(dirPath);
    CHECK(dir.has_value());
    CHECK(*dir);
}
