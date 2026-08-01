// infinity/core/src/fs_in_memory.cpp
//
// Implementation of the deterministic in-memory filesystem (F2.8, ADR-023,
// rules 04/06/11). Every operation probes the injected FaultInjector first
// (ADR-016), then validates the path, then acts; an injected failure masks the
// operation entirely and leaves the tree untouched. State lives only in the
// object's m_entries map (rule 11: no mutable globals, no environment).
#include "infinity/core/fs_in_memory.h"
#include "infinity/core/testing/fault_injector.h"
#include "infinity/core/utf8.h"

#include <cassert>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace infinity::core {

InMemoryFileSystem::InMemoryFileSystem(infinity::core::testing::FaultInjector& injector) noexcept
    : m_injector(&injector) {}

Expected<bool> InMemoryFileSystem::exists(std::string_view path) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.exists");
    if (!probe.has_value()) {
        return std::unexpected(probe.error());
    }
    const Expected<std::string> key = normalize(path);
    if (!key.has_value()) {
        if (key.error() == CoreError::IO_NOT_FOUND) {
            return false; // an empty path names no entry
        }
        return std::unexpected(key.error());
    }
    return find(*key) != nullptr;
}

Expected<uint64_t> InMemoryFileSystem::fileSize(std::string_view path) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.fileSize");
    if (!probe.has_value()) {
        return std::unexpected(probe.error());
    }
    const Expected<std::string> key = normalize(path);
    if (!key.has_value()) {
        return std::unexpected(key.error());
    }
    const Entry* entry = find(*key);
    if (entry == nullptr) {
        return std::unexpected(CoreError::IO_NOT_FOUND);
    }
    if (entry->isDirectory) {
        return std::unexpected(CoreError::IO_WRONG_TYPE);
    }
    return static_cast<uint64_t>(entry->contents.size());
}

Expected<std::size_t> InMemoryFileSystem::readFile(std::string_view path, void* buffer,
                                                   std::size_t capacity) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.readFile");
    if (!probe.has_value()) {
        return std::unexpected(probe.error());
    }
    const Expected<std::string> key = normalize(path);
    if (!key.has_value()) {
        return std::unexpected(key.error());
    }
    const Entry* entry = find(*key);
    if (entry == nullptr) {
        return std::unexpected(CoreError::IO_NOT_FOUND);
    }
    if (entry->isDirectory) {
        return std::unexpected(CoreError::IO_WRONG_TYPE);
    }
    const std::size_t size = entry->contents.size();
    if (capacity < size) {
        return std::unexpected(CoreError::INVALID_SIZE);
    }
    if (size == 0) {
        return 0;
    }
    assert(buffer != nullptr);
    std::memcpy(buffer, entry->contents.data(), size);
    return size;
}

Expected<std::size_t> InMemoryFileSystem::writeFile(std::string_view path, const void* data,
                                                    std::size_t size) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.writeFile");
    if (!probe.has_value()) {
        return std::unexpected(probe.error());
    }
    const Expected<std::string> key = normalize(path);
    if (!key.has_value()) {
        return std::unexpected(key.error());
    }
    const Expected<std::string> parent = requireParentDirectory(*key);
    if (!parent.has_value()) {
        return std::unexpected(parent.error());
    }

    if (size > 0) {
        assert(data != nullptr);
    }
    const auto existingIt = m_entries.find(*key);
    if (existingIt != m_entries.end()) {
        if (existingIt->second.isDirectory) {
            return std::unexpected(CoreError::IO_WRONG_TYPE);
        }
        if (size == 0) {
            existingIt->second.contents.clear();
        } else {
            const auto* bytes = static_cast<const uint8_t*>(data);
            existingIt->second.contents.assign(bytes, bytes + size);
        }
    } else if (size == 0) {
        m_entries.emplace(*key, Entry{.isDirectory = false, .contents = {}});
    } else {
        const auto* bytes = static_cast<const uint8_t*>(data);
        m_entries.emplace(*key, Entry{.isDirectory = false,
                                      .contents = std::vector<uint8_t>(bytes, bytes + size)});
    }
    return size;
}

ExpectedVoid InMemoryFileSystem::remove(std::string_view path) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.remove");
    if (!probe.has_value()) {
        return probe;
    }
    const Expected<std::string> key = normalize(path);
    if (!key.has_value()) {
        return std::unexpected(key.error());
    }
    const Entry* entry = find(*key);
    if (entry == nullptr) {
        return std::unexpected(CoreError::IO_NOT_FOUND);
    }
    if (entry->isDirectory) {
        const std::string prefix = *key + "/";
        for (const auto& pair : m_entries) {
            if (pair.first.starts_with(prefix)) {
                return std::unexpected(CoreError::IO_INVALID_DATA); // not empty
            }
        }
    }
    m_entries.erase(*key);
    return {};
}

