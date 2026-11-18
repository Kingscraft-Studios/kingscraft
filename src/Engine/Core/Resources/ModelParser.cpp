#include "Core/Resources/ModelParser.hpp"
#include "Core/Resources/BlockModel.hpp"
#include "Bus/MessageBus.hpp"
#include "json.hpp"

#include <string>
#include <unordered_map>

namespace kc {
    ModelParser::ParsedModel ModelParser::parse(const std::vector<char>& jsonBytes) {
        ParsedModel result;

        std::string jsonStr(jsonBytes.begin(), jsonBytes.end());
        auto j = nlohmann::json::parse(jsonStr);

        std::unordered_map<std::string, int> texIndex;
        for (auto& [name, filePath] : j["textures"].items()) {
            texIndex[name] = static_cast<int>(result.texturePaths.size());
            result.texturePaths.push_back(filePath.get<std::string>());
        }

        for (auto& elem : j["elements"]) {
            ParsedElement e;
            e.from = glm::ivec3(elem["from"][0], elem["from"][1], elem["from"][2]);
            e.to   = glm::ivec3(elem["to"][0],   elem["to"][1],   elem["to"][2]);

            for (auto& [dirStr, face] : elem["faces"].items()) {
                ParsedQuad q;
                if      (dirStr == "pos_y") q.face = FaceDir::PosY;
                else if (dirStr == "neg_y") q.face = FaceDir::NegY;
                else if (dirStr == "pos_z") q.face = FaceDir::PosZ;
                else if (dirStr == "neg_z") q.face = FaceDir::NegZ;
                else if (dirStr == "pos_x") q.face = FaceDir::PosX;
                else if (dirStr == "neg_x") q.face = FaceDir::NegX;

                q.uvFrom = glm::ivec2(face["uv"][0], face["uv"][1]);
                q.uvTo   = glm::ivec2(face["uv"][2], face["uv"][3]);

                std::string texRef = face["texture"];
                if (texRef.size() > 1 && texRef[0] == '#') {
                    auto it = texIndex.find(texRef.substr(1));
                    if (it != texIndex.end()) q.texOrder = it->second;
                }

                e.quads.push_back(q);
            }

            result.elements.push_back(std::move(e));
        }

        return result;
    }

} // namespace kc
