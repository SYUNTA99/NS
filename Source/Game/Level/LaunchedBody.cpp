#include "Game/Level/LaunchedBody.h"

#include "Game/Level/Breakable.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/Collider.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/RigidBody.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Transform.h"
#include "Runtime/Physics/PhysicsScene.h"
#include "Runtime/Platform/Clock.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace NS::Game::Level
{
    namespace
    {
        [[nodiscard]] bool IsFinite(const NS::Core::Vector3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        [[nodiscard]] bool IsFinitePositive(float value) noexcept
        {
            return std::isfinite(value) && value > 0.0f;
        }

        // 曲線を式で辿るための値。LaunchArc の欄から毎回組み直す
        struct ArcShape
        {
            NS::Core::Vector3 forward{};  // 水平の向き (長さ 1)
            float horizontalSpeed = 0.0f; // 水平の速さ (m/s)
            float riseSpeed = 0.0f;       // 発射の瞬間の上向きの速さ (m/s)
            float riseGravity = 0.0f;     // 上りの重力の大きさ (m/s^2)
            float fallGravity = 0.0f;     // 下りの重力の大きさ (m/s^2)
            float bandSpeed = 0.0f;       // 頂点の帯の縦速度 (m/s)
            float bandScale = 1.0f;       // 頂点の帯の間に重力へ掛ける倍率
        };

        // 上りの帯の外・上りの帯・下りの帯の 3 区間の秒。下りの帯の外は発射の高さを過ぎても落ち続ける
        struct ArcSegments
        {
            float riseOutside = 0.0f;
            float riseBand = 0.0f;
            float fallBand = 0.0f;
        };

        [[nodiscard]] ArcSegments SegmentsOf(const ArcShape& shape) noexcept
        {
            ArcSegments segments;
            float bandEntrySpeed = shape.riseSpeed;
            if (shape.riseSpeed > shape.bandSpeed)
            {
                segments.riseOutside = (shape.riseSpeed - shape.bandSpeed) / shape.riseGravity;
                bandEntrySpeed = shape.bandSpeed;
            }
            segments.riseBand = bandEntrySpeed / (shape.riseGravity * shape.bandScale);
            segments.fallBand = shape.bandSpeed / (shape.fallGravity * shape.bandScale);
            return segments;
        }

        // 発射の高さへ戻るまでの下りの秒
        [[nodiscard]] float FallSecondsOf(const ArcShape& shape, float apexHeight) noexcept
        {
            const float bandGravity = shape.fallGravity * shape.bandScale;
            const float bandDrop = shape.bandSpeed * shape.bandSpeed / (2.0f * bandGravity);
            if (apexHeight <= bandDrop)
            {
                return std::sqrt(2.0f * apexHeight / bandGravity);
            }
            const float landingSpeed =
                std::sqrt(shape.bandSpeed * shape.bandSpeed + 2.0f * shape.fallGravity * (apexHeight - bandDrop));
            return shape.bandSpeed / bandGravity + (landingSpeed - shape.bandSpeed) / shape.fallGravity;
        }

        // arc が曲線にならない値なら false
        [[nodiscard]] bool TryShapeOf(const LaunchArc& arc, ArcShape& outShape) noexcept
        {
            if (!IsFinitePositive(arc.distance) || !IsFinitePositive(arc.apexHeight) ||
                !IsFinitePositive(arc.fallGravityScale) || !IsFinitePositive(arc.apexBandGravityScale) ||
                !std::isfinite(arc.apexBandSpeed) || arc.apexBandSpeed < 0.0f || !IsFinite(arc.direction))
            {
                return false;
            }
            ArcShape shape;
            if (!NS::Core::TryNormalizeHorizontal(arc.direction, shape.forward))
            {
                return false;
            }
            shape.riseGravity = -NS::Phys::k_DefaultGravityY;
            shape.fallGravity = shape.riseGravity * arc.fallGravityScale;
            shape.bandSpeed = arc.apexBandSpeed;
            shape.bandScale = arc.apexBandGravityScale;

            // 帯の中は重力に倍率が掛かるぶん、同じ高さに要る初速が変わる。帯より遅く飛び出すなら上りは全部帯の中
            const float bandSquared = shape.bandSpeed * shape.bandSpeed;
            const float bandRiseHeight = bandSquared / (2.0f * shape.riseGravity * shape.bandScale);
            if (arc.apexHeight <= bandRiseHeight)
            {
                shape.riseSpeed = std::sqrt(2.0f * shape.riseGravity * shape.bandScale * arc.apexHeight);
            }
            else
            {
                shape.riseSpeed = std::sqrt(2.0f * shape.riseGravity * arc.apexHeight -
                                            bandSquared * (1.0f / shape.bandScale - 1.0f));
            }

            const ArcSegments segments = SegmentsOf(shape);
            const float flightSeconds = segments.riseOutside + segments.riseBand + FallSecondsOf(shape, arc.apexHeight);
            shape.horizontalSpeed = arc.distance / flightSeconds;
            if (!std::isfinite(shape.horizontalSpeed) || !std::isfinite(shape.riseSpeed))
            {
                return false;
            }
            outShape = shape;
            return true;
        }

        // 発射から seconds 秒後の、起点から見た位置
        [[nodiscard]] NS::Core::Vector3 ArcOffsetAt(const ArcShape& shape, float seconds) noexcept
        {
            const ArcSegments segments = SegmentsOf(shape);
            const NS::Core::Vector3 horizontal = shape.forward * (shape.horizontalSpeed * seconds);

            float t = seconds;
            if (t <= segments.riseOutside)
            {
                const float height = shape.riseSpeed * t - 0.5f * shape.riseGravity * t * t;
                return NS::Core::Vector3{horizontal.x, height, horizontal.z};
            }
            float height = shape.riseSpeed * segments.riseOutside -
                           0.5f * shape.riseGravity * segments.riseOutside * segments.riseOutside;
            const float bandEntrySpeed = std::min(shape.riseSpeed, shape.bandSpeed);
            t -= segments.riseOutside;

            const float riseBandGravity = shape.riseGravity * shape.bandScale;
            if (t <= segments.riseBand)
            {
                height += bandEntrySpeed * t - 0.5f * riseBandGravity * t * t;
                return NS::Core::Vector3{horizontal.x, height, horizontal.z};
            }
            height +=
                bandEntrySpeed * segments.riseBand - 0.5f * riseBandGravity * segments.riseBand * segments.riseBand;
            t -= segments.riseBand;

            const float fallBandGravity = shape.fallGravity * shape.bandScale;
            if (t <= segments.fallBand)
            {
                height -= 0.5f * fallBandGravity * t * t;
                return NS::Core::Vector3{horizontal.x, height, horizontal.z};
            }
            height -= 0.5f * fallBandGravity * segments.fallBand * segments.fallBand;
            t -= segments.fallBand;

            height -= shape.bandSpeed * t + 0.5f * shape.fallGravity * t * t;
            return NS::Core::Vector3{horizontal.x, height, horizontal.z};
        }
    } // namespace

    NS::Core::Vector3 LaunchArcInitialVelocity(const LaunchArc& arc) noexcept
    {
        ArcShape shape;
        if (!TryShapeOf(arc, shape))
        {
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        }
        const NS::Core::Vector3 horizontal = shape.forward * shape.horizontalSpeed;
        return NS::Core::Vector3{horizontal.x, shape.riseSpeed, horizontal.z};
    }

    LaunchedBody::LaunchedBody() noexcept : NS::Obj::Component(NS::Obj::TickPriority::LateUpdate) {}

    NS::Core::Vector3 LaunchedBody::TumbleFrom(const NS::Core::Vector3& velocity) const noexcept
    {
        NS::Core::Vector3 forward{};
        if (!NS::Core::TryNormalizeHorizontal(velocity, forward))
        {
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        }
        const float horizontal = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
        const float rate = m_spinPerSpeed * horizontal;
        if (!std::isfinite(rate))
        {
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        }

        // 軸は上向きと進む向きの外積。正の角度で上面が進行方向へ倒れる前転になる
        const NS::Core::Vector3 axis{forward.z, 0.0f, -forward.x};
        return axis * rate;
    }

    NS::Core::Vector3 LaunchedBody::Velocity() const
    {
        if (m_phase == LaunchPhase::Resting || Owner() == nullptr)
        {
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        }
        // body の速度は次のフレームのために書いた後の値。1 フレーム先の速度を返さないよう、直近の物理が使った控えを返す
        if (m_phase == LaunchPhase::Arc)
        {
            return m_arcVelocityUsed;
        }
        const NS::Obj::RigidBody* rigidBody = Owner()->FindComponent<NS::Obj::RigidBody>();
        if (rigidBody == nullptr)
            return NS::Core::Vector3{0.0f, 0.0f, 0.0f};
        return rigidBody->Velocity();
    }

    void LaunchedBody::SetRestLifeSeconds(float seconds) noexcept
    {
        if (!std::isfinite(seconds) || seconds < 0.0f)
            return;
        m_restLifeSeconds = seconds;
    }

    void LaunchedBody::SetDebrisCount(int count) noexcept
    {
        m_debrisCount = std::max(0, count);
    }

    NS::Obj::RigidBody* LaunchedBody::EnsureRigidBody()
    {
        if (Owner() == nullptr)
            return nullptr;
        if (NS::Obj::RigidBody* found = Owner()->FindComponent<NS::Obj::RigidBody>())
            return found;
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return nullptr;

        // 積み忘れた配置物でも飛ばせるよう、置かれた姿のままのキネマティックで足す
        NS::Obj::RigidBody* added = Owner()->AddComponent<NS::Obj::RigidBody>();
        added->SetKinematic(true);
        // collider に自分の静的な body を外させてから形を集める。残すと同じ場所に静的と動く body が二重に立つ
        for (NS::Obj::Component* comp : Owner()->Components())
        {
            NS::Obj::Collider* collider = NS::Obj::ComponentCast<NS::Obj::Collider>(comp);
            if (collider != nullptr && collider->IsActive())
                collider->SyncToPhysics(scene->Physics());
        }
        added->SyncToPhysics(scene->Physics());
        return added;
    }

    void LaunchedBody::Launch(const LaunchArc& arc)
    {
        // 距離や高さが壊れた値の曲線は位置へ流れ、配置物が二度と描かれない場所へ飛ぶ
        ArcShape shape;
        if (!TryShapeOf(arc, shape))
        {
            return;
        }
        NS::Obj::RigidBody* rigidBody = EnsureRigidBody();
        if (rigidBody == nullptr || rigidBody->BodyId().IsInvalid())
        {
            return;
        }

        // 起点は根。止めの間に body だけ食い込みの側へ運ばれていても、欄の距離を根から測る
        const NS::Core::AffineDecomposition pose = NS::Core::DecomposeAffine(RootTransform().WorldMatrix());
        rigidBody->Teleport(pose.translation, pose.rotation);

        // 飛んでいる最中に引き直す時は、切った後の値を控えない
        if (m_phase != LaunchPhase::Arc)
        {
            m_savedUseGravity = rigidBody->UsesGravity();
            m_savedLinearDamping = rigidBody->LinearDamping();
            m_savedAngularDamping = rigidBody->AngularDamping();
        }
        // 重力と減衰を切ると、書いた速度のまま進んで位置が曲線の点を辿る
        rigidBody->SetUseGravity(false);
        rigidBody->SetLinearDamping(0.0f);
        rigidBody->SetAngularDamping(0.0f);
        m_phase = LaunchPhase::Arc;
        m_arc = arc;
        m_arcFrames = 0;
        m_restAge = 0.0f;

        // ダイナミックへの切り替えは次の PrePhysicsStep を待たずに body へ入れ、欄と body の運動の種類を飛ばしたフレームのうちに揃える
        rigidBody->SetKinematic(false);
        rigidBody->RefreshMotion();
        WriteArcVelocity(*rigidBody, NS::Platform::FrameTimer::FixedDelta());
        m_arcVelocityUsed = m_arcVelocity;
    }

    void LaunchedBody::LaunchRigid(const NS::Core::Vector3& velocity)
    {
        // 非有限値は位置へ流れ、配置物が二度と描かれない場所へ飛ぶ
        if (!IsFinite(velocity))
        {
            return;
        }
        NS::Obj::RigidBody* rigidBody = EnsureRigidBody();
        if (rigidBody == nullptr || rigidBody->BodyId().IsInvalid())
        {
            return;
        }
        if (m_phase == LaunchPhase::Arc)
        {
            RestoreArcFields(*rigidBody);
        }

        // ダイナミックへの切り替えは次の PrePhysicsStep を待たずに body へ入れ、欄と body の運動の種類を飛ばしたフレームのうちに揃える
        rigidBody->SetKinematic(false);
        rigidBody->RefreshMotion();
        rigidBody->SetVelocity(velocity);
        rigidBody->SetAngularVelocity(TumbleFrom(velocity));
        m_phase = LaunchPhase::Rigid;
        m_restAge = 0.0f;
    }

    void LaunchedBody::WriteArcVelocity(NS::Obj::RigidBody& rigidBody, float dt)
    {
        ArcShape shape;
        if (!TryShapeOf(m_arc, shape) || !(dt > 0.0f))
        {
            return;
        }
        // 瞬間の速度でなく 1 フレームの変位を書く。瞬間の速度を積むと位置が曲線より上へずれていく
        const NS::Core::Vector3 from = ArcOffsetAt(shape, static_cast<float>(m_arcFrames) * dt);
        const NS::Core::Vector3 to = ArcOffsetAt(shape, static_cast<float>(m_arcFrames + 1) * dt);
        m_arcVelocity = (to - from) / dt;
        rigidBody.SetVelocity(m_arcVelocity);
        // 回る速さも毎フレーム書き直す。近づく向きでない接触では曲線を続けるので、そこで変わった回り方を曲線の値へ戻す
        rigidBody.SetAngularVelocity(TumbleFrom(m_arcVelocity));
        ++m_arcFrames;
    }

    void LaunchedBody::RestoreArcFields(NS::Obj::RigidBody& rigidBody) noexcept
    {
        rigidBody.SetUseGravity(m_savedUseGravity);
        rigidBody.SetLinearDamping(m_savedLinearDamping);
        rigidBody.SetAngularDamping(m_savedAngularDamping);
    }

    void LaunchedBody::ComeToRest()
    {
        m_phase = LaunchPhase::Resting;
        m_restAge = 0.0f;
        NS::Obj::RigidBody* rigidBody = Owner()->FindComponent<NS::Obj::RigidBody>();
        if (rigidBody == nullptr)
            return;
        // 残った速度はキネマティックの運びに混ざる。止まった所へ置いたまま動かさない
        rigidBody->SetVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        rigidBody->SetAngularVelocity(NS::Core::Vector3{0.0f, 0.0f, 0.0f});
        rigidBody->SetKinematic(true);
        rigidBody->RefreshMotion();
    }

    void LaunchedBody::HideAndSleep()
    {
        const LaunchPhase phase = m_phase;
        m_phase = LaunchPhase::Resting;
        if (NS::Obj::MeshRenderer* mesh = Owner()->FindComponent<NS::Obj::MeshRenderer>())
            mesh->SetActive(false);

        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (NS::Obj::RigidBody* rigidBody = Owner()->FindComponent<NS::Obj::RigidBody>())
        {
            // 曲線のために切った欄を残すと、配置物の欄が切った値のまま見える
            if (phase == LaunchPhase::Arc)
            {
                RestoreArcFields(*rigidBody);
            }
            if (scene != nullptr)
                rigidBody->RemoveFromPhysics(scene->Physics());
            rigidBody->SetActive(false);
        }
        // RigidBody を止めると collider が自分の body を持ち直せる。止めて外し、当たりを残さない
        for (NS::Obj::Component* comp : Owner()->Components())
        {
            NS::Obj::Collider* collider = NS::Obj::ComponentCast<NS::Obj::Collider>(comp);
            if (collider == nullptr)
                continue;
            collider->SetActive(false);
            if (scene != nullptr)
                collider->RemoveFromPhysics(scene->Physics());
        }
        SetActive(false);
    }

    void LaunchedBody::Shatter()
    {
        if (Owner() == nullptr)
            return;
        NS::Obj::Scene* scene = Owner()->OwningScene();
        if (scene == nullptr)
            return;

        const NS::Core::Vector3 origin = RootTransform().Position();
        const NS::Obj::RigidBody* source = Owner()->FindComponent<NS::Obj::RigidBody>();
        float mass = 1.0f;
        if (source != nullptr)
        {
            mass = source->EffectiveMass();
        }
        Breakable* breakable = Owner()->FindComponent<Breakable>();
        if (breakable != nullptr)
        {
            breakable->SetActive(false);
        }
        // 破片より先に自分の当たりを外す。残すと同じ場所に湧いた破片が押し出されて勢いが読めなくなる
        HideAndSleep();

        // 重い物ほど破片が飛ばない。押し飛ばしと同じ向きの質量感を破片でも見せる
        const float speed = m_debrisSpeed / mass;
        for (int i = 0; i < m_debrisCount; ++i)
        {
            const float angle = 2.0f * NS::Core::k_Pi * static_cast<float>(i) / static_cast<float>(m_debrisCount);
            // 浮きは交互に変える。全部同じ高さだと 1 つの輪に見えて壊れた量が伝わらない
            const float up = 0.5f + 0.5f * static_cast<float>(i % 2);
            std::unique_ptr<NS::Obj::GameObject> owned = std::make_unique<NS::Obj::GameObject>();
            owned->Root().SetPosition(origin);
            owned->Root().SetScale(NS::Core::Vector3{m_debrisScale, m_debrisScale, m_debrisScale});
            NS::Obj::MeshRenderer* mesh = owned->AddComponent<NS::Obj::MeshRenderer>();
            mesh->SetMeshRef("cube");
            mesh->SetMaterialRef("player");
            mesh->SetBaseColor(m_debrisBaseColor);
            owned->AddComponent<NS::Obj::BoxCollider>();
            NS::Obj::RigidBody* debrisBody = owned->AddComponent<NS::Obj::RigidBody>();
            // 破片同士は当たらない種別に置く。散った破片が互いを押し合うと元の勢いが読めなくなる
            debrisBody->SetObjectLayer(NS::Phys::ObjectLayers::Debris);
            // 面の手触りは壊れた物と揃える
            if (source != nullptr)
            {
                debrisBody->SetFriction(source->Friction());
                debrisBody->SetRestitution(source->Restitution());
            }
            owned->AddComponent<LaunchedBody>();
            NS::Obj::GameObject* spawned = scene->SpawnTransient(std::move(owned));
            if (spawned == nullptr)
                continue;
            LaunchedBody* body = spawned->FindComponent<LaunchedBody>();
            if (body == nullptr)
                continue;
            // 破片だけ寿命を持つ。壊すたびに増えるので、止まったら消さないと世界に積み上がり続ける
            body->SetRestLifeSeconds(m_debrisLifeSeconds);
            // 破片からは破片を出さない。今は破片が壊れる道は無い
            // 0 にしておかないと、破片に Breakable を足した時に壊すたびに破片を撒く
            body->SetDebrisCount(0);
            body->LaunchRigid(NS::Core::Vector3{std::cos(angle) * speed, up * speed, std::sin(angle) * speed});
        }
    }

    void LaunchedBody::OnUpdate()
    {
        const float dt = NS::Platform::FrameTimer::FixedDelta();
        if (m_phase == LaunchPhase::Resting)
        {
            // 0 は消えない指定。押し飛ばしただけの配置物は場に残す
            if (m_restLifeSeconds <= 0.0f)
                return;
            m_restAge += dt;
            if (m_restAge < m_restLifeSeconds)
                return;
            HideAndSleep();
            return;
        }

        // 姿勢の書き戻しは RigidBody が物理の直後に済ませている。ここは曲線の速度と接触と眠りを見る
        NS::Obj::RigidBody* rigidBody = Owner()->FindComponent<NS::Obj::RigidBody>();
        if (rigidBody == nullptr)
        {
            m_phase = LaunchPhase::Resting;
            return;
        }

        if (m_phase == LaunchPhase::Arc)
        {
            // 直近の物理は、前に書いた速度で動いて接触を解いた。近づく向きかもこの速度で見る
            // 物理の後の body の速度は接触を解いた後の値で、近づく向きの成分が既に消えている
            m_arcVelocityUsed = m_arcVelocity;
            // TODO: 連続衝突判定の掃引は 1 フレームの移動が内接球の半径の 0.75 倍を超える時だけ
            // 遅い着地は床へ沈んだ次のフレームに渡る。同梱の球で最大 0.23 m 沈む。消すなら PhysicsSettings の
            // mLinearCastThreshold を下げる
            for (const NS::Phys::BodyContact& contact : rigidBody->Contacts())
            {
                // 離れる向きの接触では渡さない。床に接して置かれた物は、飛び出したフレームにも床との接触が出る
                if (NS::Core::Dot(contact.normal, m_arcVelocityUsed) >= 0.0f)
                {
                    continue;
                }
                // 渡す時は速度も角速度も書き換えない。接触を解いた後の速度がそのまま剛体の最初の速度になる
                RestoreArcFields(*rigidBody);
                m_phase = LaunchPhase::Rigid;
                return;
            }
            WriteArcVelocity(*rigidBody, dt);
            return;
        }

        // 止まったかを決めるのは Jolt の睡眠。速度のしきい値を自分で持つと 2 か所で止まりを判断することになる
        if (rigidBody->IsSleeping())
        {
            ComeToRest();
        }
    }

    void LaunchedBody::OnEndPlay()
    {
        // body は RigidBody が自分で外す。曲線のために切った欄は戻す
        if (m_phase == LaunchPhase::Arc && Owner() != nullptr)
        {
            if (NS::Obj::RigidBody* rigidBody = Owner()->FindComponent<NS::Obj::RigidBody>())
            {
                RestoreArcFields(*rigidBody);
            }
        }
        m_phase = LaunchPhase::Resting;
    }

    NS_CLASS(LaunchedBody)
} // namespace NS::Game::Level
