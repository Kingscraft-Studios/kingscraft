#include "Threads/Logger.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "Util/StringUtils.hpp"
#include "Util/TimeUtil.hpp"

namespace kc {

    Logger::Logger() {
        logFilePath_ = createLogFileName();

        std::filesystem::create_directories("logs");

        logFile_.open(logFilePath_, std::ios::out | std::ios::app);

        if (!logFile_.is_open()) {
            log(ERROR, ThreadName::Engine, "Core", "Failed to open log file: " + logFilePath_);
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

    void Logger::log(LogLevel level, ThreadName caller, std::string_view category, std::string data) {
        std::lock_guard lock(mutex_);

        std::string line = buildLogLine(level, caller, category, data);

        if (level == LogLevel::ERROR || level == LogLevel::WARN) {
            std::cerr << line << std::endl;
        } else {
            std::cout << line << std::endl;
        }

        writeToFile(line);
    }

    void Logger::printHeader() {
        const std::string lines[] = {
            "============================================================",
            "Kingscraft Engine Logs Initialized",
            "Version    : 1.0.0-PreAlpha",
            "Build      : Debug",
            "Platform   : Linux",
            "Start Time : " + TimeUtil::getCurrentSystemTime(TimeFormat::Logger),
            "============================================================"
        };

        for (const auto& line : lines) {
            const std::string header = buildLogLine(DEBUG, ThreadName::Engine, "Core", line);
            std::cout << header << std::endl;
            writeToFile(header);
        }
    }

    std::string Logger::createLogFileName() {
        return "logs/" + TimeUtil::getCurrentSystemTime(TimeFormat::File) + ".log";
    }

    std::string Logger::buildLogLine(LogLevel level,
                                 ThreadName caller,
                                 std::string_view category,
                                 const std::string& data) {
        std::ostringstream uptime;
        uptime << std::fixed << std::setprecision(3) << TimeUtil::uptimeSeconds();

        return StringUtils::build(
            "[", uptime.str(), "/",
            logLevelToString(level), "] [",
            threadToString(caller), "/", category, "] ",
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