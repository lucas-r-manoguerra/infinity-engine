// tests/core/fs_test.cpp
//
// Contract tests for the FileSystem interface and its in-memory backend
// (F2.8, ADR-023, rules 04/06/08/11). Every public operation is covered with
// happy path, edge cases and every documented error branch, plus one
// fault-injection case per operation (F2.4, ADR-016: an error declared
// without a test is a known defect). All tests are deterministic: no sleeps,
// no wall clock, no environment - state lives in the test's own backend.
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

constexpr std::string_view BAD_PATH = "bad\xFF\xFE.txt"; // not well-formed UTF-8

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

TEST_CASE("exists reports true for files and directories and false for missing paths") {
    FsFixture fixture;
    CHECK(fixture.write("file.txt", "hello"));
    CHECK(createDirectories(fixture.fs, "dir"));

    const Expected<bool> fileExists = fixture.fs.exists("file.txt");
    CHECK(fileExists.has_value());
    CHECK(*fileExists);
    const Expected<bool> dirExists = fixture.fs.exists("dir");
    CHECK(dirExists.has_value());
    CHECK(*dirExists);
    const Expected<bool> missingExists = fixture.fs.exists("nope.txt");
    CHECK(missingExists.has_value());
    CHECK_FALSE(*missingExists);
}

TEST_CASE("exists reports INVALID_UTF8 for a malformed path") {
    FsFixture fixture;
    CHECK(failsWith(fixture.fs.exists(BAD_PATH), CoreError::INVALID_UTF8));
}

TEST_CASE("exists reports false for an empty path") {
    FsFixture fixture;
    const Expected<bool> result = fixture.fs.exists("");
    CHECK(result.has_value());
    CHECK_FALSE(*result);
}

TEST_CASE("fileSize returns the size of written files") {
    FsFixture fixture;
    CHECK(fixture.write("a.txt", "hello"));
    CHECK(fixture.write("empty.bin", ""));

    const Expected<uint64_t> size = fixture.fs.fileSize("a.txt");
    CHECK(size.has_value());
    CHECK(*size == 5);
    const Expected<uint64_t> empty = fixture.fs.fileSize("empty.bin");
    CHECK(empty.has_value());
    CHECK(*empty == 0);
}

TEST_CASE("fileSize reports IO_NOT_FOUND for a missing path") {
    FsFixture fixture;
    CHECK(failsWith(fixture.fs.fileSize("missing.txt"), CoreError::IO_NOT_FOUND));
}

TEST_CASE("fileSize reports IO_WRONG_TYPE for a directory") {
    FsFixture fixture;
    CHECK(createDirectories(fixture.fs, "dir"));
    CHECK(failsWith(fixture.fs.fileSize("dir"), CoreError::IO_WRONG_TYPE));
}

TEST_CASE("readFile copies the whole file into the caller buffer and returns its size") {
    FsFixture fixture;
    constexpr std::string_view CONTENTS = "hello world";
    CHECK(fixture.write("f.txt", CONTENTS));

    std::array<char, 64> buffer{};
    const Expected<std::size_t> result = fixture.fs.readFile("f.txt", buffer.data(), buffer.size());
    CHECK(result.has_value());
    CHECK(*result == CONTENTS.size());
    CHECK(std::string_view(buffer.data(), *result) == CONTENTS);
}

TEST_CASE("readFile reads binary data byte-exactly") {
    FsFixture fixture;
    const std::array<uint8_t, 4> bytes{0x00, 0xFF, 0x10, 0x41};
    const Expected<std::size_t> written =
        fixture.fs.writeFile("bin.dat", bytes.data(), bytes.size());
    CHECK(written.has_value());
    CHECK(*written == bytes.size());

    std::array<uint8_t, 4> buffer{};
    const Expected<std::size_t> result =
        fixture.fs.readFile("bin.dat", buffer.data(), buffer.size());
    CHECK(result.has_value());
    CHECK(*result == bytes.size());
    CHECK(buffer == bytes);
}

TEST_CASE("readFile reports IO_NOT_FOUND for a missing path") {
    FsFixture fixture;
    std::array<char, 4> buffer{};
    CHECK(failsWith(fixture.fs.readFile("missing.txt", buffer.data(), buffer.size()),
                    CoreError::IO_NOT_FOUND));
}

