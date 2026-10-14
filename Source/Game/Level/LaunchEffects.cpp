#include "Game/Level/LaunchEffects.h"
#include "NSlib/Core/Types.h"

#include "Game/Level/MapObj.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/IUse/IUseEffect.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/SubObjects/SphereCollision.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace GL::Level
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

        // 物理への登録に依らず、持ち主の変換と欄から作る
        [[nodiscard]] bool TryGetCollisionBounds(const NS::Obj::Actor& object, NS::AABB& outBounds) noexcept
        {
            if (const NS::Obj::SphereCollision* sphere =
                    NS::Obj::Cast<NS::Obj::SphereCollision>(object.CollisionSubObj()))
            {
                outBounds = sphere->WorldAABB();
                return true;
            }
            return false;
        }
    } // namespace

    LaunchEffects::LaunchEffects() noexcept : NS::Obj::SubObject() {}

    void LaunchEffects::OnStart()
    {
        m_body = NS::Obj::Cast<MapObj>(Owner());
        if (m_body != nullptr)
        {
            NS::Obj::PreloadEffects(m_body, {m_launchTrailAsset, m_launchLandDustAsset});
        }
    }

    void LaunchEffects::BeginTrail(HitTier tier, float power, float launchScale, const NS::Vector3& launchDir)
    {
        if (m_body == nullptr)
        {
            return;
        }
        // 尾は 1 本ずつ持つ。前の尾が残っていればここで消す
        if (m_trail != 0)
        {
            NS::Obj::StopEffect(m_body, m_trail);
            m_trail = 0;
        }
        if (!m_body->IsArc())
        {
            return; // 壊れた物 (破片になって飛ばない) には付けない
        }

        // 軽い物ほど遠くへ速く飛ぶので、尾も長く残す。重い物は短い
        const float trailFrames =
            static_cast<float>(m_trailFramesBase) + m_trailFramesPerLaunch * std::max(launchScale, 0.0f);
        m_trailFrames = std::max(1, static_cast<int>(std::lround(trailFrames)));
        // 威力 1 で質量だけの大きさ。強く飛ばした物ほど高く上がって強く落ちるので威力でも伸ばす
        const float mass = m_body->Params().Mass();
        const float powerGrowth = std::max(0.0f, 1.0f + m_landDustPerPower * (std::max(power, 0.0f) - 1.0f));
        m_landDustScale = (m_landDustBase + m_landDustPerRootMass * std::sqrt(std::max(mass, 0.0f))) * powerGrowth;

        // 再生の大きさは自分の直径。帯の幅は絵の定義が直径への割合で持つ
        m_trailScale = 1.0f;
        NS::AABB bounds{};
        if (TryGetCollisionBounds(*m_body, bounds))
        {
            m_trailScale = 2.0f * std::max(bounds.Extents.x, bounds.Extents.z);
        }
        m_launchDir = NormalizedOr(Vector3{launchDir.x, 0.0f, launchDir.z}, Vector3{1.0f, 0.0f, 0.0f});

        // 置物が自分を動かす Triggers の段より前に走るので、帯の頭は放した時の速度で 1 フレーム先へ置く
        // 帯の点の +Y を飛ぶ向きへ回す。揃えないと、横から見た時に幅が道に沿って潰れる
        const Vector3 head = m_body->Root().Position() + m_body->Velocity() * BodyDelta();
        NS::Gfx::EffectPlayDesc desc =
            PlayAt(head, TurnUpTo(NormalizedOr(m_body->Velocity(), m_launchDir)), Uniform(m_trailScale));
        // 0 番が橙、1 番が大きな外れの灰。2 番が点の寿命
        desc.dynamicInputs[0] = 0.0f;
        desc.dynamicInputs[1] = 0.0f;
        desc.dynamicInputs[TrailColorInputFor(tier)] = 1.0f;
        desc.dynamicInputs[2] = static_cast<float>(m_trailFrames);
        desc.dynamicInputs[3] = 0.0f;
        m_trail = NS::Obj::PlayEffect(m_body, m_launchTrailAsset, desc);
        NS::Obj::SetEffectAmount(m_body, m_trail, static_cast<float>(m_trailFrames));
        m_trailStartStep = NS::Obj::EffectStep(m_body);
    }

    void LaunchEffects::OnUpdate()
    {
        if (m_trail == 0 || m_body == nullptr || NS::Obj::EffectStep(m_body) == m_trailStartStep)
        {
            return;
        }
        if (m_body->IsArc())
        {
            NS::Obj::SetEffectTransform(m_body,
                                        m_trail,
                                        m_body->Root().Position(),
                                        TurnUpTo(NormalizedOr(m_body->Velocity(), m_launchDir)),
                                        Uniform(m_trailScale));
            return;
        }
        EndTrail();
    }

    void LaunchEffects::EndTrail()
    {
        if (m_body == nullptr)
        {
            return;
        }
        // 帯と輪はここで消す。点は世界に残るので、通った道が柱のように残る
        NS::Obj::StopEffect(m_body, m_trail);
        m_trail = 0;
    }

    void LaunchEffects::CancelTrail()
    {
        EndTrail();
    }

    void LaunchEffects::NotifyLanding(const Vector3& position, const Vector3& normal)
    {
        if (m_body == nullptr)
        {
            return;
        }
        EndTrail();
        const Vector3 at = position + normal * m_dustRingLift;
        const NS::uint32 dust = NS::Obj::PlayEffect(
            m_body,
            m_launchLandDustAsset,
            PlayAt(at, TurnUpTo(normal), Uniform(m_landDustScale / std::max(m_landDustAssetRadius, NS::k_Epsilon))));
        NS::Obj::SetEffectAmount(m_body, dust, m_landDustScale);
        NS::Obj::StopEffectAfter(m_body, dust, std::max(m_landDustLife, 1));
    }

    NS_CLASS(LaunchEffects)
} // namespace GL::Level
