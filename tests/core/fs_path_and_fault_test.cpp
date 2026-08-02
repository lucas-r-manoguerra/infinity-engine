// tests/core/fs_path_and_fault_test.cpp
//
// FileSystem PATH-GRAMMAR and FAULT-INJECTION contract tests (F2.8, ADR-023,
// F2.4, ADR-016, rules 04/06/08/11): deterministic listing order, UTF-8 path
// round-trips, the in-memory path grammar, the empty-path and INVALID_UTF8
// contracts across every path-taking operation, plus one fault-injection case
// per operation (an error declared without a test is a known defect) and the
// clear() reset. All tests are deterministic: no sleeps, no wall clock, no
// environment - state lives in the test's own backend. The file operations and
// tree operations live in fs_file_test.cpp and fs_tree_test.cpp (rule 01: One
// File = One Task).
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
