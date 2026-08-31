#pragma once
#include <chrono>

namespace kc {

enum class TimeFormat{
    Logger,
    File
};
class TimeUtil {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
private:
    inline static TimePoint startTime{};
    inline static bool initialized = false;

public:
    static void Init() {
        startTime = Clock::now();
        initialized = true;
    }

    // raw current time
    static TimePoint now() {
        return Clock::now();
    }

    // uptime in seconds (float/double style for engine use)
    static double uptimeSeconds() {
        using namespace std::chrono;
        return duration<double>(now() - startTime).count();
    }

    // uptime in milliseconds
    static long long uptimeMs() {
        using namespace std::chrono;
        return duration_cast<milliseconds>(now() - startTime).count();
    }

    // uptime in seconds (fixed type if you prefer consistency)
    static float uptime() {
        using namespace std::chrono;
        return duration<float>(now() - startTime).count();
    }

    static std::string getCurrentSystemTime(TimeFormat mode) {
        auto now = std::chrono::system_clock::now();
        time_t now_time = std::chrono::system_clock::to_time_t(now);

        std::tm tm{};
        localtime_r(&now_time, &tm);

        char buffer[64];

        switch (mode) {
            case TimeFormat::Logger:
                std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
                break;
            case TimeFormat::File:
                std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", &tm);
                break;
        }

        return std::string(buffer);
    }

    static bool isInitialized() {
        return initialized;
    }
};

}
