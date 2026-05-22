#include "Logger.h"
#include <fstream>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdarg>
#include <string>
#include <filesystem>

static std::ofstream g_logFile;
static std::ofstream g_errFile;
static std::mutex g_logMutex;
static bool g_inited = false;

static std::string timeNow() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

static void writeLine(std::ofstream& f, const std::string& line) {
    if (f.is_open()) {
        f << line << "\n";
        f.flush();
    }
}

void Logger::init() {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_inited) return;

    // Default names
    std::string logName = "scan_log.txt";
    std::string errName = "error_log.txt";

    // Try to read build/options.txt for LOG_FILE/errorlog_file
    std::filesystem::path opts = std::filesystem::current_path() / "build" / "options.txt";
    if (!std::filesystem::exists(opts)) {
        // try relative path
        opts = std::filesystem::path("build") / "options.txt";
    }
    if (std::filesystem::exists(opts)) {
        std::ifstream in(opts.string());
        std::string line;
        while (std::getline(in, line)) {
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            // trim
            auto trim = [](std::string s){
                size_t a = s.find_first_not_of(" \t\r\n");
                size_t b = s.find_last_not_of(" \t\r\n");
                if (a==std::string::npos) return std::string();
                return s.substr(a, b - a + 1);
            };
            key = trim(key);
            val = trim(val);
            if (key == "LOG_FILE") logName = val;
            if (key == "errorlog_file") errName = val;
        }
    }

    g_logFile.open(logName, std::ios::app);
    g_errFile.open(errName, std::ios::app);
    g_inited = true;
}

static void vlog(std::ofstream& out, const char* fmt, va_list ap) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    std::string line = std::string("[") + timeNow() + "] " + buf;
    writeLine(out, line);
}

void Logger::info(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_inited) init();
    std::string line = std::string("[INFO] ") + msg;
    writeLine(g_logFile, std::string("") + timeNow() + " " + line);
    std::cout << line << std::endl;
}

void Logger::error(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_inited) init();
    std::string line = std::string("[ERROR] ") + msg;
    writeLine(g_errFile, std::string("") + timeNow() + " " + line);
    std::cerr << line << std::endl;
}

void Logger::debug(const std::string& msg) {
    std::lock_guard<std::mutex> lk(g_logMutex);
    if (!g_inited) init();
    std::string line = std::string("[DEBUG] ") + msg;
    writeLine(g_logFile, std::string("") + timeNow() + " " + line);
}

void Logger::infof(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char buf[1024]; vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    info(std::string(buf));
}

void Logger::errorf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char buf[1024]; vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    error(std::string(buf));
}

void Logger::debugf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char buf[1024]; vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    debug(std::string(buf));
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
