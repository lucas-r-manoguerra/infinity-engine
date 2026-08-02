// tests/core/fs_os_tree_test.cpp
//
// POSIX FileSystem backend contract tests (F3.5, ADR-023, rules 04/06/08/11):
// tree operations only (rule 01, one file = one task): remove, rename,
// makeDirectory and listDirectory, against a unique temp directory (mkdtemp,
// no std::filesystem), covering every documented error branch. Each test
// cleans up after itself: the TempDir fixture removes the whole tree on
// destruction, even when the test fails (best-effort teardown). File
// operations live in fs_os_test.cpp; probe fault injection lives in
// fs_os_fault_test.cpp.
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
using infinity::core::ExpectedVoid;
using infinity::core::PosixFileSystem;
using infinity::core::testing::FaultInjector;

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

// Creates path (absolute, under the fixture root) and every missing ancestor
// as directories. Returns false on any failure other than "already a
// directory".
[[nodiscard]] bool createDirectories(PosixFileSystem& fs, const std::string& path) {
    std::size_t end = path.find('/', 1);
    while (end != std::string::npos) {
        const ExpectedVoid result = fs.makeDirectory(path.substr(0, end));
        if (!result.has_value() && result.error() != CoreError::IO_ALREADY_EXISTS) {
            return false;
        }
        end = path.find('/', end + 1);
    }
    const ExpectedVoid finalResult = fs.makeDirectory(path);
    return finalResult.has_value() || finalResult.error() == CoreError::IO_ALREADY_EXISTS;
}

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

TEST_CASE("remove deletes files and empty directories and reports IO_NOT_FOUND when missing") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string filePath = fixture.dir.join("file.txt");
    CHECK(fixture.fs.writeFile(filePath, "x", 1).has_value());
    CHECK(fixture.fs.remove(filePath).has_value());
    const Expected<bool> fileGone = fixture.fs.exists(filePath);
    CHECK(fileGone.has_value());
    CHECK_FALSE(*fileGone);

    const std::string dirPath = fixture.dir.join("empty");
    CHECK(fixture.fs.makeDirectory(dirPath).has_value());
    CHECK(fixture.fs.remove(dirPath).has_value());
    const Expected<bool> dirGone = fixture.fs.exists(dirPath);
    CHECK(dirGone.has_value());
    CHECK_FALSE(*dirGone);

    CHECK(failsWith(fixture.fs.remove(fixture.dir.join("missing")), CoreError::IO_NOT_FOUND));
}

TEST_CASE("remove reports IO_INVALID_DATA for a non-empty directory") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(createDirectories(fixture.fs, fixture.dir.join("dir")));
    CHECK(fixture.write("dir/a.txt", "x"));
    CHECK(failsWith(fixture.fs.remove(fixture.dir.join("dir")), CoreError::IO_INVALID_DATA));
    const Expected<bool> stillThere = fixture.fs.exists(fixture.dir.join("dir"));
    CHECK(stillThere.has_value());
    CHECK(*stillThere);
}

TEST_CASE("rename moves a file between directories without overwriting") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(createDirectories(fixture.fs, fixture.dir.join("src")));
    CHECK(fixture.write("src/a.txt", "hello"));
    const std::string dstDir = fixture.dir.join("dst");
    CHECK(fixture.fs.makeDirectory(dstDir).has_value());

    const ExpectedVoid moved =
        fixture.fs.rename(fixture.dir.join("src/a.txt"), fixture.dir.join("dst/b.txt"));
    CHECK(moved.has_value());
    const Expected<bool> sourceGone = fixture.fs.exists(fixture.dir.join("src/a.txt"));
    CHECK(sourceGone.has_value());
    CHECK_FALSE(*sourceGone);

    std::array<char, 8> buffer{};
    const Expected<std::size_t> read =
        fixture.fs.readFile(fixture.dir.join("dst/b.txt"), buffer.data(), buffer.size());
    CHECK(read.has_value());
    CHECK(*read == 5);
    CHECK(std::string_view(buffer.data(), *read) == "hello");
}

TEST_CASE("rename moves a directory with its whole subtree") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(createDirectories(fixture.fs, fixture.dir.join("a/b")));
    CHECK(fixture.write("a/b/c.txt", "c"));
    CHECK(fixture.write("a/d.txt", "d"));

    const ExpectedVoid moved = fixture.fs.rename(fixture.dir.join("a"), fixture.dir.join("moved"));
    CHECK(moved.has_value());
    const Expected<bool> oldGone = fixture.fs.exists(fixture.dir.join("a"));
    CHECK(oldGone.has_value());
    CHECK_FALSE(*oldGone);

    std::array<char, 8> buffer{};
    const Expected<std::size_t> readC =
        fixture.fs.readFile(fixture.dir.join("moved/b/c.txt"), buffer.data(), buffer.size());
    CHECK(readC.has_value());
    CHECK(*readC == 1);
    CHECK(buffer[0] == 'c');
    const Expected<std::size_t> readD =
        fixture.fs.readFile(fixture.dir.join("moved/d.txt"), buffer.data(), buffer.size());
    CHECK(readD.has_value());
    CHECK(*readD == 1);
    CHECK(buffer[0] == 'd');
}

