#pragma once

#include "Runtime/Object/Component.h"
#include "Runtime/Object/Reflection/Reflection.h"

#include <string>
#include <string_view>
#include <utility>

namespace NS::Object
{
    class SkeletalAnimationComponent;
}

namespace NS::Game::Player
{
    class PlayerComponent;
    class PlayerStateManagerComponent;

    //! @brief 自機の見た目を毎フレーム決める
    //! @details 接地・水平の速さ・垂直速度・ぶら下がりかを見て、クリップと再生速度を選ぶ
    //! 状態 1 つにクリップ 1 本を割り当てる形は採らない。走りの速度と跳ぶと落ちるの出し分けが
    //! 同じ状態の中で変わるので、状態は入力の 1 つとして使う
    //! 優先度は Update 帯の +50。移動が終わった後に選び、骨を回す SkeletalAnimationComponent (+100) より前に置く
    //! 依存: PlayerComponent, PlayerStateManagerComponent, NS::Object::SkeletalAnimationComponent
    class PlayerAnimatorComponent : public NS::Object::Component
    {
    public:
        PlayerAnimatorComponent() noexcept;

        //! 同居する PlayerComponent / PlayerStateManagerComponent / SkeletalAnimationComponent を控える
        void OnStart() override;
        //! クリップと再生速度を選び直す。PlayerComponent か SkeletalAnimationComponent が欠けていれば何もしない
        void OnUpdate() override;

        void SetIdleClip(std::string name) noexcept { m_idleClip = std::move(name); }
        void SetWalkClip(std::string name) noexcept { m_walkClip = std::move(name); }
        void SetRunClip(std::string name) noexcept { m_runClip = std::move(name); }
        void SetJumpClip(std::string name) noexcept { m_jumpClip = std::move(name); }
        void SetFallClip(std::string name) noexcept { m_fallClip = std::move(name); }
        void SetLedgeHangClip(std::string name) noexcept { m_ledgeHangClip = std::move(name); }

        NS_REFLECT_BEGIN(PlayerAnimatorComponent, NS::Object::Component)
        NS_REFLECT_FIELD(m_idleClip, "立ちのクリップ")
        NS_REFLECT_FIELD(m_walkClip, "歩きのクリップ")
        NS_REFLECT_FIELD(m_runClip, "走りのクリップ")
        NS_REFLECT_FIELD(m_jumpClip, "跳ぶクリップ")
        NS_REFLECT_FIELD(m_fallClip, "落ちるクリップ")
        NS_REFLECT_FIELD(m_ledgeHangClip, "ぶら下がりのクリップ")
        NS_REFLECT_FIELD(m_runBlendRatio, "走りへ移る速さの比")
        NS_REFLECT_FIELD(m_minPlaybackSpeed, "再生速度の下限")
        NS_REFLECT_END()

    private:
        [[nodiscard]] std::string_view ChooseClip(float lateralSpeed) const noexcept;
        [[nodiscard]] float ChoosePlaybackSpeed(std::string_view clip, float lateralSpeed) const noexcept;

        // 既定は同梱モデル Xbot.glb が持つ名前。跳ぶ・落ちる・ぶら下がりの絵はそのモデルに無いので空で、
        // 空と一致無しは立ちへ落とす。前の絵のまま凍ると、走ったまま落下する
        std::string m_idleClip = "idle";
        std::string m_walkClip = "walk";
        std::string m_runClip = "run";
        std::string m_jumpClip{};
        std::string m_fallClip{};
        std::string m_ledgeHangClip{};

        // 歩きの絵は最大速度の半分ほどの見た目と見込む。半分より手前で移らないと足が接地点を滑る
        // 絵の見た目を実機で見ていないので、この比は触って詰める前提
        float m_runBlendRatio = 0.4f;
        // 下げ止めないと、止まりかけで足がほぼ静止して滑って見える。実機で見ていないので触って詰める前提
        float m_minPlaybackSpeed = 0.5f;

        PlayerComponent* m_player = nullptr;                           // 速さと接地の出所 (非所有)
        PlayerStateManagerComponent* m_states = nullptr;               // 現在状態の問い合わせ先 (非所有)
        NS::Object::SkeletalAnimationComponent* m_animation = nullptr; // クリップの差し先 (非所有)
        std::string m_appliedClip{};                                   // 最後に選べたクリップ名
    };
} // namespace NS::Game::Player
