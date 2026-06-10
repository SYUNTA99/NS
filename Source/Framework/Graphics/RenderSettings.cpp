#include "Framework/Graphics/RenderSettings.h"

namespace NS::Graphics
{
    RenderSettings Resolve(const RenderSettings& defaults, const RenderSettingsOverride& over) noexcept
    {
        (void)over;
        return defaults;
    }
} // namespace NS::Graphics