TEST_CASE("readFile reports IO_WRONG_TYPE for a directory") {
    FsFixture fixture;
    CHECK(createDirectories(fixture.fs, "dir"));
    std::array<char, 4> buffer{};
    CHECK(failsWith(fixture.fs.readFile("dir", buffer.data(), buffer.size()),
                    CoreError::IO_WRONG_TYPE));
}

TEST_CASE("readFile reports INVALID_SIZE when the buffer is too small and leaves it untouched") {
    FsFixture fixture;
    CHECK(fixture.write("f.txt", "hello"));

    std::array<char, 3> buffer{'A', 'B', 'C'};
    CHECK(failsWith(fixture.fs.readFile("f.txt", buffer.data(), buffer.size()),
                    CoreError::INVALID_SIZE));
    CHECK(buffer[0] == 'A');
    CHECK(buffer[1] == 'B');
    CHECK(buffer[2] == 'C');

    std::array<char, 1> noRoom{};
    CHECK(failsWith(fixture.fs.readFile("f.txt", noRoom.data(), 0), CoreError::INVALID_SIZE));
}

TEST_CASE("readFile of an empty file returns zero without touching the buffer") {
    FsFixture fixture;
    CHECK(fixture.write("empty.txt", ""));

    std::array<char, 8> buffer{'Q', 'Q', 'Q', 'Q', 'Q', 'Q', 'Q', 'Q'};
    const Expected<std::size_t> result =
        fixture.fs.readFile("empty.txt", buffer.data(), buffer.size());
    CHECK(result.has_value());
    CHECK(*result == 0);
    CHECK(buffer[0] == 'Q');
}

TEST_CASE("writeFile creates, truncates and overwrites files") {
    FsFixture fixture;
    const Expected<std::size_t> first = fixture.fs.writeFile("f.txt", "hello", 5);
    CHECK(first.has_value());
    CHECK(*first == 5);
    const Expected<std::size_t> second = fixture.fs.writeFile("f.txt", "hi", 2);
    CHECK(second.has_value());
    CHECK(*second == 2);

    std::array<char, 8> buffer{};
    const Expected<std::size_t> read = fixture.fs.readFile("f.txt", buffer.data(), buffer.size());
    CHECK(read.has_value());
    CHECK(*read == 2);
    CHECK(std::string_view(buffer.data(), *read) == "hi");
}

TEST_CASE("writeFile of zero bytes creates an empty file") {
    FsFixture fixture;
    const Expected<std::size_t> result = fixture.fs.writeFile("empty.bin", nullptr, 0);
    CHECK(result.has_value());
    CHECK(*result == 0);
    const Expected<uint64_t> size = fixture.fs.fileSize("empty.bin");
    CHECK(size.has_value());
    CHECK(*size == 0);
}

TEST_CASE("writeFile reports IO_NOT_FOUND when the parent directory is missing") {
    FsFixture fixture;
    CHECK(failsWith(fixture.fs.writeFile("no/parent.txt", "x", 1), CoreError::IO_NOT_FOUND));
}

TEST_CASE("writeFile reports IO_WRONG_TYPE when the path or an ancestor is a file") {
    FsFixture fixture;
    CHECK(createDirectories(fixture.fs, "dir"));
    CHECK(failsWith(fixture.fs.writeFile("dir", "x", 1), CoreError::IO_WRONG_TYPE));

    CHECK(fixture.write("file.txt", "hello"));
    CHECK(failsWith(fixture.fs.writeFile("file.txt/child.txt", "x", 1), CoreError::IO_WRONG_TYPE));
}

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

TEST_CASE("listing order is deterministic across identical workloads") {
    FsFixture first;
    FsFixture second;

    CHECK(first.write("alpha/one.txt", "1"));
    CHECK(first.write("beta/two.txt", "2"));
    CHECK(first.write("alpha/three.txt", "3"));
    CHECK(first.write("beta/four.txt", "4"));

    CHECK(second.write("beta/four.txt", "4"));
    CHECK(second.write("alpha/three.txt", "3"));
    CHECK(second.write("beta/two.txt", "2"));
    CHECK(second.write("alpha/one.txt", "1"));

    CollectedEntries firstListing;
    CHECK(first.fs.listDirectory("alpha", collectEntries, &firstListing).has_value());
    CollectedEntries secondListing;
    CHECK(second.fs.listDirectory("alpha", collectEntries, &secondListing).has_value());

    CHECK(firstListing.entries.size() == secondListing.entries.size());
    if (firstListing.entries.size() != secondListing.entries.size()) {
        return;
    }
    for (std::size_t i = 0; i < firstListing.entries.size(); ++i) {
        CHECK(firstListing.entries[i].first == secondListing.entries[i].first);
        CHECK(firstListing.entries[i].second == secondListing.entries[i].second);
    }
    CHECK(firstListing.entries[0].first == "one.txt");
    CHECK(firstListing.entries[1].first == "three.txt");
}

