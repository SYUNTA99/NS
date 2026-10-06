#pragma once

#include "Game/Level/HitTier.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Object/Reflection/ActorRef.h"

// 突進の狙いの予測の型
// Player と ImpactResolver の両方が使うので、どちらのヘッダにも置かない

namespace NS::Game::Level
{
    //! @brief 押している間の狙いの線。狙う相手を探す線で、溜めている間は SlamArrow がこの線の向きへ放った玉の道筋に
    //! 矢印を描く。溜めて放した突進はこの線の向きと縦の速さで出て、突進の間も向きを曲げない
    struct AimLine
    {
        NS::Vector3 origin;    //!< 線を引き始める自機の位置 (配置物の根)。世界座標
        NS::Vector3 direction; //!< カメラの管理役の ViewPose から作った水平の前。正規化済みで y は 0
        float length = 0.0f;         //!< 線に沿って突進が止まる所までの距離。欄「突進距離」の値で、単位は m
        //! 溜めて放つ瞬間の縦の速さ (m/s)。上が正。狙う相手の SlamLineTarget::launchVerticalSpeed で、相手が無ければ 0
        float launchVerticalSpeed = 0.0f;
        bool grounded = false; //!< 線を控えた時に接地していたか。真なら道筋は放った高さより下へ行かない
    };

    //! @brief 突進の線で最初に触れる相手の予測
    struct SlamLineTarget
    {
        NS::Obj::ActorRef target{};  //!< 相手の配置物
        NS::AABB bounds{};     //!< 相手の当たりの外接箱。世界座標
        NS::Vector3 origin;    //!< 探した時の自機の位置。世界座標
        NS::Vector3 direction; //!< 探した水平の向き。正規化済みで y は 0
        float along = 0.0f;          //!< 自機の位置から相手の外接箱の中心までの、線に沿った水平の距離。単位は m
        float offset = 0.0f;         //!< 面の判定の横ずれ。0 以上 1 以下で、裁定の当たりの横ずれと同じ式
        //! 線を進む自機の当たりの玉が相手の当たりの形に初めて触れるまでに、玉の中心が線に沿って進む距離。単位は m。
        //! 1 mm の幅で、触れている側へ丸める
        float contact = 0.0f;
        //! 溜めて放つ瞬間の縦の速さ (m/s)。上が正。LaunchPitch の値で、届かない相手と応じない相手は 0
        float launchVerticalSpeed = 0.0f;
        //! 放った玉の中心が相手に触れる所までの、線に沿った水平の距離 (m)。届く相手は着きたい高さで測り、
        //! それ以外は contact と同じ
        float launchContact = 0.0f;
        //! 段の予測。放つ縦の速さの道筋が launchContact で居る高さで、裁定と同じ面の判定で出す
        HitTier tier = HitTier::Wide;
    };
} // namespace NS::Game::Level
