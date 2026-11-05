#include "Threads/Logger.hpp"

#include <filesystem>
#include <iostream>

#include "Util/StringUtils.hpp"
#include "Util/TimeUtil.hpp"

namespace kc {

    Logger::Logger() {
        logFilePath_ = createLogFileName();

        std::filesystem::create_directories("logs");

        logFile_.open(logFilePath_, std::ios::out | std::ios::app);

        if (!logFile_.is_open()) {
            buildLogLine(ERROR, ThreadName::Engine, "Failed to open log file: " + logFilePath_);
        }

        printHeader();
    }

    Logger::~Logger() {
        std::lock_guard lock(mutex_);

        if (logFile_.is_open()) {
            logFile_.flush();
            logFile_.close();
        }
    }

    void Logger::log(LogLevel level, ThreadName caller, std::string data) {
        std::lock_guard lock(mutex_);

        std::string line = buildLogLine(level, caller, data);

        if (level == LogLevel::ERROR || level == LogLevel::WARN) {
            std::cerr << line << std::endl;
        } else {
            std::cout << line << std::endl;
        }

        writeToFile(line);
    }

    void Logger::printHeader() {
        std::string header =
            "[DEBUG] ============================================================\n"
            "[DEBUG] Kingscraft Engine Logs Initialized\n"
            "[DEBUG] Version    : 1.0.0-PreAlpha\n"
            "[DEBUG] Build      : Debug\n"
            "[DEBUG] Platform   : Linux\n"
            "[DEBUG] Start Time : " + TimeUtil::getCurrentSystemTime(TimeFormat::Logger) +
            "\n[DEBUG] ============================================================\n";

        std::cout << header << std::endl;

        writeToFile(header);
    }

    std::string Logger::createLogFileName() {
        return "logs/" + TimeUtil::getCurrentSystemTime(TimeFormat::File) + ".log";
    }

    std::string Logger::buildLogLine(LogLevel level,
                                 ThreadName caller,
                                 const std::string& data) {
        return StringUtils::build(
            "[", TimeUtil::uptimeSeconds(), " ",
            logLevelToString(level), "]: ",
            "[", threadToString(caller), "] ",
            data
        );
    }

    void Logger::writeToFile(const std::string& log) {
        if (logFile_.is_open()) {
            logFile_ << log << std::endl;
            logFile_.flush();
        }
    }

} // namespace kc