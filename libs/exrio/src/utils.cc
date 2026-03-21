#include <exrio/utils.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace exrio {

bool g_verbose = false;

void setVerbose(bool verbose) { g_verbose = verbose; }

auto isVerbose() -> bool { return g_verbose; }

static void LogVerbose(const std::string& message) {
    if (g_verbose) {
        std::cout << message << std::endl;
    }
}

static void Log(const std::string& message) { std::cout << message << std::endl; }

static void LogError(const std::string& message) { std::cerr << "Error: " << message << std::endl; }

Timer::Timer() { reset(); }

void Timer::reset() { start_ = std::chrono::high_resolution_clock::now(); }

double Timer::elapsedMs() {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(now - start_).count();
}

std::string Timer::ElapsedString() const {
    double const ms = elapsedMs();
    std::ostringstream oss;

    if (ms < 1000.0) {
        oss << std::fixed << std::setprecision(1) << ms << " ms";
    } else {
        oss << std::fixed << std::setprecision(2) << (ms / 1000.0) << " s";
    }

    return oss.str();
}

static std::string FormatNumber(size_t number) {
    std::string num_str = std::to_string(number);
    std::string result;

    int count = 0;
    for (auto it = numStr.rbegin(); it != numStr.rend(); ++it) {
        if (count > 0 && count % 3 == 0) {
            result = ',' + result;
        }
        result = *it + result;
        count++;
    }

    return result;
}

static std::string FormatBytes(size_t bytes) {
    std::ostringstream oss;

    if (bytes < 1024) {
        oss << bytes << " B";
    } else if (bytes < 1024 * 1024) {
        oss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
    } else if (bytes < 1024 * 1024 * 1024) {
        oss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024.0)) << " MB";
    } else {
        oss << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    }

    return oss.str();
}

static std::string GetFilename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

static std::string GetDirectory(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return ".";
    }
    return path.substr(0, pos);
}

static void EnsureDirectoryExists(const std::string& filepath) {
    std::string dir = getDirectory(filepath);
    if (dir == "." || dir.empty()) {
        return;
    }

    try {
        std::filesystem::create_directories(dir);
    } catch (const std::exception& e) {
        logError("Failed to create directory " + dir + ": " + e.what());
    }
}

static auto FileExists(const std::string& path) -> bool {
    std::ifstream f(path);
    return f.good();
}

}  // namespace exrio
