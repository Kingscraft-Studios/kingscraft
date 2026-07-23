#include "Core/Resources/ModelParser.hpp"
#include "Core/Resources/BlockModel.hpp"
#include "Bus/MessageBus.hpp"
#include "Threads/IO.hpp"
#include "json.hpp"
#include "stb_image.h"

#include <string>
#include <unordered_map>

namespace lve {

void ModelParser::loadAsync(const std::string& path,
                            std::function<void(BlockModel)> callback) {
    MessageBus::Get().request<BlockModel>(ThreadName::Resource, [path]() -> BlockModel {
            auto data = IO::Get().readFile(path);
            std::string jsonStr(data.begin(), data.end());
            auto j = nlohmann::json::parse(jsonStr);

            std::unordered_map<std::string, std::string> texFiles;
            for (auto& [name, filePath] : j["textures"].items()) {
                texFiles[name] = filePath;
            }

            std::vector<TextureData> textures;
            std::unordered_map<std::string, int> texIndex;
            for (auto& [name, filePath] : texFiles) {
                auto raw = IO::Get().readFile(filePath);
                if (!raw.empty()) {
                    TextureData tex;
                    tex.rawData.assign(raw.begin(), raw.end());
                    int w = 0, h = 0, ch = 0;
                    stbi_info_from_memory(
                        reinterpret_cast<const stbi_uc*>(tex.rawData.data()),
                        static_cast<int>(tex.rawData.size()), &w, &h, &ch);
                    tex.width = w;
                    tex.height = h;
                    texIndex[name] = static_cast<int>(textures.size());
                    textures.push_back(std::move(tex));
                }
            }

            std::vector<Element> elements;
            for (auto& elem : j["elements"]) {
                Element e;
                e.from = glm::ivec3(elem["from"][0], elem["from"][1], elem["from"][2]);
                e.to   = glm::ivec3(elem["to"][0],   elem["to"][1],   elem["to"][2]);

                for (auto& [dirStr, face] : elem["faces"].items()) {
                    Quad q;
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
                        if (it != texIndex.end()) q.tileIndex = it->second;
                    }

                    e.quads.push_back(q);
                }
                elements.push_back(std::move(e));
            }
            return BlockModel(std::move(elements), std::move(textures));
        },
        ThreadName::Registration,
        [callback = std::move(callback)](BlockModel model) mutable {
            if (callback) callback(std::move(model));
        }
    );
}

} // namespace lve
