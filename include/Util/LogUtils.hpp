#pragma once
#include "Bus/MessageBus.hpp"
#include "Threads/Logger.hpp"

namespace kc {
    class LogUtils {
    public:
        static void info(ThreadName sender, std::string data) {
            log(sender, data, LogLevel::INFO);
        }

        static void warn(ThreadName sender, std::string data) {
            log(sender, data, LogLevel::WARN);
        }

        static void error(ThreadName sender, std::string data) {
            log(sender, data, LogLevel::ERROR);
        }

        static void debug(ThreadName sender, std::string data) {
            log(sender, data, LogLevel::DEBUG);
        }

    private:
         static void log(ThreadName sender, std::string data, LogLevel level) {
            if (sender != ThreadName::Engine) {
                MessageBus::Get().send(ThreadName::Engine, [data, sender, level]() {
                    Logger::Get().log(level, sender, data);
                });
            } else {
                Logger::Get().log(level, sender, data);
            }
        }
    };
}
