#pragma once

#include "NSlib/Core/Types.h"
#include "NSlib/Core/Windows/Filesystem.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace NS
{
    namespace Gfx
    {
        namespace detail
        {
            [[nodiscard]] inline bool IsDdsExtension(std::string_view path)
            {
                std::string ext = OS::FileSystem::Extension(path);
                std::transform(ext.begin(), ext.end(), ext.begin(), [](uint8 c) {
                    return static_cast<char>(std::tolower(c));
                });
                return ext == ".dds";
            }
        } // namespace detail
    } // namespace Gfx
} // namespace NS
