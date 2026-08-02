// tests/core/fs_tree_test.cpp
//
// FileSystem TREE-OPERATION contract tests (F2.8, ADR-023, rules 04/06/08/11):
// remove, rename, makeDirectory and listDirectory, each with happy path, edge
// cases and every documented error branch (rule 04: an error declared without
// a test is a known defect). All tests are deterministic: no sleeps, no wall
// clock, no environment - state lives in the test's own backend. The file
// operations (exists/fileSize/readFile/writeFile) and the path-grammar plus
// fault-injection cases live in fs_file_test.cpp and
// fs_path_and_fault_test.cpp (rule 01: One File = One Task).
//
// ADL note (same as error_test.cpp): infinity::core::toString would win ADL
// over doctest's toString for a CoreError operand, so no CHECK ever compares
// a CoreError directly; failsWith() isolates the comparison.
#include "infinity/core/fs.h"
#include "infinity/core/fs_in_memory.h"
#include "infinity/core/testing/fault_injector.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

namespace {

using infinity::core::CoreError;
using infinity::core::DirectoryEntry;
using infinity::core::Expected;
using infinity::core::ExpectedVoid;
using infinity::core::InMemoryFileSystem;
using infinity::core::testing::FaultInjector;

// True when result is an error carrying expected. Isolates CoreError operands
// from CHECK so doctest never stringifies one (ADL note, see file brief).
template <typename T>
[[nodiscard]] bool failsWith(const Expected<T>& result, CoreError expected) noexcept {
    return !result.has_value() &&
           infinity::core::toString(result.error()) == infinity::core::toString(expected);
}

// Creates path and every missing ancestor as directories (an existing
// directory is fine). Returns false on any failure other than "already a
// directory".
[[nodiscard]] bool createDirectories(InMemoryFileSystem& fs, std::string_view path) {
    std::size_t end = path.find('/');
    while (end != std::string_view::npos) {
        const ExpectedVoid result = fs.makeDirectory(path.substr(0, end));
        if (!result.has_value() && result.error() != CoreError::IO_ALREADY_EXISTS) {
            return false;
        }
        end = path.find('/', end + 1);
    }
    const ExpectedVoid finalResult = fs.makeDirectory(path);
    return finalResult.has_value() || finalResult.error() == CoreError::IO_ALREADY_EXISTS;
}

// Test fixture: a backend wired to its own injector, plus a helper that writes
// a file after creating the missing ancestors.
struct FsFixture {
    FaultInjector injector;
    InMemoryFileSystem fs{injector};

