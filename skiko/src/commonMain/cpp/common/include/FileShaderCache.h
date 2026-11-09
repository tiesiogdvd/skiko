#pragma once
#include <atomic>
#include <string>
#include "include/gpu/ganesh/GrContextOptions.h"

namespace skiko {

// Ganesh's persistent shader cache in a directory: one file a program, named by a hash of its key
// and holding the key (to tell apart keys of the same hash), written to a temporary file and
// renamed into place. One cache a directory for the process's life (a context's options point at
// it). Ganesh calls it on its context's thread; several contexts may share one directory.
class FileShaderCache : public GrContextOptions::PersistentCache {
public:
    static FileShaderCache* forDirectory(const char* directory);

    sk_sp<SkData> load(const SkData& key) override;
    void store(const SkData& key, const SkData& data) override;
    void store(const SkData& key, const SkData& data, const SkString& description) override;

    int loads() const { return this->loadCount.load(); }
    int hits() const { return this->hitCount.load(); }
    int stores() const { return this->storeCount.load(); }

private:
    explicit FileShaderCache(std::string directory) : directory(std::move(directory)) {}
    std::string pathFor(const SkData& key) const;

    const std::string directory;
    std::atomic<int> loadCount{0};
    std::atomic<int> hitCount{0};
    std::atomic<int> storeCount{0};
};

} // namespace skiko
