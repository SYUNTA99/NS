#include "Game/Level/MomentumComponent.h"

#include "Runtime/Core/Clock.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Components/CharacterMovementComponent.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Level
{
    namespace
    {
        // 走り出す境目。CharacterMovementComponent が歩き速度と最高速度を分ける値と同じ
        // TODO: 同じ 0.5 が UpdateLocomotion にも直接書かれている。片方だけ動かすと歩きのまま昇格する
        constexpr float k_RunInputThreshold = 0.5f;

        // 進んでいる向きがあると言える下限の速さの 2 乗。0.1 m/s は歩き速度 4.0 の 40 分の 1
        constexpr float k_MovingSpeedSq = 0.01f;

        [[nodiscard]] const char* LevelLabel(MomentumLevel level) noexcept
        {
            switch (level)
            {
            case MomentumLevel::Dash:
                return "ダッシュ";
            case MomentumLevel::MaxDash:
                return "最高ダッシュ";
            default:
                return "通常";
            }
        }

        // 1 段だけ落とす。Normal が下限
        [[nodiscard]] MomentumLevel DemotedFrom(MomentumLevel level) noexcept
        {
            if (level == MomentumLevel::MaxDash)
                return MomentumLevel::Dash;
            return MomentumLevel::Normal;
        }
    } // namespace

    // PlayerInputComponent の 0 より後、CharacterMovementComponent の 200 より前。移動が動く前に最高速度が決まる
    MomentumComponent::MomentumComponent() noexcept : NS::Object::Component(NS::Object::TickPriority::Update - 150) {}

    void MomentumComponent::OnStart()
    {
        m_movement = Owner()->FindComponent<NS::Object::CharacterMovementComponent>();
    }

    float MomentumComponent::SpeedForLevel(MomentumLevel level) const noexcept
    {
        switch (level)
        {
        case MomentumLevel::Dash:
            return m_dashSpeed;
        case MomentumLevel::MaxDash:
            return m_maxDashSpeed;
        default:
            return m_normalSpeed;
        }
    }

    bool MomentumComponent::IsInGrace() const noexcept
    {
        return m_level != MomentumLevel::Normal && m_graceTimer > 0.0f;
    }

    bool MomentumComponent::IsRunInputActive() const noexcept
    {
        if (m_movement->DesiredSpeedScale() < k_RunInputThreshold)
            return false;
        if (!m_requireForwardInput)
            return true;

        // 進行方向への入力だけを走行とみなす設定。水平成分だけで見る
        const NS::Core::Vector3 velocity = m_movement->Velocity();
        const NS::Core::Vector3 direction = m_movement->DesiredDirection();
        // 止まっている時は比べる向きが無い。ここで弾くと走り出しの 1 歩が積算に入らず昇格が 1 歩遅れる
        if (velocity.x * velocity.x + velocity.z * velocity.z < k_MovingSpeedSq)
            return true;
        return velocity.x * direction.x + velocity.z * direction.z > 0.0f;
    }

    void MomentumComponent::OnUpdate()
    {
        if (m_movement == nullptr)
            return;

        const float dt = NS::Core::FrameTimer::FixedDelta();
        // 走行の判定は入力そのもの。速度の大きさで代用すると、手を放した後も減速しきるまで走行のままになる
        const bool running = IsRunInputActive() && m_movement->IsGrounded();

        const MomentumLevel previous = m_level;
        if (running)
        {
            m_graceTimer = 0.0f;
            m_runSeconds += dt;

            // 半歩ぶん先を見て昇格の秒に最も近い固定ステップで上げる。dt の積算は誤差で 1.5 秒へ届かず 1 歩遅れる
            const float elapsed = m_runSeconds + dt * 0.5f;
            if (m_level == MomentumLevel::Normal && elapsed >= m_dashPromoteSeconds)
            {
                m_level = MomentumLevel::Dash;
                m_runSeconds = 0.0f;
            }
            else if (m_level == MomentumLevel::Dash && elapsed >= m_maxDashPromoteSeconds)
            {
                m_level = MomentumLevel::MaxDash;
                m_runSeconds = 0.0f;
            }
        }
        else
        {
            m_graceTimer += dt;
            // 昇格と同じ半歩ぶんを足す。素直に比べると猶予秒の値しだいで落ちるのが 1 歩ずれる
            if (m_level != MomentumLevel::Normal && m_graceTimer + dt * 0.5f >= m_demoteGraceSeconds)
            {
                m_level = DemotedFrom(m_level);
                m_graceTimer = 0.0f;
                m_runSeconds = 0.0f;
            }
        }

        if (m_level != previous)
            NS_LOG_INFO(Game, "勢いの段: {} -> {}", LevelLabel(previous), LevelLabel(m_level));

        m_movement->SetMaxSpeed(SpeedForLevel(m_level));
    }

    NS_CLASS(MomentumComponent)
} // namespace NS::Game::Level