TEST_CASE("UTF-8 paths round-trip through write, list and read") {
    FsFixture fixture;
    constexpr std::string_view PATH = "data/caf\xC3\xA9.txt"; // data/café.txt
    CHECK(fixture.write(PATH, "bonjour"));

    CollectedEntries collected;
    const ExpectedVoid listed = fixture.fs.listDirectory("data", collectEntries, &collected);
    CHECK(listed.has_value());
    CHECK(collected.entries.size() == 1);
    if (collected.entries.size() != 1) {
        return;
    }
    CHECK(collected.entries[0].first == PATH.substr(PATH.find('/') + 1));

    std::array<char, 16> buffer{};
    const Expected<std::size_t> read = fixture.fs.readFile(PATH, buffer.data(), buffer.size());
    CHECK(read.has_value());
    CHECK(std::string_view(buffer.data(), *read) == "bonjour");
}

TEST_CASE("the in-memory path grammar rejects malformed component sequences") {
    FsFixture fixture;
    CHECK(failsWith(fixture.fs.exists("/leading"), CoreError::IO_INVALID_DATA));
    CHECK(failsWith(fixture.fs.exists("trailing/"), CoreError::IO_INVALID_DATA));
    CHECK(failsWith(fixture.fs.exists("double//slash"), CoreError::IO_INVALID_DATA));
    CHECK(failsWith(fixture.fs.exists("dot/./component"), CoreError::IO_INVALID_DATA));
    CHECK(failsWith(fixture.fs.exists("../parent"), CoreError::IO_INVALID_DATA));
}

TEST_CASE("an empty path names no entry for every operation") {
    FsFixture fixture;
    const Expected<bool> exists = fixture.fs.exists("");
    CHECK(exists.has_value());
    CHECK_FALSE(*exists);

    std::array<char, 4> buffer{};
    CHECK(failsWith(fixture.fs.fileSize(""), CoreError::IO_NOT_FOUND));
    CHECK(
        failsWith(fixture.fs.readFile("", buffer.data(), buffer.size()), CoreError::IO_NOT_FOUND));
    CHECK(failsWith(fixture.fs.writeFile("", buffer.data(), 0), CoreError::IO_NOT_FOUND));
    CHECK(failsWith(fixture.fs.remove(""), CoreError::IO_NOT_FOUND));
    CHECK(failsWith(fixture.fs.rename("", "x.txt"), CoreError::IO_NOT_FOUND));
    CHECK(failsWith(fixture.fs.rename("x.txt", ""), CoreError::IO_NOT_FOUND));
    CHECK(failsWith(fixture.fs.makeDirectory(""), CoreError::IO_NOT_FOUND));
    CHECK(
        failsWith(fixture.fs.listDirectory("", collectEntries, nullptr), CoreError::IO_NOT_FOUND));
}

TEST_CASE("every path-taking operation reports INVALID_UTF8 for a malformed path") {
    FsFixture fixture;
    std::array<char, 8> buffer{};
    CHECK(failsWith(fixture.fs.exists(BAD_PATH), CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.fileSize(BAD_PATH), CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.readFile(BAD_PATH, buffer.data(), buffer.size()),
                    CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.writeFile(BAD_PATH, buffer.data(), 0), CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.remove(BAD_PATH), CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.rename(BAD_PATH, "ok.txt"), CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.rename("ok.txt", BAD_PATH), CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.makeDirectory(BAD_PATH), CoreError::INVALID_UTF8));
    CHECK(failsWith(fixture.fs.listDirectory(BAD_PATH, collectEntries, nullptr),
                    CoreError::INVALID_UTF8));
}

TEST_CASE("exists fails when the operation probe fails") {
    FsFixture fixture;
    fixture.injector.failNext("fs.exists", CoreError::IO_ERROR);
    CHECK(failsWith(fixture.fs.exists("anything"), CoreError::IO_ERROR));
}

