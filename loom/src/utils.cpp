#include "utils.h"
#include <fstream>
#include <algorithm>

namespace deep_compositor {

bool g_verbose = false;

void setVerbose(bool verbose) {
    g_verbose = verbose;
}

bool isVerbose() {
    return g_verbose;
}

void logVerbose(const std::string& message) {
    if (g_verbose) {
        std::cout << message << std::endl;
    }
}

void log(const std::string& message) {
    std::cout << message << std::endl;
}

void logError(const std::string& message) {
    std::cerr << "Error: " << message << std::endl;
}

Timer::Timer() {
    reset();
}

void Timer::reset() {
    start_ = std::chrono::high_resolution_clock::now();
}

double Timer::elapsedMs() const {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double, std::milli>(now - start_).count();
}

std::string Timer::elapsedString() const {
    double ms = elapsedMs();
    std::ostringstream oss;
    
    if (ms < 1000.0) {
        oss << std::fixed << std::setprecision(1) << ms << " ms";
    } else {
        oss << std::fixed << std::setprecision(2) << (ms / 1000.0) << " s";
    }
    
    return oss.str();
}

std::string formatNumber(size_t number) {
    std::string numStr = std::to_string(number);
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

std::string formatBytes(size_t bytes) {
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

std::string getFilename(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::string getDirectory(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return ".";
    }
    return path.substr(0, pos);
}

bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

} // namespace deep_compositor