TEST_CASE("rename reports IO_ALREADY_EXISTS when the destination exists") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(fixture.write("src.txt", "s"));
    CHECK(fixture.write("dst.txt", "d"));

    CHECK(failsWith(fixture.fs.rename(fixture.dir.join("src.txt"), fixture.dir.join("dst.txt")),
                    CoreError::IO_ALREADY_EXISTS));
    const Expected<bool> source = fixture.fs.exists(fixture.dir.join("src.txt"));
    CHECK(source.has_value());
    CHECK(*source);
    const Expected<bool> dest = fixture.fs.exists(fixture.dir.join("dst.txt"));
    CHECK(dest.has_value());
    CHECK(*dest);

    const std::string same = fixture.dir.join("src.txt");
    CHECK(failsWith(fixture.fs.rename(same, same), CoreError::IO_ALREADY_EXISTS));
}

TEST_CASE("rename reports IO_NOT_FOUND for a missing source or destination parent") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(fixture.write("src.txt", "s"));
    CHECK(failsWith(fixture.fs.rename(fixture.dir.join("missing.txt"), fixture.dir.join("x.txt")),
                    CoreError::IO_NOT_FOUND));
    CHECK(failsWith(
        fixture.fs.rename(fixture.dir.join("src.txt"), fixture.dir.join("no/parent/x.txt")),
        CoreError::IO_NOT_FOUND));
}

TEST_CASE("rename reports IO_INVALID_DATA when moving a directory into its own subtree") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(createDirectories(fixture.fs, fixture.dir.join("a/sub")));
    CHECK(fixture.write("a/sub/keep.txt", "x"));

    CHECK(failsWith(fixture.fs.rename(fixture.dir.join("a"), fixture.dir.join("a/sub/moved")),
                    CoreError::IO_INVALID_DATA));
    const Expected<bool> stillThere = fixture.fs.exists(fixture.dir.join("a/sub/keep.txt"));
    CHECK(stillThere.has_value());
    CHECK(*stillThere);
}

TEST_CASE(
    "makeDirectory creates nested directories and reports IO_ALREADY_EXISTS when they exist") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string level1 = fixture.dir.join("level1");
    const std::string level2 = fixture.dir.join("level1/level2");
    CHECK(fixture.fs.makeDirectory(level1).has_value());
    CHECK(fixture.fs.makeDirectory(level2).has_value());
    const Expected<bool> exists = fixture.fs.exists(level2);
    CHECK(exists.has_value());
    CHECK(*exists);

    CHECK(failsWith(fixture.fs.makeDirectory(level1), CoreError::IO_ALREADY_EXISTS));

    const std::string filePath = fixture.dir.join("file.txt");
    CHECK(fixture.fs.writeFile(filePath, "x", 1).has_value());
    CHECK(failsWith(fixture.fs.makeDirectory(filePath), CoreError::IO_ALREADY_EXISTS));
}

TEST_CASE("makeDirectory reports IO_NOT_FOUND for a missing parent and IO_WRONG_TYPE for a file "
          "ancestor") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(failsWith(fixture.fs.makeDirectory(fixture.dir.join("no/parent")),
                    CoreError::IO_NOT_FOUND));

    const std::string filePath = fixture.dir.join("file.txt");
    CHECK(fixture.fs.writeFile(filePath, "x", 1).has_value());
    CHECK(failsWith(fixture.fs.makeDirectory(fixture.dir.join("file.txt/child")),
                    CoreError::IO_WRONG_TYPE));
}

TEST_CASE("listDirectory yields each direct entry sorted with type flags") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(fixture.fs.makeDirectory(fixture.dir.join("dir")).has_value());
    CHECK(fixture.write("dir/z.txt", "z"));
    CHECK(fixture.write("dir/a.txt", "a"));
    CHECK(createDirectories(fixture.fs, fixture.dir.join("dir/mid")));
    CHECK(fixture.write("dir/mid/deep.txt", "deep"));

    CollectedEntries collected;
    const ExpectedVoid result =
        fixture.fs.listDirectory(fixture.dir.join("dir"), collectEntries, &collected);
    CHECK(result.has_value());
    CHECK(collected.entries.size() == 3);
    if (collected.entries.size() != 3) {
        return;
    }
    CHECK(collected.entries[0].first == "a.txt");
    CHECK_FALSE(collected.entries[0].second);
    CHECK(collected.entries[1].first == "mid");
    CHECK(collected.entries[1].second);
    CHECK(collected.entries[2].first == "z.txt");
    CHECK_FALSE(collected.entries[2].second);
}

TEST_CASE("listDirectory of an empty directory invokes the callback zero times") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    const std::string dirPath = fixture.dir.join("empty");
    CHECK(fixture.fs.makeDirectory(dirPath).has_value());
    CollectedEntries collected;
    const ExpectedVoid result = fixture.fs.listDirectory(dirPath, collectEntries, &collected);
    CHECK(result.has_value());
    CHECK(collected.entries.empty());
}

TEST_CASE("listDirectory reports IO_NOT_FOUND for a missing path and IO_WRONG_TYPE for a file") {
    FsFixture fixture;
    CHECK(fixture.dir.valid());
    CHECK(failsWith(fixture.fs.listDirectory(fixture.dir.join("missing"), collectEntries, nullptr),
                    CoreError::IO_NOT_FOUND));

    const std::string filePath = fixture.dir.join("file.txt");
    CHECK(fixture.fs.writeFile(filePath, "x", 1).has_value());
    CHECK(failsWith(fixture.fs.listDirectory(filePath, collectEntries, nullptr),
                    CoreError::IO_WRONG_TYPE));
}
