#pragma once

#include "NSlib/Windows/Filesystem.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace NS::Gfx::detail
{
    [[nodiscard]] inline bool IsDdsExtension(std::string_view path)
    {
        std::string ext = NS::OS::FileSystem::Extension(path);
        std::transform(
            ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".dds";
    }
} // namespace NS::Gfx::detail
