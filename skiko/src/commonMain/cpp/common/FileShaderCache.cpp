#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "include/core/SkData.h"
#include "include/core/SkString.h"
#include "FileShaderCache.h"

// std::filesystem only on Windows: Apple's runtime has it from iOS 13 / macOS 10.15, below Skiko's minimums, so the
// other platforms use POSIX.
#ifdef _WIN32
#include <filesystem>
#else
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace skiko {

namespace {

bool isDirectory(const std::string& path) {
#ifdef _WIN32
    std::error_code error;
    return std::filesystem::is_directory(std::filesystem::path(path), error);
#else
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

// The directory and its missing parents; whether it is a directory afterwards.
bool makeDirectories(const std::string& path) {
#ifdef _WIN32
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path), error);
#else
    for (size_t slash = path.find('/', 1); ; slash = path.find('/', slash + 1)) {
        std::string prefix = slash == std::string::npos ? path : path.substr(0, slash);
        if (!prefix.empty() && mkdir(prefix.c_str(), 0755) != 0 && errno != EEXIST) {
            break;
        }
        if (slash == std::string::npos) {
            break;
        }
    }
#endif
    return isDirectory(path);
}

// Puts from in to's place, replacing a file there; false if it could not.
bool replaceFile(const std::string& from, const std::string& to) {
#ifdef _WIN32
    std::error_code error;
    std::filesystem::rename(std::filesystem::path(from), std::filesystem::path(to), error);
    return !error;
#else
    return ::rename(from.c_str(), to.c_str()) == 0;
#endif
}

void removeFile(const std::string& path) {
#ifdef _WIN32
    std::error_code error;
    std::filesystem::remove(std::filesystem::path(path), error);
#else
    ::unlink(path.c_str());
#endif
}

} // namespace

static const uint32_t kMagic = 0x53434b53;  // "SKCS"
static const uint32_t kVersion = 1;

FileShaderCache* FileShaderCache::forDirectory(const char* directory) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::unique_ptr<FileShaderCache>> caches;
    std::lock_guard<std::mutex> lock(mutex);
    std::string key(directory);
    auto it = caches.find(key);
    if (it != caches.end()) {
        return it->second.get();
    }
    if (!makeDirectories(key)) {
        return nullptr;
    }
    auto cache = std::unique_ptr<FileShaderCache>(new FileShaderCache(key));
    FileShaderCache* result = cache.get();
    caches.emplace(key, std::move(cache));
    return result;
}

std::string FileShaderCache::pathFor(const SkData& key) const {
    // FNV-1a, 64 bits.
    uint64_t hash = 0xcbf29ce484222325ULL;
    const uint8_t* bytes = key.bytes();
    for (size_t i = 0; i < key.size(); i++) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ULL;
    }
    char name[40];
    snprintf(name, sizeof(name), "%016llx.sksl", static_cast<unsigned long long>(hash));
    std::string path = this->directory;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += '/';
    }
    return path + name;
}

sk_sp<SkData> FileShaderCache::load(const SkData& key) {
    this->loadCount++;
    std::string path = this->pathFor(key);
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        return nullptr;
    }
    std::unique_ptr<FILE, int (*)(FILE*)> closer(file, fclose);
    uint32_t header[3];
    if (fread(header, sizeof(header), 1, file) != 1 || header[0] != kMagic || header[1] != kVersion ||
        header[2] != key.size()) {
        return nullptr;
    }
    std::vector<uint8_t> storedKey(key.size());
    if (!storedKey.empty() && fread(storedKey.data(), storedKey.size(), 1, file) != 1) {
        return nullptr;
    }
    if (!storedKey.empty() && memcmp(storedKey.data(), key.data(), key.size()) != 0) {
        return nullptr;
    }
    uint64_t size;
    if (fread(&size, sizeof(size), 1, file) != 1 || size > (64u << 20)) {
        return nullptr;
    }
    sk_sp<SkData> data = SkData::MakeUninitialized(static_cast<size_t>(size));
    if (size > 0 && fread(data->writable_data(), static_cast<size_t>(size), 1, file) != 1) {
        return nullptr;
    }
    this->hitCount++;
    return data;
}

void FileShaderCache::store(const SkData& key, const SkData& data) {
    this->storeCount++;
    std::string path = this->pathFor(key);
    // Unique per writer, so that contexts storing the same program at once don't interleave.
    static std::atomic<uint64_t> serial{0};
    char suffix[48];
    snprintf(suffix, sizeof(suffix), ".%llx.tmp", static_cast<unsigned long long>(
        reinterpret_cast<uintptr_t>(this) ^ (serial.fetch_add(1) << 20)));
    std::string temporary = path + suffix;
    FILE* file = fopen(temporary.c_str(), "wb");
    if (!file) {
        return;
    }
    uint32_t header[3] = { kMagic, kVersion, static_cast<uint32_t>(key.size()) };
    uint64_t size = data.size();
    bool written = fwrite(header, sizeof(header), 1, file) == 1 &&
                   (key.size() == 0 || fwrite(key.data(), key.size(), 1, file) == 1) &&
                   fwrite(&size, sizeof(size), 1, file) == 1 &&
                   (data.size() == 0 || fwrite(data.data(), data.size(), 1, file) == 1);
    written = fclose(file) == 0 && written;
    if (!written || !replaceFile(temporary, path)) {
        removeFile(temporary);
    }
}

void FileShaderCache::store(const SkData& key, const SkData& data, const SkString&) {
    this->store(key, data);
}

} // namespace skiko
