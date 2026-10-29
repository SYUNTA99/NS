#include "NSlib/CoreObject/Asset/EffectCatalog.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Windows/Filesystem.h"

#include <algorithm>
#include <map>

namespace NS
{
    bool IsEffectAssetId(std::string_view id) noexcept
    {
        return id.size() == 32 && std::all_of(id.begin(), id.end(), [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               });
    }

    std::vector<EffectAssetFile> ReadEffectAssetFiles(std::string_view contentRoot)
    {
        std::vector<EffectAssetFile> result;
        std::map<std::string, std::size_t> counts;
        const std::string root = OS::FileSystem::Combine(contentRoot, "Assets/Effects");
        for (const std::string& path : OS::FileSystem::ListFilesRecursive(root, ".effect"))
        {
            const std::optional<std::string> text = OS::FileSystem::ReadAllText(path);
            if (!text)
            {
                continue;
            }
            nlohmann::json document = nlohmann::json::parse(*text, nullptr, false);
            if (!document.is_object() || !document.contains("id") || !document["id"].is_string() ||
                !IsEffectAssetId(document["id"].get<std::string>()) || !document.contains("label") ||
                !document["label"].is_string())
            {
                NS_LOG_ERROR(Scene, "エフェクトの素材定義 {} の識別子か表示名が不正", path);
                continue;
            }
            const std::optional<std::string> relative = OS::FileSystem::RelativeUnder(contentRoot, path);
            if (!relative)
            {
                continue;
            }
            EffectAssetFile file;
            file.id = document["id"].get<std::string>();
            file.label = document["label"].get<std::string>();
            file.path = *relative;
            file.document = std::move(document);
            ++counts[file.id];
            result.push_back(std::move(file));
        }
        std::erase_if(result, [&](const EffectAssetFile& file) {
            if (counts[file.id] == 1)
            {
                return false;
            }
            NS_LOG_ERROR(Scene, "エフェクトの識別子 {} が重複しているため {} を使わない", file.id, file.path);
            return true;
        });
        return result;
    }
} // namespace NS
