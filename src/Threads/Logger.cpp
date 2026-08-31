#include "Threads/Logger.hpp"

#include <iostream>

#include "Threads/IO.hpp"
#include "Util/StringBuilder.hpp"
#include "Util/TimeUtil.hpp"

namespace kc {

    std::unique_ptr<Logger> Logger::instance_ = nullptr;

    void Logger::Init() {
        instance_ = std::make_unique<Logger>();
        instance_->logFilePath_ = instance_->createLogFileName();
        instance_->printHeader();
    }

    void Logger::Shutdown() {
        if (!instance_) return;
        instance_.reset();
    }

    Logger& Logger::Get() {
        return *instance_;
    }

    void Logger::log(LogLevel level, ThreadName caller, std::string data) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::string line = buildLogLine(level, caller, data);

        if (level == LogLevel::ERROR || level == LogLevel::WARN) {
            std::cerr << line << std::endl;
        } else {
            std::cout << line << std::endl;
        }

        IO::Get().writeLogFile(logFilePath_, line);
    }

    std::string Logger::createLogFileName() {
        return "logs/" + TimeUtil::getCurrentSystemTime(TimeFormat::File) + ".log";
    }

    std::string Logger::buildLogLine(LogLevel level,
                                 ThreadName caller,
                                 const std::string& data) {
        return StringBuilder::build(
            "[", TimeUtil::uptimeSeconds(), " ",
            logLevelToString(level), "]: ",
            "[", threadToString(caller), "] ",
            data
        );
    }
} // namespace kc
