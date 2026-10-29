#pragma once

#include "NSlib/CoreObject/Reflection/ReflectionJson.h"

#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    struct EffectAssetFile
    {
        std::string id;
        std::string label;
        std::string path;
        nlohmann::json document;
    };

    // 重複した識別子は両方を除外し、ファイルの並びで参照先を決めない。
    [[nodiscard]] std::vector<EffectAssetFile> ReadEffectAssetFiles(std::string_view contentRoot);
    [[nodiscard]] bool IsEffectAssetId(std::string_view id) noexcept;
} // namespace NS
