#pragma once

#include "Game/Level/HitTier.h"
#include "Runtime/Core/Math.h"

namespace NS::Editor
{
    //! @brief 段の面と当たりの印を描く、段ごとの色を返す
    //! @details エディタの Scene のタブで、相手の HitZones の面と直近の当たりの印に使う
    //! @param[in] tier 段
    //! @return 段の色。真ん中は赤、外れは青
    [[nodiscard]] NS::Core::Color HitZoneColor(NS::Game::Level::HitTier tier) noexcept;

    //! @brief 付け忘れを知らせる警告の色を返す
    //! @details HitZones を持たない壊せる物の印に使う
    //! @return 段のどの色とも違う紫
    [[nodiscard]] NS::Core::Color HitZoneWarningColor() noexcept;
} // namespace NS::Editor
