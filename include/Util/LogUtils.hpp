#pragma once
#include <string>
#include <string_view>

#include "Bus/MessageBus.hpp"
#include "Threads/Logger.hpp"

namespace kc {
    class LogUtils {
    public:
        static void info(ThreadName sender, std::string_view category, std::string data) {
            log(sender, category, data, LogLevel::INFO);
        }

        static void warn(ThreadName sender, std::string_view category, std::string data) {
            log(sender, category, data, LogLevel::WARN);
        }

        static void error(ThreadName sender, std::string_view category, std::string data) {
            log(sender, category, data, LogLevel::ERROR);
        }

        static void debug(ThreadName sender, std::string_view category, std::string data) {
            log(sender, category, data, LogLevel::DEBUG);
        }

    private:
         static void log(ThreadName sender, std::string_view category, std::string data, LogLevel level) {
            if (sender != ThreadName::Engine) {
                MessageBus::Get().send(ThreadName::Engine, [data, sender, category = std::string(category), level]() {
                    Logger::Get().log(level, sender, category, data);
                });
            } else {
                Logger::Get().log(level, sender, category, data);
            }
        }
    };
}
