#pragma once

#include "NSlib/Core/Types.h"
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
            ext.begin(), ext.end(), ext.begin(), [](NS::uint8 c) { return static_cast<char>(std::tolower(c)); });
        return ext == ".dds";
    }
} // namespace NS::Gfx::detail
