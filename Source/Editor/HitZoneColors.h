#pragma once

#include "Game/Level/HitTier.h"
#include "NSlib/Core/Math.h"

namespace NS::Editor
{
    //! 当たりの領域と命中点に使う不透明色。中央は赤、外れは青、未登録の段は外れの色
    [[nodiscard]] NS::Color HitZoneColor(GL::Level::HitTier tier) noexcept;
} // namespace NS::Editor