ExpectedVoid InMemoryFileSystem::rename(std::string_view from, std::string_view to) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.rename");
    if (!probe.has_value()) {
        return probe;
    }
    const Expected<std::string> fromKey = normalize(from);
    if (!fromKey.has_value()) {
        return std::unexpected(fromKey.error());
    }
    const Expected<std::string> toKey = normalize(to);
    if (!toKey.has_value()) {
        return std::unexpected(toKey.error());
    }
    if (*fromKey == *toKey) {
        return std::unexpected(CoreError::IO_ALREADY_EXISTS); // the destination exists
    }
    const Entry* source = find(*fromKey);
    if (source == nullptr) {
        return std::unexpected(CoreError::IO_NOT_FOUND);
    }
    const Expected<std::string> parent = requireParentDirectory(*toKey);
    if (!parent.has_value()) {
        return std::unexpected(parent.error());
    }
    if (find(*toKey) != nullptr) {
        return std::unexpected(CoreError::IO_ALREADY_EXISTS);
    }
    if (source->isDirectory && toKey->starts_with(*fromKey + "/")) {
        return std::unexpected(CoreError::IO_INVALID_DATA); // into its own subtree
    }

    if (!source->isDirectory) {
        auto node = m_entries.extract(*fromKey);
        node.key() = *toKey;
        m_entries.insert(std::move(node));
        return {};
    }

    const std::string fromPrefix = *fromKey + "/";
    std::vector<std::pair<std::string, Entry>> moved;
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        const std::string& entryKey = it->first;
        if (entryKey == *fromKey || entryKey.starts_with(fromPrefix)) {
            moved.emplace_back(*toKey + entryKey.substr(fromKey->size()), std::move(it->second));
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& pair : moved) {
        m_entries.emplace(std::move(pair.first), std::move(pair.second));
    }
    return {};
}

ExpectedVoid InMemoryFileSystem::makeDirectory(std::string_view path) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.makeDirectory");
    if (!probe.has_value()) {
        return probe;
    }
    const Expected<std::string> key = normalize(path);
    if (!key.has_value()) {
        return std::unexpected(key.error());
    }
    const Expected<std::string> parent = requireParentDirectory(*key);
    if (!parent.has_value()) {
        return std::unexpected(parent.error());
    }
    if (find(*key) != nullptr) {
        return std::unexpected(CoreError::IO_ALREADY_EXISTS);
    }
    m_entries.emplace(*key, Entry{.isDirectory = true, .contents = {}});
    return {};
}

ExpectedVoid InMemoryFileSystem::listDirectory(std::string_view path, DirectoryCallback callback,
                                               void* userData) noexcept {
    const ExpectedVoid probe = m_injector->probe("fs.listDirectory");
    if (!probe.has_value()) {
        return probe;
    }
    const Expected<std::string> key = normalize(path);
    if (!key.has_value()) {
        return std::unexpected(key.error());
    }
    const Entry* dir = find(*key);
    if (dir == nullptr) {
        return std::unexpected(CoreError::IO_NOT_FOUND);
    }
    if (!dir->isDirectory) {
        return std::unexpected(CoreError::IO_WRONG_TYPE);
    }
    assert(callback != nullptr);

    const std::string prefix = *key + "/";
    for (const auto& pair : m_entries) {
        const std::string& entryKey = pair.first;
        if (!entryKey.starts_with(prefix)) {
            continue; // not under this directory
        }
        const std::string_view child = std::string_view(entryKey).substr(prefix.size());
        if (child.find('/') != std::string_view::npos) {
            continue; // not a direct child
        }
        const DirectoryEntry listed{.name = child, .isDirectory = pair.second.isDirectory};
        callback(listed, userData);
    }
    return {};
}

void InMemoryFileSystem::clear() noexcept { m_entries.clear(); }

Expected<std::string> InMemoryFileSystem::normalize(std::string_view path) noexcept {
    if (path.empty()) {
        return std::unexpected(CoreError::IO_NOT_FOUND);
    }
    if (!isValidUtf8(path)) {
        return std::unexpected(CoreError::INVALID_UTF8);
    }
    if (path.front() == '/' || path.back() == '/') {
        return std::unexpected(CoreError::IO_INVALID_DATA);
    }

    std::string normalized;
    normalized.reserve(path.size());
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::size_t componentLength =
            (end == std::string_view::npos ? path.size() : end) - start;
        const std::string_view component = path.substr(start, componentLength);
        if (component.empty() || component == "." || component == "..") {
            return std::unexpected(CoreError::IO_INVALID_DATA);
        }
        if (!normalized.empty()) {
            normalized.push_back('/');
        }
        normalized.append(component);
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return normalized;
}

const InMemoryFileSystem::Entry* InMemoryFileSystem::find(const std::string& key) const noexcept {
    const auto it = m_entries.find(key);
    return it == m_entries.end() ? nullptr : &it->second;
}

Expected<std::string>
InMemoryFileSystem::requireParentDirectory(const std::string& key) const noexcept {
    const std::size_t slash = key.find_last_of('/');
    if (slash == std::string::npos) {
        return std::string{}; // top-level: the parent is the virtual root
    }
    std::string parentKey = key.substr(0, slash);
    const Entry* parent = find(parentKey);
    if (parent == nullptr) {
        return std::unexpected(CoreError::IO_NOT_FOUND);
    }
    if (!parent->isDirectory) {
        return std::unexpected(CoreError::IO_WRONG_TYPE);
    }
    return parentKey;
}

} // namespace infinity::core
