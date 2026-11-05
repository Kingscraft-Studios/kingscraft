#pragma once

#include <fstream>
#include <mutex>
#include <string>

#include "Bus/Mailbox.hpp"
#include "Bus/Message.hpp"
#include "Util/TimeUtil.hpp"

namespace kc {

    enum LogLevel {
        DEBUG,
        INFO,
        WARN,
        ERROR
    };

    class Logger {
    public:
        static Logger& Get() {
            static Logger instance;
            return instance;
        }

        Logger();
        ~Logger();

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        void log(LogLevel level, ThreadName sender, std::string data);

    private:
        std::mutex mutex_;
        std::string createLogFileName();
        std::string buildLogLine(LogLevel level, ThreadName caller, const std::string& data);

        void printHeader();
        void writeToFile(const std::string& log);

        const char* logLevelToString(LogLevel level) {
            switch (level) {
            case LogLevel::DEBUG:    return "DEBUG";
            case LogLevel::INFO:     return "INFO";
            case LogLevel::WARN:     return "WARN";
            case LogLevel::ERROR:    return "ERROR";
            }

            return "UNKNOWN";
        }

        const char* threadToString(ThreadName thread) {
            switch (thread) {
            case ThreadName::Unknown:     return "Unknown";
            case ThreadName::Engine:       return "Engine";
            case ThreadName::Renderer:     return "Renderer";
            case ThreadName::Registry: return "Registry";
            case ThreadName::Input: return "Input";
            case ThreadName::GameLogic: return "GameLogic";
            case ThreadName::WorkerPool: return "WorkerPool";
            case ThreadName::MessageBus: return "MessageBus";
            }

            return "Unknown";
        }

        std::ofstream logFile_;
        std::string logFilePath_;
    };

} // namespace kc