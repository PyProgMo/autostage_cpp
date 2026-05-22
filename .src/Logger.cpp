#include "Logger.h"

#include <iostream>

// AppLogger implementation (single-process logger used across the project)
AppLogger& AppLogger::instance() {
    static AppLogger logger;
    return logger;
}

void AppLogger::setPaths(const std::string& generalPath, const std::string& errorPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    generalPath_ = generalPath;
    errorPath_ = errorPath;
    opened_ = false;
    if (generalFile_.is_open()) {
        generalFile_.close();
    }
    if (errorFile_.is_open()) {
        errorFile_.close();
    }
}

void AppLogger::info(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensureOpenLocked();
    writeLocked(generalFile_, "INFO", message);
    std::cout << message << "\n";
}

void AppLogger::error(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensureOpenLocked();
    writeLocked(generalFile_, "ERROR", message);
    writeLocked(errorFile_, "ERROR", message);
    std::cerr << message << "\n";
}

void AppLogger::writeLocked(std::ofstream& file, const std::string& level, const std::string& message) {
    if (file.is_open()) {
        file << timestamp() << " [" << level << "] " << message << '\n';
        file.flush();
    }
}

void AppLogger::ensureOpenLocked() {
    if (opened_) {
        return;
    }

    if (!generalFile_.is_open()) {
        generalFile_.open(generalPath_.c_str(), std::ios::out | std::ios::app);
    }
    if (!errorFile_.is_open()) {
        errorFile_.open(errorPath_.c_str(), std::ios::out | std::ios::app);
    }
    opened_ = true;
}

std::string AppLogger::timestamp() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto nowTime = clock::to_time_t(now);

    std::tm tm = {};
#if defined(_WIN32)
    localtime_s(&tm, &nowTime);
#else
    localtime_r(&nowTime, &tm);
#endif

    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << millis.count();
    return oss.str();
}
// Logger.cpp
#include "Logger.h"

#include <iostream>

AppLogger& AppLogger::instance() {
    static AppLogger logger;
    return logger;
}

void AppLogger::setPaths(const std::string& generalPath, const std::string& errorPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    generalPath_ = generalPath;
    errorPath_ = errorPath;
    opened_ = false;
    if (generalFile_.is_open()) {
        generalFile_.close();
    }
    if (errorFile_.is_open()) {
        errorFile_.close();
    }
}

void AppLogger::info(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensureOpenLocked();
    writeLocked(generalFile_, "INFO", message);
    std::cout << message << "\n";
}

void AppLogger::error(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    ensureOpenLocked();
    writeLocked(generalFile_, "ERROR", message);
    writeLocked(errorFile_, "ERROR", message);
    std::cerr << message << "\n";
}

void AppLogger::writeLocked(std::ofstream& file, const std::string& level, const std::string& message) {
    if (file.is_open()) {
        file << timestamp() << " [" << level << "] " << message << '\n';
        file.flush();
    }
}

void AppLogger::ensureOpenLocked() {
    if (opened_) {
        return;
    }

    if (!generalFile_.is_open()) {
        generalFile_.open(generalPath_.c_str(), std::ios::out | std::ios::app);
    }
    if (!errorFile_.is_open()) {
        errorFile_.open(errorPath_.c_str(), std::ios::out | std::ios::app);
    }
    opened_ = true;
}

std::string AppLogger::timestamp() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto nowTime = clock::to_time_t(now);

    std::tm tm = {};
#if defined(_WIN32)
    localtime_s(&tm, &nowTime);
#else
    localtime_r(&nowTime, &tm);
#endif

    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << millis.count();
    return oss.str();
}
