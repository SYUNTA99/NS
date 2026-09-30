#include "Game/Level/TackleReaction.h"

#include "Game/Level/Breakable.h"
#include "Game/Level/ColliderBounds.h"
#include "Game/Level/ImpactMark.h"
#include "Game/Level/LaunchEffects.h"
#include "Game/Level/LaunchedBody.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/IUseCollision.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

#include <limits>

namespace NS::Game::Level
{
    namespace
    {
        // 飛ぶ向きの軸だけ倍率を効かせた描く形の倍率。衝突は水平でしか起きないので、軸の成分の 2 乗で混ぜる
        [[nodiscard]] NS::Core::Vector3 AlongImpactFactors(const NS::Core::Vector3& impactDir,
                                                           float along,
                                                           float height) noexcept
        {
            const float dx2 = impactDir.x * impactDir.x;
            const float dz2 = impactDir.z * impactDir.z;
            return NS::Core::Vector3{1.0f + (along - 1.0f) * dx2, height, 1.0f + (along - 1.0f) * dz2};
        }

        // 明けを待つフレーム数の上限。当てた側が止めの途中で居なくなっても、縮めた形のまま残らない
        constexpr int k_MaxOverrunFrames = 2;
    } // namespace

    TackleReaction::TackleReaction() noexcept : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate) {}

    bool TackleReaction::Answer(TackleTargetAnswer& outAnswer) const
    {
        const NS::Obj::Actor* owner = Owner();
        if (owner == nullptr || !IsActive())
        {
            return false;
        }
        outAnswer.mass = 1.0f;
        if (const NS::Obj::RigidBody* rigidBody = owner->FindComponent<NS::Obj::RigidBody>())
        {
            outAnswer.mass = rigidBody->EffectiveMass();
        }
        // 耐久を持たない物は壊れない
        outAnswer.toughness = std::numeric_limits<float>::infinity();
        if (const Breakable* breakable = owner->FindComponent<Breakable>())
        {
            outAnswer.toughness = breakable->Toughness();
        }
        const LaunchedBody* launched = owner->FindComponent<LaunchedBody>();
        outAnswer.placed = launched == nullptr || launched->Phase() == LaunchPhase::Resting;
        outAnswer.position = owner->Root().Position();
        // 外接箱は体当たりが調べた体のセンサーの形。体のセンサーが無ければ当たりの形から取る
        if (const NS::Obj::HitSensor* body = owner->FindComponent<NS::Obj::HitSensor>())
        {
            outAnswer.bounds = body->WorldVolume().Bounds();
        }
        else if (!TryGetColliderBounds(*owner, outAnswer.bounds))
        {
            outAnswer.bounds = NS::Core::AABB{outAnswer.position, NS::Core::Vector3{0.5f, 0.5f, 0.5f}};
        }
        return true;
    }

    void TackleReaction::BeginFreeze(const TackleFreezeDesc& desc)
    {
        NS::Obj::Actor* owner = Owner();
        if (owner == nullptr)
        {
            return;
        }
        // 前の止めが残っていれば閉じてから始める
        if (m_frozen)
        {
            EndFreeze();
        }
        const LaunchedBody* launched = owner->FindComponent<LaunchedBody>();
        // 飛んでいる物の根は物理が毎フレーム書くので、食い込み・往復・元位置へ戻す書き込みは効かない。置かれた物に絞る
        m_placed = launched == nullptr || launched->Phase() == LaunchPhase::Resting;
        m_home = owner->Root().Position();
        m_impactDir = desc.impactDir;
        m_pushInDistance = desc.pushInDistance;
        m_shakeAmplitude = desc.shakeAmplitude;
        m_remaining = desc.stopSteps;
        m_total = desc.stopSteps;
        m_overrun = 0;
        m_justBegan = true;
        m_frozen = true;

        if (!m_placed)
        {
            return;
        }
        // 力が伝わった瞬間の絵。飛ぶ向きへ食い込ませて止める
        owner->Root().SetPosition(m_home + m_impactDir * m_pushInDistance);
        if (!desc.squash)
        {
            return;
        }
        // 当たりは根の世界のスケールから形を作るので、根は潰さず描く形だけを縮める
        // 前と今を揃えて書く。描く部品の更新の前後に依らず、元の形から補間せずに縮んだ形で描く
        NS::Obj::MeshRenderer* look = owner->FindComponent<NS::Obj::MeshRenderer>();
        if (look == nullptr)
        {
            return;
        }
        if (!look->SnapDrawScale(AlongImpactFactors(m_impactDir, desc.squashThickness, desc.squashHeight)))
        {
            NS_LOG_WARN(Game,
                        "潰れの厚みか伸び上がりが有限の正でなく、縮めなかった: 厚み {} 伸び上がり {}",
                        desc.squashThickness,
                        desc.squashHeight);
            return;
        }
        m_shapeHeld = true;
    }

    void TackleReaction::OnUpdate()
    {
        if (!m_frozen)
        {
            return;
        }
        // 止めの頭のフレームは食い込んだ位置のまま
        if (m_justBegan)
        {
            m_justBegan = false;
            return;
        }
        if (m_remaining > 0)
        {
            --m_remaining;
        }
        if (m_remaining == 0)
        {
            // 明けは当てた側が知らせる。来なければ数フレームで自分から閉じる
            ++m_overrun;
            if (m_overrun > k_MaxOverrunFrames)
            {
                EndFreeze();
            }
            return;
        }
        if (!m_placed || m_total <= 0 || Owner() == nullptr)
        {
            return;
        }
        // フレーム数の偶奇で往復し、残りフレーム数で減衰する。乱数を使わないので同じ入力は同じ絵になる
        const float sign = 1.0f - 2.0f * static_cast<float>(m_remaining % 2);
        const float decay = static_cast<float>(m_remaining) / static_cast<float>(m_total);
        const float along = m_pushInDistance + m_shakeAmplitude * sign * decay;
        Owner()->Root().SetPosition(m_home + m_impactDir * along);
    }

    void TackleReaction::EndFreeze()
    {
        // 食い込みと往復は見せるための動き。曲線の起点がずれないよう、置かれていた物は元位置へ厳密に戻す
        if (m_frozen && m_placed && Owner() != nullptr)
        {
            Owner()->Root().SetPosition(m_home);
        }
        m_frozen = false;
        RestoreShape();
    }

    void TackleReaction::Release(const TackleReleaseDesc& desc)
    {
        NS::Obj::Actor* owner = Owner();
        if (owner == nullptr)
        {
            return;
        }
        // 元の形で飛ぶ。飛んでいた物は戻さず、今の位置から曲線を引き直す
        EndFreeze();

        // 跡は破壊と押し飛ばしの両方で出す。片方だけ何も残らないと結果が非対称になる
        SpawnMark();

        LaunchedBody* launched = owner->FindComponent<LaunchedBody>();
        if (launched == nullptr)
        {
            return;
        }
        if (desc.breaks)
        {
            launched->Shatter();
            return;
        }
        launched->Launch(desc.arc);
        if (LaunchEffects* effects = owner->FindComponent<LaunchEffects>())
        {
            effects->BeginTrail(desc.tier, desc.power, desc.launchScale, desc.arc.direction);
        }
    }

    void TackleReaction::SpawnMark()
    {
        NS::Obj::Actor* owner = Owner();
        if (owner == nullptr || owner->OwningScene() == nullptr)
        {
            return;
        }
        // 起点は自分の底の下。中心から始めると自分の当たりに 0 距離で当たる
        // 水平は明けのフレームの根の位置。飛んでいた物は止めの間も進んでいて、検知のフレームの位置には居ない
        NS::Core::Vector3 probe = owner->Root().Position();
        NS::Core::AABB bounds{};
        if (TryGetColliderBounds(*owner, bounds))
        {
            // 底から 1cm 下げる。誤差で自分に当たらない最小の隙間
            probe.y = bounds.Center.y - bounds.Extents.y - 0.01f;
        }
        float distance = 0.0f;
        if (!NS::Obj::RaycastCollision(*owner, probe, NS::Core::Vector3{0.0f, -1.0f, 0.0f}, m_markProbeDistance, distance))
        {
            return;
        }
        // 床の上面から 2cm 浮かせる。面がぴったり重なるとちらつく
        const NS::Core::Vector3 at{probe.x, probe.y - distance + 0.02f, probe.z};
        (void)ImpactMark::SpawnAt(owner->OwningScene(), at);
    }

    void TackleReaction::RestoreShape()
    {
        if (!m_shapeHeld)
        {
            return;
        }
        m_shapeHeld = false;
        if (Owner() == nullptr)
        {
            return;
        }
        if (NS::Obj::MeshRenderer* look = Owner()->FindComponent<NS::Obj::MeshRenderer>())
        {
            // 縮めた時と同じく前と今を揃えて書き、縮んだ形から補間しない
            (void)look->SnapDrawScale(NS::Core::Vector3{1.0f, 1.0f, 1.0f});
        }
    }

    void TackleReaction::OnEndPlay()
    {
        RestoreShape();
        m_frozen = false;
    }

    NS_CLASS(TackleReaction)
} // namespace NS::Game::Level
