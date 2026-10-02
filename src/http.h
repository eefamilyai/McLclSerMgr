#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace http {

struct Response {
    int status = 0;
    std::string body;
    std::string error;
    bool ok() const { return status >= 200 && status < 300; }
};

Response get(const std::string& url, int timeoutMs = 20000);

using Progress = std::function<void(uint64_t done, uint64_t total)>;
// Downloads to dest (via dest + ".part"). Returns false and fills err on failure/cancel.
bool download(const std::string& url, const std::filesystem::path& dest, const Progress& progress,
              const std::atomic<bool>* cancel, std::string& err);

}  // namespace http
