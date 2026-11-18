#pragma once
#include "IOTemplateBase.hpp"

#include <string>
#include <vector>

#include "Util/TimeUtil.hpp"

namespace kc {

    // Persists profiling reports next to the executable. The newest capture is
    // always profiler/latest.txt; every older capture is rotated to
    // profiler/<date>_<time>.txt when the next one is written.
    class ProfilerTemplate final : public IOTemplateBase {
    public:
        explicit ProfilerTemplate(DiskLayer& disk) : IOTemplateBase(disk) {}

        bool saveReport(const std::string& report) {
            const std::string latest = path("latest.txt");

            if (disk_.exists(latest)) {
                const std::string timestamp = TimeUtil::getCurrentSystemTime(TimeFormat::File);
                const std::string archived = archivedPath(latest, timestamp);
                disk_.rename(latest, archived);
            }

            return writeBytes(latest, std::vector<char>(report.begin(), report.end()));
        }

        std::string latestPath() const { return path("latest.txt"); }

    private:
        static std::string path(const std::string& filename) {
            return DiskLayer::executableDirectory() + "/" + DIRECTORY + "/" + filename;
        }

        std::string archivedPath(const std::string& latest, const std::string& fallbackTimestamp) const {
            std::string timestamp = fallbackTimestamp;
            if (auto modified = disk_.lastWriteTime(latest))
                timestamp = TimeUtil::format(*modified, TimeFormat::File);

            std::string candidate = path(timestamp + ".txt");
            if (!disk_.exists(candidate)) return candidate;

            for (int suffix = 1; ; ++suffix) {
                candidate = path(timestamp + "-" + std::to_string(suffix) + ".txt");
                if (!disk_.exists(candidate)) return candidate;
            }
        }

        static constexpr const char* DIRECTORY = "profiler";
    };
}
