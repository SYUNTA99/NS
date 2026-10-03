#pragma once

#include "Game/Player/PlayerGravity.h"

namespace NS::Game::Player
{
    //! @brief 溜めて放った玉の道筋を決める値
    //! @details 水平は一定の速さで進み、縦は 1 フレームずつ ChooseGravity の強さで変わる。Player が突進の間に当てる
    //! 重力と同じ選び方と順で、速さへ重力を足してから位置を進める
    struct LaunchPath
    {
        float horizontalSpeed = 0.0f; //!< 水平の速さ (m/s)
        float verticalSpeed = 0.0f;   //!< 放つ瞬間の縦の速さ (m/s)。上が正
        PlayerGravity gravity{};      //!< 重力の強さを選ぶ欄の写し
        float dt = 0.0f;              //!< 1 フレームの秒
        bool grounded = false;        //!< 接地して放つか。真なら道筋は放った高さより下へ行かない
    };

    //! @brief 放った玉の中心が水平に distance 進んだ所での、放った高さからの高さを返す
    //! @details フレームの間は直線で結ぶ。接地して放った道筋は、放った高さまで戻るとそこで縦の速さを 0 にして
    //! 床の上を進む
    //! @param[in] path 道筋を決める値
    //! @param[in] distance 放った所からの水平の距離 (m)
    //! @return 放った高さからの高さ (m)。上が正。水平の速さか dt が有限の正でない時、distance が有限の 0 以上でない時、
    //! 縦の速さが有限でない時は 0
    [[nodiscard]] float LaunchHeightAt(const LaunchPath& path, float distance) noexcept;

    //! @brief 放つ上下の速さを探す時の入力
    struct LaunchPitchDesc
    {
        float ballHeight = 0.0f;       //!< 放つ時の玉の中心の高さ (世界の y)
        float targetHeight = 0.0f;     //!< 相手に触れる所で玉の中心を着けたい高さ (世界の y)
        float contactDistance = 0.0f;  //!< 玉の中心が相手に触れる所までの水平の距離 (m)
        float horizontalSpeed = 0.0f;  //!< 突進の水平の速さ (m/s)
        PlayerGravity gravity{};       //!< 重力の強さを選ぶ欄の写し
        float maxAngleDegrees = 40.0f; //!< 放つ角度の上限 (度)。上向きも下向きもこの角度で切る
        bool grounded = false;         //!< 接地しているか。真なら下へ向けない
        float dt = 0.0f;               //!< 1 フレームの秒
    };

    //! @brief LaunchPitch の結果
    struct LaunchPitchResult
    {
        float verticalSpeed = 0.0f; //!< 放つ瞬間の縦の速さ (m/s)。上が正。届かない時は 0
        bool reachable = false;     //!< 上限の角度の中で着きたい高さに着く場合 true
    };

    //! @brief 触れる所で着きたい高さになる、放つ瞬間の縦の速さを探す
    //! @details LaunchHeightAt と同じ道筋で、縦の速さを上限の角度の中で二分探索する。接地していれば下限は 0
    //! で水平、空中なら上限と同じ角度の下向き。上限の角度の中で着かない時は届かないとして縦の速さ 0 を返す
    //! @param[in] desc 探す時の入力
    //! @return 求めた縦の速さと届くか。入力に有限でない値がある時、水平の速さか dt が正でない時、
    //! 触れる所までの距離が負の時は届かない
    [[nodiscard]] LaunchPitchResult LaunchPitch(const LaunchPitchDesc& desc) noexcept;
} // namespace NS::Game::Player
