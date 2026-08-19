#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Object
{
    class CharacterMovementComponent;
} // namespace NS::Object

namespace NS::Game::Level
{
    //! @brief 勢いの段
    //! @details 走行時間だけで上がる。段と段の間に連続量を持たない
    enum class MomentumLevel
    {
        Normal,
        Dash,
        MaxDash,
    };

    //! @brief 走行時間で最高速度を 3 段に上げる Component
    //! @details 段は MovementState と独立で、ダッシュ中のジャンプも落下も成立する。状態機械の状態にはしない
    //! 帯は Update より前。CharacterMovementComponent が動く前にその固定ステップの最高速度を決める
    //! 止まったまま勢いを上げる入力の受け口を持たない
    //! 依存: NS::Object::CharacterMovementComponent
    class MomentumComponent : public NS::Object::Component
    {
    public:
        MomentumComponent() noexcept;

        //! 同じ配置物の CharacterMovementComponent を引き当てる。見つからなければ以後何もしない
        void OnStart() override;

        //! 接地したまま走行入力がある間だけ昇格の積算秒を進め、段に応じた最高速度を書き込む
        void OnUpdate() override;

        //! 現在の段
        [[nodiscard]] MomentumLevel Level() const noexcept { return m_level; }

        //! 段に対応する最高速度を返す
        [[nodiscard]] float SpeedForLevel(MomentumLevel level) const noexcept;

        // 勢いの調整値を Inspector へ公開する。プレイ中にライブで触って感触を詰める用途
        NS_REFLECT_BEGIN(MomentumComponent, NS::Object::Component)
        NS_REFLECT_FIELD(m_normalSpeed, "通常速度")
        NS_REFLECT_FIELD(m_dashSpeed, "ダッシュ速度")
        NS_REFLECT_FIELD(m_maxDashSpeed, "最高ダッシュ速度")
        NS_REFLECT_FIELD(m_dashPromoteSeconds, "ダッシュ昇格秒")
        NS_REFLECT_FIELD(m_maxDashPromoteSeconds, "最高ダッシュ昇格秒")
        NS_REFLECT_END()

    private:
        float m_normalSpeed = 8.0f;
        float m_dashSpeed = 12.0f;
        float m_maxDashSpeed = 16.0f;
        float m_dashPromoteSeconds = 1.5f;
        float m_maxDashPromoteSeconds = 2.5f;

        float m_runSeconds = 0.0f; // 今の段になってからの走行の積算秒
        MomentumLevel m_level = MomentumLevel::Normal;
        NS::Object::CharacterMovementComponent* m_movement = nullptr; // 同じ配置物の移動。非所有
    };
} // namespace NS::Game::Level
