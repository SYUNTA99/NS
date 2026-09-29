#pragma once

#include <Runtime/Object/Reflection/ComponentEntry.h>

namespace NsTest
{
    //! @brief 試験で組む壊せる物に、段の面 (HitZones) と気持ちいいの色 (HitZoneArea) の項目を足す
    //! @details 段の面を持たない壊せる物は体当たりの相手にならない
    //! 色は横幅 0.35・縦の幅 1 の箱。横ずれ比 0.35 の内側が中心近くで、高さは見ない。試験の自機 (半径 0.4 m) と
    //! 半幅 0.5 m の箱なら横ずれ 0.315 m が縁。前の範囲 (m) の段を前提にした試験の意味を保つ
    //! @param[in,out] components 壊せる物の部品の項目の並び
    inline void AddTestHitZones(nlohmann::json& components)
    {
        components.push_back(NS::Obj::MakeComponentEntry("HitZones"));
        nlohmann::json area = NS::Obj::MakeComponentEntry("HitZoneArea");
        NS::Obj::SetField(area, "横幅", 0.35f);
        NS::Obj::SetField(area, "縦の幅", 1.0f);
        components.push_back(area);
    }
} // namespace NsTest
