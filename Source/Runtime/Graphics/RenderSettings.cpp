#include "Runtime/Graphics/RenderSettings.h"

namespace NS::Graphics
{
    RenderSettings Resolve(const RenderSettings& defaults, const RenderSettingsOverride& over) noexcept
    {
        RenderSettings result = defaults;
        if (over.clearColor)
            result.clearColor = *over.clearColor;
        if (over.lightDir)
            result.lightDir = *over.lightDir;
        if (over.lightColor)
            result.lightColor = *over.lightColor;
        if (over.ambientColor)
            result.ambientColor = *over.ambientColor;
        if (over.groundColor)
            result.groundColor = *over.groundColor;
        if (over.exposure)
            result.exposure = *over.exposure;
        return result;
    }
} // namespace NS::Graphics
