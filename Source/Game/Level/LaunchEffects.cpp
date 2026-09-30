#include "Game/Level/LaunchEffects.h"

#include "Game/Level/ColliderBounds.h"
#include "Game/Level/LaunchedBody.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Graphics/EffectScene.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Platform/Clock.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace NS::Game::Level
{
    namespace
    {
        using NS::Core::Quaternion;
        using NS::Core::Vector3;

        constexpr std::string_view k_LaunchTrail = "launch.trail";
        constexpr std::string_view k_LaunchLandDust = "launch.landDust";
        // 落ちた所の粉の塊の寿命 (絵の定義と同じ)
        constexpr int k_LandDustLife = 24;
        // 粉の輪が再生の大きさ 1 で広がりきる半径 (m)。絵の定義の出始め 0.4 m と外へ進む 0.8 m の和
        constexpr float k_DustRingRadiusAtUnitScale = 1.2f;
        // 粉の輪を置く床からの高さ (m)。塊の中心を浮かせ、カメラへ向く板の下半分が床に切られないようにする
        constexpr float k_DustRingLift = 0.3f;
        // 床と見なす接触の法線の上向きの成分。自機が立てる斜面の上限 45 度 (JoltCharacter) と同じ
        constexpr float k_FloorNormalY = 0.7071f;
        constexpr float k_TinyLength = 1e-4f;

        [[nodiscard]] Vector3 NormalizedOr(const Vector3& v, const Vector3& fallback) noexcept
        {
            const float length = v.Length();
            if (!(length > k_TinyLength))
            {
                return fallback;
            }
            return v / length;
        }

        // 絵の +Y を向きへ回す回転
        [[nodiscard]] Quaternion TurnUpTo(const Vector3& direction) noexcept
        {
            return Quaternion::FromToRotation(Vector3{0.0f, 1.0f, 0.0f}, direction);
        }

        [[nodiscard]] NS::Gfx::EffectPlayDesc PlayAt(const Vector3& position,
                                                     const Quaternion& rotation,
                                                     const Vector3& scale) noexcept
        {
            NS::Gfx::EffectPlayDesc desc;
            desc.position = position;
            desc.rotation = rotation;
            desc.scale = scale;
            return desc;
        }

        [[nodiscard]] Vector3 Uniform(float value) noexcept
        {
            return Vector3{value, value, value};
        }
    } // namespace

    LaunchEffects::LaunchEffects() noexcept : NS::Obj::Component(NS::Obj::TickPriority::Update + 60) {}

    void LaunchEffects::OnStart()
    {
        m_body = Owner() != nullptr ? Owner()->FindComponent<LaunchedBody>() : nullptr;
        NS::Gfx::EffectScene* effects = NS::Game::Player::EffectsOf(*this);
        if (effects == nullptr)
        {
            return;
        }
        // 読めない絵は Play が無効なハンドルを返し、記録だけ残る。警告は EffectScene が名前ごとに 1 回出す
        static_cast<void>(effects->Preload(k_LaunchTrail));
        static_cast<void>(effects->Preload(k_LaunchLandDust));
    }

    void LaunchEffects::BeginTrail(HitTier tier, float power, float launchScale, const NS::Core::Vector3& launchDir)
    {
        NS::Gfx::EffectScene* effects = NS::Game::Player::EffectsOf(*this);
        // 尾は 1 本ずつ持つ。前の尾が残っていればここで消す
        if (m_trail != 0)
        {
            m_layers.Stop(effects, m_trail);
            m_trail = 0;
        }
        if (Owner() == nullptr || m_body == nullptr || m_body->Phase() != LaunchPhase::Arc)
        {
            return; // 壊れた物 (破片になって飛ばない) には付けない
        }

        // 軽い物ほど遠くへ速く飛ぶので、尾も長く残す。重い物は短い
        const float trailFrames = static_cast<float>(m_trailFramesBase) + m_trailFramesPerLaunch * std::max(launchScale, 0.0f);
        m_trailFrames = std::max(1, static_cast<int>(std::lround(trailFrames)));
        // 威力 1 で質量だけの大きさ。強く飛ばした物ほど高く上がって強く落ちるので威力でも伸ばす
        float mass = 1.0f;
        if (const NS::Obj::RigidBody* rigidBody = Owner()->FindComponent<NS::Obj::RigidBody>())
        {
            mass = rigidBody->EffectiveMass();
        }
        const float powerGrowth = std::max(0.0f, 1.0f + m_landDustPerPower * (std::max(power, 0.0f) - 1.0f));
        m_landDustScale = (m_landDustBase + m_landDustPerRootMass * std::sqrt(std::max(mass, 0.0f))) * powerGrowth;

        // 再生の大きさは自分の直径。帯の幅は絵の定義が直径への割合で持つ
        m_trailScale = 1.0f;
        NS::Core::AABB bounds{};
        if (TryGetColliderBounds(*Owner(), bounds))
        {
            m_trailScale = 2.0f * std::max(bounds.Extents.x, bounds.Extents.z);
        }
        m_launchDir = NormalizedOr(Vector3{launchDir.x, 0.0f, launchDir.z}, Vector3{1.0f, 0.0f, 0.0f});

        // 物理の前に走るので、帯の頭は直近の物理が使った速度で 1 フレーム先へ置く
        // 帯の点の +Y を飛ぶ向きへ回す。揃えないと、横から見た時に幅が道に沿って潰れる
        const Vector3 head = Owner()->Root().Position() + m_body->Velocity() * NS::Platform::FrameTimer::FixedDelta();
        NS::Gfx::EffectPlayDesc desc = PlayAt(head, TurnUpTo(NormalizedOr(m_body->Velocity(), m_launchDir)), Uniform(m_trailScale));
        // 0 番が橙、1 番が大きな外れの灰。2 番が点の寿命
        desc.dynamicInputs[0] = 1.0f;
        desc.dynamicInputs[1] = 0.0f;
        if (tier == HitTier::Wide)
        {
            desc.dynamicInputs[0] = 0.0f;
            desc.dynamicInputs[1] = 1.0f;
        }
        desc.dynamicInputs[2] = static_cast<float>(m_trailFrames);
        desc.dynamicInputs[3] = 0.0f;
        m_trail = m_layers.Play(effects, k_LaunchTrail, desc);
        m_layers.SetAmount(m_trail, static_cast<float>(m_trailFrames));
        m_trailStartStep = m_layers.Step();
    }

    void LaunchEffects::OnUpdate()
    {
        NS::Gfx::EffectScene* effects = NS::Game::Player::EffectsOf(*this);
        m_layers.BeginStep(effects);
        const int step = m_layers.Step();
        for (const ScheduledStop& stop : m_scheduledStops)
        {
            if (step >= stop.step)
            {
                m_layers.Stop(effects, stop.id);
            }
        }
        std::erase_if(m_scheduledStops, [step](const ScheduledStop& stop) { return step >= stop.step; });

        if (m_trail == 0 || step == m_trailStartStep || Owner() == nullptr || m_body == nullptr)
        {
            return;
        }
        if (m_body->Phase() == LaunchPhase::Arc)
        {
            if (const NS::Game::Player::EffectLayerRecord* record = m_layers.Find(m_trail); record != nullptr && effects != nullptr)
            {
                const Vector3 head =
                    Owner()->Root().Position() + m_body->Velocity() * NS::Platform::FrameTimer::FixedDelta();
                effects->SetTransform(
                    record->handle, head, TurnUpTo(NormalizedOr(m_body->Velocity(), m_launchDir)), Uniform(m_trailScale));
            }
            return;
        }
        EndTrail(effects);
    }

    void LaunchEffects::EndTrail(NS::Gfx::EffectScene* effects)
    {
        // 曲線を離れたのは前のフレームの LateUpdate。接触はその物理の 1 歩の物で、次の物理まで読める
        // 帯と輪はここで消す。点は世界に残るので、通った道が柱のように残る
        m_layers.Stop(effects, m_trail);
        m_trail = 0;
        if (m_body == nullptr || m_body->Phase() != LaunchPhase::Rigid || !TouchesFloor())
        {
            return;
        }
        Vector3 at = Owner()->Root().Position();
        NS::Core::AABB bounds{};
        if (TryGetColliderBounds(*Owner(), bounds))
        {
            at.y = bounds.Center.y - bounds.Extents.y;
        }
        at.y += k_DustRingLift;
        const std::uint32_t dust =
            m_layers.Play(effects,
                          k_LaunchLandDust,
                          PlayAt(at, Quaternion::Identity, Uniform(m_landDustScale / k_DustRingRadiusAtUnitScale)));
        m_layers.SetAmount(dust, m_landDustScale);
        m_scheduledStops.push_back(ScheduledStop{.id = dust, .step = m_layers.Step() + k_LandDustLife});
    }

    bool LaunchEffects::TouchesFloor() const
    {
        const NS::Obj::RigidBody* rigidBody = Owner() != nullptr ? Owner()->FindComponent<NS::Obj::RigidBody>() : nullptr;
        if (rigidBody == nullptr)
        {
            return false;
        }
        for (const NS::Phys::BodyContact& contact : rigidBody->Contacts())
        {
            if (contact.normal.y >= k_FloorNormalY)
            {
                return true;
            }
        }
        return false;
    }

    NS_CLASS(LaunchEffects)
} // namespace NS::Game::Level