TEST_CASE("fileSize fails when the operation probe fails") {
    FsFixture fixture;
    CHECK(fixture.write("f.txt", "x"));
    fixture.injector.failNext("fs.fileSize", CoreError::IO_ERROR);
    CHECK(failsWith(fixture.fs.fileSize("f.txt"), CoreError::IO_ERROR));
}

TEST_CASE("readFile fails when the operation probe fails") {
    FsFixture fixture;
    CHECK(fixture.write("f.txt", "x"));
    std::array<char, 4> buffer{};
    fixture.injector.failNext("fs.readFile", CoreError::IO_ERROR);
    CHECK(
        failsWith(fixture.fs.readFile("f.txt", buffer.data(), buffer.size()), CoreError::IO_ERROR));
}

TEST_CASE("writeFile fails before mutating when the operation probe fails") {
    FsFixture fixture;
    fixture.injector.failNext("fs.writeFile", CoreError::IO_ERROR);
    const Expected<std::size_t> result = fixture.fs.writeFile("new.txt", "data", 4);
    CHECK(failsWith(result, CoreError::IO_ERROR));
    const Expected<bool> exists = fixture.fs.exists("new.txt");
    CHECK(exists.has_value());
    CHECK_FALSE(*exists);
}

TEST_CASE("remove fails before mutating when the operation probe fails") {
    FsFixture fixture;
    CHECK(fixture.write("keep.txt", "x"));
    fixture.injector.failNext("fs.remove", CoreError::IO_ERROR);
    CHECK(failsWith(fixture.fs.remove("keep.txt"), CoreError::IO_ERROR));
    const Expected<bool> exists = fixture.fs.exists("keep.txt");
    CHECK(exists.has_value());
    CHECK(*exists);
}

TEST_CASE("rename fails before mutating when the operation probe fails") {
    FsFixture fixture;
    CHECK(fixture.write("source.txt", "x"));
    fixture.injector.failNext("fs.rename", CoreError::IO_ERROR);
    CHECK(failsWith(fixture.fs.rename("source.txt", "dest.txt"), CoreError::IO_ERROR));
    const Expected<bool> source = fixture.fs.exists("source.txt");
    CHECK(source.has_value());
    CHECK(*source);
    const Expected<bool> dest = fixture.fs.exists("dest.txt");
    CHECK(dest.has_value());
    CHECK_FALSE(*dest);
}

TEST_CASE("makeDirectory fails before mutating when the operation probe fails") {
    FsFixture fixture;
    fixture.injector.failNext("fs.makeDirectory", CoreError::IO_ERROR);
    CHECK(failsWith(fixture.fs.makeDirectory("newdir"), CoreError::IO_ERROR));
    const Expected<bool> exists = fixture.fs.exists("newdir");
    CHECK(exists.has_value());
    CHECK_FALSE(*exists);
}

TEST_CASE("listDirectory fails when the operation probe fails") {
    FsFixture fixture;
    CHECK(createDirectories(fixture.fs, "dir"));
    fixture.injector.failNext("fs.listDirectory", CoreError::IO_ERROR);
    CHECK(failsWith(fixture.fs.listDirectory("dir", collectEntries, nullptr), CoreError::IO_ERROR));
}

TEST_CASE("an injected IO_PERMISSION_DENIED propagates verbatim through the backend") {
    FsFixture fixture;
    CHECK(fixture.write("f.txt", "x"));
    fixture.injector.failNext("fs.readFile", CoreError::IO_PERMISSION_DENIED);
    std::array<char, 4> buffer{};
    CHECK(failsWith(fixture.fs.readFile("f.txt", buffer.data(), buffer.size()),
                    CoreError::IO_PERMISSION_DENIED));
}

TEST_CASE("clear resets the tree to a fresh empty state") {
    FsFixture fixture;
    CHECK(fixture.write("a/b.txt", "x"));
    CHECK(createDirectories(fixture.fs, "dir"));
    fixture.fs.clear();

    const Expected<bool> gone = fixture.fs.exists("a");
    CHECK(gone.has_value());
    CHECK_FALSE(*gone);
    const Expected<bool> dirGone = fixture.fs.exists("dir");
    CHECK(dirGone.has_value());
    CHECK_FALSE(*dirGone);

    CHECK(fixture.write("new.txt", "y"));
    const Expected<bool> present = fixture.fs.exists("new.txt");
    CHECK(present.has_value());
    CHECK(*present);
}
