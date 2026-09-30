#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Obj
{
    class ThirdPersonFollow;
}

namespace NS::Game::Level
{
    //! @brief 同じ配置物の追従カメラへ、追う相手の接地と速度と、
    //! 根から立ち姿の中心までの高さと、反動の状態と突進を出した向きと、
    //! 溜めの状態を渡す Component
    //! @details 追う相手は ThirdPersonFollow の追従対象から引き、相手が ICameraTarget の窓口で答えた状態だけを運ぶ
    //! 相手の部品は読まない。オデッセイでカメラが CameraTarget を通してだけプレイヤーを見るのと同じ
    //! 帯は LateUpdate + 40 で、やり直しの後・カメラの追従の前
    //! 依存: NS::Obj::ICameraTarget, NS::Obj::ThirdPersonFollow
    class FollowCameraFeed : public NS::Obj::Component
    {
    public:
        FollowCameraFeed() noexcept;

        //! 同じ配置物の追従カメラを控える。無ければ OnUpdate は何もしない
        void OnStart() override;
        //! @brief 追う相手の接地と速度と、根から立ち姿の中心までの高さを追従カメラへ渡す
        //! @details 追う相手が移動を持たなければ何もしない。
        //! 追う相手が自機なら、反動の状態かと、
        //! 最後に出した突進の出たフレームの向きも渡す。
        //! 追う相手が CollisionInput を持てば、押しているか・押している間の溜め量・
        //! 狙う相手の中心も渡す
        void OnUpdate() override;
        //! @brief 追従カメラへ渡した高さと、溜めの締め・揺れ・構図のずらしを 0 に戻し、
        //! 反動の間の追い方を止める
        //! @details プレイを終えると OnUpdate が走らず、編集中の視点がそれらを残したままになる
        void OnEndPlay() override;

        // 保存する調整値は無い。データから普通の配置物へ載せられるよう型名だけ登録する
        NS_REFLECT_NONE(FollowCameraFeed, NS::Obj::Component)

    private:
        NS::Obj::ThirdPersonFollow* m_follow = nullptr; // 同じ配置物の追従カメラ (非所有)
    };
} // namespace NS::Game::Level
