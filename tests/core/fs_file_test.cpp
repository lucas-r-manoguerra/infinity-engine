// tests/core/fs_file_test.cpp
//
// FileSystem FILE-OPERATION contract tests (F2.8, ADR-023, rules 04/06/08/11):
// exists, fileSize, readFile and writeFile, each with happy path, edge cases
// and every documented error branch (rule 04: an error declared without a test
// is a known defect). All tests are deterministic: no sleeps, no wall clock,
// no environment - state lives in the test's own backend. The tree operations
// (remove/rename/makeDirectory/listDirectory) and the path-grammar plus
// fault-injection cases live in fs_tree_test.cpp and
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
