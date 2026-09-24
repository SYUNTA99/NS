#include "Runtime/Object/ObjectName.h"

#include <cstdint>

namespace NS::Obj
{
    std::string MakeUniqueObjectName(std::string_view base, const std::unordered_set<std::string>& used)
    {
        std::string name{"Object"};
        if (!base.empty())
        {
            name = std::string{base};
        }
        if (!used.contains(name))
        {
            return name;
        }
        for (std::uint32_t n = 1;; ++n)
        {
            std::string candidate = name + "_" + std::to_string(n);
            if (!used.contains(candidate))
            {
                return candidate;
            }
        }
    }
} // namespace NS::Obj
