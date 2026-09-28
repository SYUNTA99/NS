#pragma once

#include <Runtime/Object/Reflection/ComponentEntry.h>

namespace NsTest
{
    //! @brief 試験で組む壊せる物に足す段の範囲の項目を作る
    //! @details 段の範囲を持たない壊せる物は体当たりの相手にならない
    //! 範囲は、試験の自機 (半径 0.4 m) が半幅 0.5 m の相手へ軸に沿って当たった時に、前の自機の側の割合
    //! 0.35 / 0.7 × 届く幅 0.9 m と同じ段になる値。前の段を前提にした試験の意味を保つ
    //! @return HitZones の項目。3 段、真ん中 0.315 m、惜しい 0.63 m
    [[nodiscard]] inline nlohmann::json MakeTestHitZonesEntry()
    {
        nlohmann::json entry = NS::Obj::MakeComponentEntry("HitZones");
        NS::Obj::SetField(entry, "真ん中の範囲", 0.315f);
        NS::Obj::SetField(entry, "惜しいの範囲", 0.63f);
        return entry;
    }
} // namespace NsTest
