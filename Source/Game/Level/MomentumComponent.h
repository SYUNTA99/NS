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
    //! @details 段は MovementState と独立で、状態機械の状態にはしない
    //! 帯は Update より前。CharacterMovementComponent が動く前にその固定ステップの最高速度を決める
    //! 昇格に使う入力は走行入力だけ
    //! 依存: NS::Object::CharacterMovementComponent
    class MomentumComponent : public NS::Object::Component
    {
    public:
        MomentumComponent() noexcept;

        //! 同じ配置物の CharacterMovementComponent を引き当てる。見つからなければ以後何もしない
        void OnStart() override;

        //! 接地したまま走行入力がある間だけ昇格の秒を積む。途切れれば猶予を数えて 1 段落とす
        //! 空中では積算も猶予も止め、段をそのまま保つ
        //! 最高速度は段に対応する値を毎ステップ CharacterMovementComponent へ書き込む
        void OnUpdate() override;

        //! 現在の段
        [[nodiscard]] MomentumLevel Level() const noexcept { return m_level; }

        //! 段に対応する最高速度を返す
        [[nodiscard]] float SpeedForLevel(MomentumLevel level) const noexcept;

        //! 走行が途切れてからの積算秒。走り直すか段が落ちると 0 へ戻る
        [[nodiscard]] float GraceSeconds() const noexcept { return m_graceTimer; }

        //! 猶予を数えている最中の場合 true、それ以外の場合は false。段が Normal のときは常に false
        [[nodiscard]] bool IsInGrace() const noexcept;

        // プレイ中に Inspector で触って感触を詰められるよう公開する
        NS_REFLECT_BEGIN(MomentumComponent, NS::Object::Component)
        NS_REFLECT_FIELD(m_normalSpeed, "通常速度")
        NS_REFLECT_FIELD(m_dashSpeed, "ダッシュ速度")
        NS_REFLECT_FIELD(m_maxDashSpeed, "最高ダッシュ速度")
        NS_REFLECT_FIELD(m_dashPromoteSeconds, "ダッシュ昇格秒")
        NS_REFLECT_FIELD(m_maxDashPromoteSeconds, "最高ダッシュ昇格秒")
        NS_REFLECT_FIELD(m_demoteGraceSeconds, "降格猶予秒")
        NS_REFLECT_FIELD(m_requireForwardInput, "復帰に進行方向入力を要求")
        NS_REFLECT_END()

    private:
        // 走行入力が出ているかを返す。事前条件: m_movement が非 null
        [[nodiscard]] bool IsRunInputActive() const noexcept;

        float m_normalSpeed = 8.0f;
        float m_dashSpeed = 12.0f;
        float m_maxDashSpeed = 16.0f;
        float m_dashPromoteSeconds = 1.5f;
        float m_maxDashPromoteSeconds = 2.5f;
        float m_demoteGraceSeconds = 0.5f;
        bool m_requireForwardInput = false;

        float m_runSeconds = 0.0f; // 今の段になってからの走行の積算秒
        float m_graceTimer = 0.0f; // 走行が途切れてからの積算秒
        MomentumLevel m_level = MomentumLevel::Normal;
        NS::Object::CharacterMovementComponent* m_movement = nullptr; // 同じ配置物の移動。非所有
    };
} // namespace NS::Game::Level
