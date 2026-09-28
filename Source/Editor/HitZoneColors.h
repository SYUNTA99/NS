#pragma once

#include "Game/Level/HitTier.h"
#include "Runtime/Core/Math.h"

namespace NS::Editor
{
    //! @brief 段の範囲の円と当たりの印を描く、段ごとの色を返す
    //! @details エディタの Scene のタブで、相手の HitZones の円と直近の当たりの印に使う
    //! @param[in] tier 段
    //! @return 段の色。真ん中は赤、惜しいは水色、外れは灰色
    [[nodiscard]] NS::Core::Color HitZoneColor(NS::Game::Level::HitTier tier) noexcept;

    //! @brief 付け忘れや崩れを知らせる警告の色を返す
    //! @details 大きさの順が崩れた範囲と、HitZones を持たない壊せる物の印に使う
    //! @return 段のどの色とも違う紫
    [[nodiscard]] NS::Core::Color HitZoneWarningColor() noexcept;

    //! @brief 範囲の円を描く色を返す
    //! @param[in] tier 円が表す段
    //! @param[in] orderBroken 真ん中が惜しい以上に崩れている場合 true
    //! @return 崩れていれば警告の色、それ以外の場合は段の色
    [[nodiscard]] NS::Core::Color HitZoneRingColor(NS::Game::Level::HitTier tier, bool orderBroken) noexcept;
} // namespace NS::Editor
