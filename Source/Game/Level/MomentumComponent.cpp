#include "Game/Level/MomentumComponent.h"

#include "Game/Player/PlayerComponent.h"
#include "Runtime/Core/Clock.h"
#include "Runtime/Core/LogCategories.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Level
{
    namespace
    {
        // 走り出す境目。PlayerComponent が歩き速度と最高速度を分ける値と同じ
        // TODO: 同じ 0.5 が UpdateLocomotion にも直接書かれている。片方だけ動かすと走りの境目と昇格の境目がずれる
        constexpr float k_RunInputThreshold = 0.5f;

        // 進んでいる向きがあると言える下限の速さの 2 乗。0.1 m/s は歩き速度 4.0 の 40 分の 1
        constexpr float k_MovingSpeedSq = 0.01f;

        [[nodiscard]] const char* LevelLabel(MomentumLevel level) noexcept
        {
            switch (level)
            {
            case MomentumLevel::MaxDash:
                return "最高ダッシュ";
            default:
                return "通常";
            }
        }
    } // namespace

    // PlayerInputComponent の 0 より後、PlayerComponent の 200 より前。移動が動く前に最高速度が決まる
    MomentumComponent::MomentumComponent() noexcept : NS::Object::Component(NS::Object::TickPriority::Update - 150)
    {
        // 平らな 2 点の倍率 1 を既定にするのは、形を触るまで平地も下りも従来の昇格秒のままにするため
        m_promoteRateCurve.count = 2;
        m_promoteRateCurve.keys[0] = NS::Object::Curve::Key{0.0f, 1.0f};
        m_promoteRateCurve.keys[1] = NS::Object::Curve::Key{1.0f, 1.0f};
    }

    void MomentumComponent::OnStart()
    {
        m_movement = Owner()->FindComponent<NS::Game::Player::PlayerComponent>();
    }

    float MomentumComponent::SpeedForLevel(MomentumLevel level) const noexcept
    {
        switch (level)
        {
        case MomentumLevel::MaxDash:
            return m_maxDashSpeed;
        default:
            return m_normalSpeed;
        }
    }

    void MomentumComponent::SetLevel(MomentumLevel level) noexcept
    {
        m_level = level;
        m_runSeconds = 0.0f;
        m_graceTimer = 0.0f;
        m_reboundGrace = false;
    }

    void MomentumComponent::BeginGrace() noexcept
    {
        m_graceTimer = 0.0f;
        m_reboundGrace = true;
    }

    bool MomentumComponent::IsInGrace() const noexcept
    {
        return m_level != MomentumLevel::Normal && (m_reboundGrace || m_graceTimer > 0.0f);
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
        const MomentumLevel previous = m_level;

        // 反発から始まった猶予だけは接地を見ない。弾かれた自機は空中に居るので、接地を条件にすると着地まで切れない
        if (m_reboundGrace)
        {
            // 走り直しは着地してから。空中で入力を倒しただけで猶予が解けると、立て直しが着地を待たずに済んでしまう
            if (IsRunInputActive() && m_movement->IsGrounded())
            {
                m_reboundGrace = false;
                m_graceTimer = 0.0f;
            }
            else
            {
                m_graceTimer += dt;
                if (m_level != MomentumLevel::Normal && m_graceTimer + dt * 0.5f >= m_demoteGraceSeconds)
                {
                    m_level = MomentumLevel::Normal;
                    m_graceTimer = 0.0f;
                    m_runSeconds = 0.0f;
                    m_reboundGrace = false;
                }
            }
        }
        // 空中では積算も猶予も進めず段を保つ。滞空は猶予秒より長く、猶予を進めると空中で手を放すたび段が落ちる
        // 積算も止めるのは、跳んでいる間は助走ではないため
        else if (m_movement->IsGrounded())
        {
            // 走行の判定は入力そのもの。速度の大きさで代用すると、手を放した後も減速しきるまで走行のままになる
            if (IsRunInputActive())
            {
                m_graceTimer = 0.0f;

                // 下り勾配は Velocity().y から作る。接地して坂を下ると CapsuleMover が斜面に沿わせるので y が負になる
                // 通常速度で割った 0..1 は坂の角度と走る向きの両方が効く。登りの正は 0 へ丸める
                const float descent = NS::Core::Clamp(-m_movement->Velocity().y / m_normalSpeed, 0.0f, 1.0f);
                float rate = m_promoteRateCurve.Evaluate(descent);
                // Inspector で点を全部消すと Evaluate が 0 を返して昇格が永久に止まるため、0 以下は 1 とみなす
                if (!(rate > 0.0f))
                    rate = 1.0f;
                m_runSeconds += dt * rate;

                // 半歩ぶん先を見て昇格の秒に最も近い固定ステップで上げる。dt の積算は誤差で昇格の秒へ届かず 1 歩遅れる
                const float elapsed = m_runSeconds + dt * 0.5f;
                if (m_level == MomentumLevel::Normal && elapsed >= m_promoteSeconds)
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
                    m_level = MomentumLevel::Normal;
                    m_graceTimer = 0.0f;
                    m_runSeconds = 0.0f;
                }
            }
        }

        if (m_level != previous)
            NS_LOG_INFO(Game, "勢いの段: {} -> {}", LevelLabel(previous), LevelLabel(m_level));

        m_movement->SetMaxSpeed(SpeedForLevel(m_level));
    }

    NS_CLASS(MomentumComponent)
} // namespace NS::Game::Level
