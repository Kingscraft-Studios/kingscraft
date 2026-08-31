#pragma once
#include "Bus/MessageBus.hpp"
#include "Threads/Logger.hpp"

namespace kc {
    class LogUtils {
    public:
        static void info(ThreadName sender, std::string data) {
            if (sender != ThreadName::Engine) {
                MessageBus::Get().send(ThreadName::Engine, [data, sender]() {
                    Logger::Get().log(LogLevel::INFO, sender, data);
                });
            } else {
                Logger::Get().log(LogLevel::INFO, sender, data);
            }
        }

        static void warn(ThreadName sender, std::string data) {
            if (sender != ThreadName::Engine) {
                MessageBus::Get().send(ThreadName::Engine, [data, sender]() {
                    Logger::Get().log(LogLevel::WARN, sender, data);
                });
            } else {
                Logger::Get().log(LogLevel::WARN, sender, data);
            }
        }

        static void error(ThreadName sender, std::string data) {
            if (sender != ThreadName::Engine) {
                MessageBus::Get().send(ThreadName::Engine, [data, sender]() {
                    Logger::Get().log(LogLevel::ERROR, sender, data);
                });
            } else {
                Logger::Get().log(LogLevel::ERROR, sender, data);
            }
        }

        static void debug(ThreadName sender, std::string data) {
            if (sender != ThreadName::Engine) {
                MessageBus::Get().send(ThreadName::Engine, [data, sender]() {
                    Logger::Get().log(LogLevel::DEBUG, sender, data);
                });
            } else {
                Logger::Get().log(LogLevel::DEBUG, sender, data);
            }
        }
    };
}
