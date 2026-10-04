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
    //   #K <v>               worldFormatVersion (gate: refuse anything != WORLD_FORMAT_VERSION)
    //   #S <seed>            generator seed
    //   #SL <y>              sea level (vanilla 63)
    //   #RB <f>              extra rolling hills, in blocks (default 0 = vanilla)
    //   #LP <f>              terrain dial: inland valley floor   (default 0.01)
    //   #LH <f>              terrain dial: inland gentle middle (default 0.03)
    //   #LT <f>              terrain dial: inland high ground   (default 0.10)
    //   #G <id> <version>    generatorId generatorVersion
    //   #B <version>         blockRegistryVersion
    //   #W <ticks>           worldTime
    //   #SP <x> <y> <z>      spawn point (body coords)
    //   #PL <x> <y> <z>      player position (body coords)
    //   #PY <yaw> <pitch>    camera facing
    //
    // The #RB/#LP/#LH/#LT terrain dials are the whole terrain customisation
    // surface and are safe to hand-edit: they are read into settings on load and
    // written back verbatim, because World only ever overwrites worldTime and
    // the player/spawn fields, never settings. See TerrainGenSettings.hpp.
    //
    // Height shaping used to live on Biomes and was never persisted; the legacy
    // #A / #H tags are still ignored on read. Unknown directives are ignored and
    // absent directives keep the defaults, so future fields only need a new tag
    // (no version bump) and a malformed or future-version file is refused and
    // must not be overwritten.
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
            if (!std::filesystem::exists(worldPath)) return outcome;

            auto fileData = readBytes(worldPath);
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
                } else if (tag == "#SL") {
                    // Optional: worlds written before the vanilla pipeline have no
                    // #SL line, and keeping the default is the right reading for
                    // them (the old generator hardcoded its own water level).
                    if (!(iss >> outcome.metadata.settings.seaLevel)) return fail();
                } else if (tag == "#RB") {
                    // Terrain dials are optional for the same reason #SL is: a
                    // world saved before the dials existed keeps vanilla terrain.
                    if (!(iss >> outcome.metadata.settings.shape.reliefBlocks)) return fail();
                } else if (tag == "#LP") {
                    if (!(iss >> outcome.metadata.settings.shape.lowlandPlain)) return fail();
                } else if (tag == "#LH") {
                    if (!(iss >> outcome.metadata.settings.shape.lowlandHill)) return fail();
                } else if (tag == "#LT") {
                    if (!(iss >> outcome.metadata.settings.shape.lowlandTall)) return fail();
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
            // A hand-edited dial must not be able to invert the spline knots.
            outcome.metadata.settings.shape.clampToValidRanges();
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

            // Write back the values actually in use, so a hand-edited dial that
            // had to be clamped (see clampToValidRanges) reads back correctly.
            TerrainShape shape = metadata_.settings.shape;
            shape.clampToValidRanges();

            std::ostringstream out;
            out << "#K " << WORLD_FORMAT_VERSION << '\n';
            out << "#S " << metadata_.settings.seed << '\n';
            out << "#SL " << metadata_.settings.seaLevel << '\n';
            out << "#RB " << writeFloat(shape.reliefBlocks) << '\n';
            out << "#LP " << writeFloat(shape.lowlandPlain) << '\n';
            out << "#LH " << writeFloat(shape.lowlandHill) << '\n';
            out << "#LT " << writeFloat(shape.lowlandTall) << '\n';
            out << "#G " << metadata_.generatorId << ' ' << metadata_.generatorVersion << '\n';
            out << "#B " << metadata_.blockRegistryVersion << '\n';
            out << "#W " << metadata_.worldTime << '\n';
            out << "#SP " << writeFloat(metadata_.spawnPos.x) << ' ' << writeFloat(metadata_.spawnPos.y) << ' ' << writeFloat(metadata_.spawnPos.z) << '\n';
            out << "#PL " << writeFloat(metadata_.playerPos.x) << ' ' << writeFloat(metadata_.playerPos.y) << ' ' << writeFloat(metadata_.playerPos.z) << '\n';
            out << "#PY " << writeFloat(metadata_.yaw) << ' ' << writeFloat(metadata_.pitch) << '\n';

            std::string text = out.str();
            writeBytes(worldPath, std::vector<char>(text.begin(), text.end()));
            pending_ = false;
        }

    private:
        static constexpr const char* worldPath = "world/world.kcw";

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