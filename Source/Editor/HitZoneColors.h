#pragma once

#include "Game/Level/HitTier.h"
#include "NSlib/Core/Math.h"

namespace NS::Editor
{
    //! @brief 段の名前で引く色の表から、段の面と当たりの印の色を返す
    //! @details エディタの Scene のタブで、相手の正面の面と直近の当たりの印に使う。表は段ごとに 1 行で、
    //! 段を足す時は行を足す。表に行の無い段は外れの行の色
    //! @param[in] tier 段
    //! @return 段の色。不透明。真ん中は赤、外れは青
    [[nodiscard]] NS::Color HitZoneColor(NS::Game::Level::HitTier tier) noexcept;
} // namespace NS::Editor
