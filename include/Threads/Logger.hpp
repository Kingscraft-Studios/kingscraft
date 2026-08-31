#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <functional>
#include <iostream>

#include "IO.hpp"
#include "Bus/Mailbox.hpp"
#include "Bus/Message.hpp"
#include "Util/StringBuilder.hpp"
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

        static void Init();
        static void Shutdown();
        static Logger& Get();

        void log(LogLevel level, ThreadName sender, std::string data);

    private:
        std::mutex mutex_;
        std::string createLogFileName();
        std::string buildLogLine(LogLevel level, ThreadName caller, const std::string& data);

        void printHeader() {
            std::string header =
                "[DEBUG] ============================================================\n"
                "[DEBUG] Kingscraft Engine Logs Initialized\n"
                "[DEBUG] Version    : 1.0.0-PreAlpha\n"
                "[DEBUG] Build      : Debug\n"
                "[DEBUG] Platform   : Linux\n"
                "[DEBUG] Start Time : " + TimeUtil::getCurrentSystemTime(TimeFormat::Logger) +
                "\n[DEBUG] ============================================================\n";

            std::cout << header << std::endl;

            IO::Get().writeLogFile(logFilePath_, header);

        }

        const char* logLevelToString(LogLevel level) {
            switch (level) {
                case LogLevel::DEBUG:    return "DEBUG";
                case LogLevel::INFO:     return "INFO";
                case LogLevel::WARN:  return "WARN";
                case LogLevel::ERROR:    return "ERROR";
            }

            return "UNKNOWN";
        }

        const char* threadToString(ThreadName thread) {
            switch (thread) {
                case ThreadName::Engine:       return "Engine";
                case ThreadName::Renderer:     return "Renderer";
                case ThreadName::Resource:     return "Resource";
                case ThreadName::Registration: return "Registration";
                case ThreadName::Input: return "Input";
                case ThreadName::GameLogic: return "GameLogic";
            }

            return "Unknown";
        }

        std::string logFilePath_;

        static std::unique_ptr<Logger> instance_;
    };

} // namespace kc