    // Writes data to path, creating the ancestor directories first. Returns
    // false when any step fails.
    [[nodiscard]] bool write(std::string_view path, std::string_view data) {
        const std::size_t lastSlash = path.rfind('/');
        if (lastSlash != std::string_view::npos &&
            !createDirectories(fs, path.substr(0, lastSlash))) {
            return false;
        }
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

TEST_CASE("remove deletes files and empty directories") {
    FsFixture fixture;
    CHECK(fixture.write("file.txt", "x"));
    CHECK(createDirectories(fixture.fs, "dir"));

    CHECK(fixture.fs.remove("file.txt").has_value());
    const Expected<bool> fileGone = fixture.fs.exists("file.txt");
    CHECK(fileGone.has_value());
    CHECK_FALSE(*fileGone);

    CHECK(fixture.fs.remove("dir").has_value());
    const Expected<bool> dirGone = fixture.fs.exists("dir");
    CHECK(dirGone.has_value());
    CHECK_FALSE(*dirGone);
}

TEST_CASE("remove reports IO_INVALID_DATA for a non-empty directory") {
    FsFixture fixture;
    CHECK(fixture.write("dir/a.txt", "x"));
    CHECK(failsWith(fixture.fs.remove("dir"), CoreError::IO_INVALID_DATA));
    const Expected<bool> stillThere = fixture.fs.exists("dir");
    CHECK(stillThere.has_value());
    CHECK(*stillThere);
}

TEST_CASE("remove reports IO_NOT_FOUND for a missing path") {
    FsFixture fixture;
    CHECK(failsWith(fixture.fs.remove("missing"), CoreError::IO_NOT_FOUND));
}

TEST_CASE("rename moves a file between directories") {
    FsFixture fixture;
    CHECK(fixture.write("src/a.txt", "hello"));
    CHECK(createDirectories(fixture.fs, "dst"));

    const ExpectedVoid result = fixture.fs.rename("src/a.txt", "dst/b.txt");
    CHECK(result.has_value());
    const Expected<bool> sourceGone = fixture.fs.exists("src/a.txt");
    CHECK(sourceGone.has_value());
    CHECK_FALSE(*sourceGone);

    std::array<char, 8> buffer{};
    const Expected<std::size_t> read =
        fixture.fs.readFile("dst/b.txt", buffer.data(), buffer.size());
    CHECK(read.has_value());
    CHECK(*read == 5);
    CHECK(std::string_view(buffer.data(), *read) == "hello");
}

TEST_CASE("rename moves a directory with its whole subtree") {
    FsFixture fixture;
    CHECK(fixture.write("a/b/c.txt", "c"));
    CHECK(fixture.write("a/d.txt", "d"));

    const ExpectedVoid result = fixture.fs.rename("a", "moved");
    CHECK(result.has_value());
    const Expected<bool> oldGone = fixture.fs.exists("a");
    CHECK(oldGone.has_value());
    CHECK_FALSE(*oldGone);

    std::array<char, 8> buffer{};
    const Expected<std::size_t> readC =
        fixture.fs.readFile("moved/b/c.txt", buffer.data(), buffer.size());
    CHECK(readC.has_value());
    CHECK(*readC == 1);
    CHECK(buffer[0] == 'c');
    const Expected<std::size_t> readD =
        fixture.fs.readFile("moved/d.txt", buffer.data(), buffer.size());
    CHECK(readD.has_value());
    CHECK(*readD == 1);
    CHECK(buffer[0] == 'd');
}

TEST_CASE("rename reports IO_ALREADY_EXISTS when the destination exists") {
    FsFixture fixture;
    CHECK(fixture.write("src.txt", "s"));
    CHECK(fixture.write("dst.txt", "d"));

    CHECK(failsWith(fixture.fs.rename("src.txt", "dst.txt"), CoreError::IO_ALREADY_EXISTS));
    const Expected<bool> source = fixture.fs.exists("src.txt");
    CHECK(source.has_value());
    CHECK(*source);
    const Expected<bool> dest = fixture.fs.exists("dst.txt");
    CHECK(dest.has_value());
    CHECK(*dest);

    CHECK(failsWith(fixture.fs.rename("src.txt", "src.txt"), CoreError::IO_ALREADY_EXISTS));
}

TEST_CASE("rename reports IO_NOT_FOUND for a missing source or destination parent") {
    FsFixture fixture;
    CHECK(fixture.write("src.txt", "s"));
    CHECK(failsWith(fixture.fs.rename("missing.txt", "x.txt"), CoreError::IO_NOT_FOUND));
    CHECK(failsWith(fixture.fs.rename("src.txt", "no/parent/x.txt"), CoreError::IO_NOT_FOUND));
}

TEST_CASE("rename reports IO_INVALID_DATA when moving a directory into its own subtree") {
    FsFixture fixture;
    CHECK(fixture.write("a/sub/keep.txt", "x"));
    CHECK(failsWith(fixture.fs.rename("a", "a/sub/moved"), CoreError::IO_INVALID_DATA));
    const Expected<bool> stillThere = fixture.fs.exists("a/sub/keep.txt");
    CHECK(stillThere.has_value());
    CHECK(*stillThere);
}

TEST_CASE("makeDirectory creates nested directories") {
    FsFixture fixture;
    CHECK(fixture.fs.makeDirectory("level1").has_value());
    CHECK(fixture.fs.makeDirectory("level1/level2").has_value());
    const Expected<bool> exists = fixture.fs.exists("level1/level2");
    CHECK(exists.has_value());
    CHECK(*exists);
}

TEST_CASE("makeDirectory reports IO_ALREADY_EXISTS when the path exists") {
    FsFixture fixture;
    CHECK(fixture.fs.makeDirectory("dir").has_value());
    CHECK(failsWith(fixture.fs.makeDirectory("dir"), CoreError::IO_ALREADY_EXISTS));
    CHECK(fixture.write("file.txt", "x"));
    CHECK(failsWith(fixture.fs.makeDirectory("file.txt"), CoreError::IO_ALREADY_EXISTS));
}

TEST_CASE("makeDirectory reports IO_NOT_FOUND when a parent is missing") {
    FsFixture fixture;
    CHECK(failsWith(fixture.fs.makeDirectory("no/parent"), CoreError::IO_NOT_FOUND));
}

TEST_CASE("makeDirectory reports IO_WRONG_TYPE when an ancestor is a file") {
    FsFixture fixture;
    CHECK(fixture.write("file.txt", "x"));
    CHECK(failsWith(fixture.fs.makeDirectory("file.txt/child"), CoreError::IO_WRONG_TYPE));
}

TEST_CASE("listDirectory yields each direct entry exactly once with type flags") {
    FsFixture fixture;
    CHECK(fixture.write("dir/a.txt", "a"));
    CHECK(fixture.write("dir/b.txt", "b"));
    CHECK(createDirectories(fixture.fs, "dir/sub"));
    CHECK(fixture.write("dir/sub/deep.txt", "deep"));

    CollectedEntries collected;
    const ExpectedVoid result = fixture.fs.listDirectory("dir", collectEntries, &collected);
    CHECK(result.has_value());
    CHECK(collected.entries.size() == 3);
    if (collected.entries.size() != 3) {
        return;
    }
    CHECK(collected.entries[0].first == "a.txt");
    CHECK_FALSE(collected.entries[0].second);
    CHECK(collected.entries[1].first == "b.txt");
    CHECK_FALSE(collected.entries[1].second);
    CHECK(collected.entries[2].first == "sub");
    CHECK(collected.entries[2].second);
}

TEST_CASE("listDirectory of an empty directory invokes the callback zero times") {
    FsFixture fixture;
    CHECK(createDirectories(fixture.fs, "empty"));
    CollectedEntries collected;
    const ExpectedVoid result = fixture.fs.listDirectory("empty", collectEntries, &collected);
    CHECK(result.has_value());
    CHECK(collected.entries.empty());
}

TEST_CASE("listDirectory reports IO_NOT_FOUND for a missing path") {
    FsFixture fixture;
    CHECK(failsWith(fixture.fs.listDirectory("missing", collectEntries, nullptr),
                    CoreError::IO_NOT_FOUND));
}

TEST_CASE("listDirectory reports IO_WRONG_TYPE for a file path") {
    FsFixture fixture;
    CHECK(fixture.write("file.txt", "x"));
    CHECK(failsWith(fixture.fs.listDirectory("file.txt", collectEntries, nullptr),
                    CoreError::IO_WRONG_TYPE));
}
