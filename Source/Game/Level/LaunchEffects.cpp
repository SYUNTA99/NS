#include "Game/Level/LaunchEffects.h"

#include "Game/Level/CollisionBounds.h"
#include "Game/Level/MapObj.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Graphics/EffectScene.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Windows/Clock.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace NS::Game::Level
{
    namespace
    {
        using NS::Quaternion;
        using NS::Vector3;
        // 尾の絵 launch.trail の、段の色の節を出す動的入力の番号。段ごとに 1 行の表で、段を足したら行を足す
        [[nodiscard]] std::size_t TrailColorInputFor(HitTier tier) noexcept
        {
            switch (tier)
            {
            case HitTier::Center:
                return 0;
            case HitTier::Wide:
                return 1;
            }
            // 番号から作った段の外の値は、中心近くの色を出さない
            return 1;
        }

        [[nodiscard]] Vector3 NormalizedOr(const Vector3& v, const Vector3& fallback) noexcept
        {
            const float length = v.Length();
            if (!(length > NS::k_Epsilon))
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

    LaunchEffects::LaunchEffects() noexcept : NS::Obj::Component() {}

    const MapObjParams& LaunchEffects::Tuning() const noexcept
    {
        if (const MapObj* owner = NS::Obj::Cast<MapObj>(Owner()))
        {
            return owner->Params();
        }
        static const MapObjParams defaults;
        return defaults;
    }

    void LaunchEffects::OnStart()
    {
        if (Owner() != nullptr && std::string_view{Owner()->ClassName()} == "MapObj")
        {
            m_body = static_cast<MapObj*>(Owner());
        }
        NS::Gfx::EffectScene* effects = NS::Game::Player::EffectsOf(*this);
        if (effects == nullptr)
        {
            return;
        }
        // 読めない絵は Play が無効なハンドルを返し、記録だけ残る。警告は EffectScene が名前ごとに 1 回出す
        static_cast<void>(effects->Preload(m_launchTrailAsset));
        static_cast<void>(effects->Preload(m_launchLandDustAsset));
    }

    void LaunchEffects::BeginTrail(HitTier tier, float power, float launchScale, const NS::Vector3& launchDir)
    {
        NS::Gfx::EffectScene* effects = NS::Game::Player::EffectsOf(*this);
        // 尾は 1 本ずつ持つ。前の尾が残っていればここで消す
        if (m_trail != 0)
        {
            m_layers.Stop(effects, m_trail);
            m_trail = 0;
        }
        if (Owner() == nullptr || m_body == nullptr || !m_body->IsArc())
        {
            return; // 壊れた物 (破片になって飛ばない) には付けない
        }

        // 軽い物ほど遠くへ速く飛ぶので、尾も長く残す。重い物は短い
        const float trailFrames = static_cast<float>(Tuning().m_trailFramesBase) +
                                  Tuning().m_trailFramesPerLaunch * std::max(launchScale, 0.0f);
        m_trailFrames = std::max(1, static_cast<int>(std::lround(trailFrames)));
        // 威力 1 で質量だけの大きさ。強く飛ばした物ほど高く上がって強く落ちるので威力でも伸ばす
        const float mass = m_body->Params().Mass();
        const float powerGrowth = std::max(0.0f, 1.0f + Tuning().m_landDustPerPower * (std::max(power, 0.0f) - 1.0f));
        m_landDustScale =
            (Tuning().m_landDustBase + Tuning().m_landDustPerRootMass * std::sqrt(std::max(mass, 0.0f))) * powerGrowth;

        // 再生の大きさは自分の直径。帯の幅は絵の定義が直径への割合で持つ
        m_trailScale = 1.0f;
        NS::AABB bounds{};
        if (TryGetCollisionBounds(*Owner(), bounds))
        {
            m_trailScale = 2.0f * std::max(bounds.Extents.x, bounds.Extents.z);
        }
        m_launchDir = NormalizedOr(Vector3{launchDir.x, 0.0f, launchDir.z}, Vector3{1.0f, 0.0f, 0.0f});

        // 置物が自分を動かす Triggers の段より前に走るので、帯の頭は放した時の速度で 1 フレーム先へ置く
        // 帯の点の +Y を飛ぶ向きへ回す。揃えないと、横から見た時に幅が道に沿って潰れる
        const Vector3 head = Owner()->Root().Position() + m_body->Velocity() * NS::OS::FrameTimer::FixedDelta();
        NS::Gfx::EffectPlayDesc desc =
            PlayAt(head, TurnUpTo(NormalizedOr(m_body->Velocity(), m_launchDir)), Uniform(m_trailScale));
        // 0 番が橙、1 番が大きな外れの灰。2 番が点の寿命
        desc.dynamicInputs[0] = 0.0f;
        desc.dynamicInputs[1] = 0.0f;
        desc.dynamicInputs[TrailColorInputFor(tier)] = 1.0f;
        desc.dynamicInputs[2] = static_cast<float>(m_trailFrames);
        desc.dynamicInputs[3] = 0.0f;
        m_trail = m_layers.Play(effects, m_launchTrailAsset, desc);
        m_layers.SetAmount(m_trail, static_cast<float>(m_trailFrames));
        m_trailStartStep = m_layers.Step();
    }

    void LaunchEffects::BeginStep()
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
    }

    void LaunchEffects::OnUpdate()
    {
        NS::Gfx::EffectScene* effects = NS::Game::Player::EffectsOf(*this);
        const int step = m_layers.Step();
        if (m_trail == 0 || step == m_trailStartStep || Owner() == nullptr || m_body == nullptr)
        {
            return;
        }
        if (m_body->IsArc())
        {
            if (const NS::Game::Player::EffectLayerRecord* record = m_layers.Find(m_trail);
                record != nullptr && effects != nullptr)
            {
                const Vector3 head = Owner()->Root().Position();
                effects->SetTransform(record->handle,
                                      head,
                                      TurnUpTo(NormalizedOr(m_body->Velocity(), m_launchDir)),
                                      Uniform(m_trailScale));
            }
            return;
        }
        EndTrail(effects);
    }

    void LaunchEffects::EndTrail(NS::Gfx::EffectScene* effects)
    {
        // 帯と輪はここで消す。点は世界に残るので、通った道が柱のように残る
        m_layers.Stop(effects, m_trail);
        m_trail = 0;
    }

    void LaunchEffects::CancelTrail()
    {
        EndTrail(NS::Game::Player::EffectsOf(*this));
    }

    void LaunchEffects::NotifyLanding(const Vector3& position, const Vector3& normal)
    {
        NS::Gfx::EffectScene* effects = NS::Game::Player::EffectsOf(*this);
        EndTrail(effects);
        const Vector3 at = position + normal * m_dustRingLift;
        const std::uint32_t dust =
            m_layers.Play(effects,
                          m_launchLandDustAsset,
                          PlayAt(at, TurnUpTo(normal), Uniform(m_landDustScale / std::max(m_landDustAssetRadius, NS::k_Epsilon))));
        m_layers.SetAmount(dust, m_landDustScale);
        m_scheduledStops.push_back(ScheduledStop{.id = dust, .step = m_layers.Step() + std::max(m_landDustLife, 1)});
    }

    NS_CLASS(LaunchEffects)
} // namespace NS::Game::Level
