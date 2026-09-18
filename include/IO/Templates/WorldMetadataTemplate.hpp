#pragma once

#include "IOTemplateBase.hpp"
#include "Core/World/WorldMetadata.hpp"

#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace kc {

    // world.kcw holds the persistent world + player metadata as text directives
    // (one per line, '#'-prefixed) — the same style the region files use while
    // chunk storage is still text. Text keeps it human-debuggable; a binary
    // layout arrives with the KCF pipeline later.
    //
    //   #K <v>               worldFormatVersion (gate: refuse anything != 1)
    //   #S <seed>            generator seed
    //   #F <freq>            noise frequency
    //   #A <amplitude>       noise amplitude
    //   #H <height>          base height
    //   #O <octaves>         fractal octaves
    //   #L <lacunarity>      fractal lacunarity
    //   #N <gain>            fractal gain
    //   #G <id> <version>    generatorId generatorVersion
    //   #B <version>         blockRegistryVersion
    //   #W <ticks>           worldTime
    //   #SP <x> <y> <z>      spawn point (body coords)
    //   #PL <x> <y> <z>      player position (body coords)
    //   #PY <yaw> <pitch>    player camera facing
    //
    // Unknown directives are ignored and absent directives keep the defaults,
    // so future fields only need a new tag (no version bump) and a malformed or
    // future-version file is refused and must not be overwritten.
    class WorldMetadataTemplate final : public IOTemplateBase {
    public:
        enum class LoadResult { Ok, NotFound, Invalid };

        struct LoadOutcome {
            LoadResult result = LoadResult::NotFound;
            WorldMetadata metadata;
        };

        explicit WorldMetadataTemplate(IO& io) : IOTemplateBase(io) {}

        LoadOutcome load() {
            LoadOutcome outcome;
            if (!std::filesystem::exists(kWorldPath)) return outcome;

            auto fileData = readBytes(kWorldPath);
            if (fileData.empty()) return outcome;

            std::string text(fileData.begin(), fileData.end());
            if (text.find_first_not_of(" \t\r\n") == std::string::npos) return outcome;

            auto fail = [&outcome]() -> LoadOutcome {
                outcome.result = LoadResult::Invalid;
                return outcome;
            };

            bool versionOk = false;
            for (const auto& line : splitLines(text)) {
                std::istringstream iss(line);
                std::string tag;
                if (!(iss >> tag)) continue;

                if (tag == "#K") {
                    uint32_t v = 0;
                    if (!(iss >> v) || v != WORLD_FORMAT_VERSION) return fail();
                    versionOk = true;
                } else if (tag == "#S") {
                    if (!(iss >> outcome.metadata.settings.seed)) return fail();
                } else if (tag == "#F") {
                    if (!(iss >> outcome.metadata.settings.frequency)) return fail();
                } else if (tag == "#A") {
                    if (!(iss >> outcome.metadata.settings.amplitude)) return fail();
                } else if (tag == "#H") {
                    if (!(iss >> outcome.metadata.settings.baseHeight)) return fail();
                } else if (tag == "#O") {
                    if (!(iss >> outcome.metadata.settings.octaves)) return fail();
                } else if (tag == "#L") {
                    if (!(iss >> outcome.metadata.settings.lacunarity)) return fail();
                } else if (tag == "#N") {
                    if (!(iss >> outcome.metadata.settings.gain)) return fail();
                } else if (tag == "#G") {
                    if (!(iss >> outcome.metadata.generatorId >> outcome.metadata.generatorVersion)) return fail();
                } else if (tag == "#B") {
                    if (!(iss >> outcome.metadata.blockRegistryVersion)) return fail();
                } else if (tag == "#W") {
                    if (!(iss >> outcome.metadata.worldTime)) return fail();
                } else if (tag == "#SP") {
                    if (!(iss >> outcome.metadata.spawnPos.x >> outcome.metadata.spawnPos.y >> outcome.metadata.spawnPos.z)) return fail();
                } else if (tag == "#PL") {
                    if (!(iss >> outcome.metadata.playerPos.x >> outcome.metadata.playerPos.y >> outcome.metadata.playerPos.z)) return fail();
                } else if (tag == "#PY") {
                    if (!(iss >> outcome.metadata.yaw >> outcome.metadata.pitch)) return fail();
                }
            }

            if (!versionOk) return fail();
            outcome.result = LoadResult::Ok;
            return outcome;
        }

        // Queue a metadata write; flush() is the only place disk is touched,
        // so repeated saves collapse to one write (same batching as regions).
        void stageSave(const WorldMetadata& metadata) {
            metadata_ = metadata;
            pending_ = true;
        }

        void flush() {
            if (!pending_) return;

            std::ostringstream out;
            out << "#K " << WORLD_FORMAT_VERSION << '\n';
            out << "#S " << metadata_.settings.seed << '\n';
            out << "#F " << writeFloat(metadata_.settings.frequency) << '\n';
            out << "#A " << writeFloat(metadata_.settings.amplitude) << '\n';
            out << "#H " << writeFloat(metadata_.settings.baseHeight) << '\n';
            out << "#O " << metadata_.settings.octaves << '\n';
            out << "#L " << writeFloat(metadata_.settings.lacunarity) << '\n';
            out << "#N " << writeFloat(metadata_.settings.gain) << '\n';
            out << "#G " << metadata_.generatorId << ' ' << metadata_.generatorVersion << '\n';
            out << "#B " << metadata_.blockRegistryVersion << '\n';
            out << "#W " << metadata_.worldTime << '\n';
            out << "#SP " << writeFloat(metadata_.spawnPos.x) << ' ' << writeFloat(metadata_.spawnPos.y) << ' ' << writeFloat(metadata_.spawnPos.z) << '\n';
            out << "#PL " << writeFloat(metadata_.playerPos.x) << ' ' << writeFloat(metadata_.playerPos.y) << ' ' << writeFloat(metadata_.playerPos.z) << '\n';
            out << "#PY " << writeFloat(metadata_.yaw) << ' ' << writeFloat(metadata_.pitch) << '\n';

            std::string text = out.str();
            writeBytes(kWorldPath, std::vector<char>(text.begin(), text.end()));
            pending_ = false;
        }

    private:
        static constexpr const char* kWorldPath = "world/world.kcw";

        WorldMetadata metadata_;
        bool pending_ = false;

        static std::string writeFloat(float v) {
            std::ostringstream ss;
            ss << std::setprecision(9) << v;
            return ss.str();
        }

        static std::vector<std::string> splitLines(const std::string& text) {
            std::vector<std::string> lines;
            size_t pos = 0;
            while (pos <= text.size()) {
                size_t eol = text.find('\n', pos);
                std::string line = (eol == std::string::npos)
                    ? text.substr(pos)
                    : text.substr(pos, eol - pos);
                if (!line.empty()) lines.push_back(line);
                if (eol == std::string::npos) break;
                pos = eol + 1;
            }
            return lines;
        }
    };

} // namespace kc