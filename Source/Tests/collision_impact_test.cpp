#include "Editor/EditorObjects.h"
#include "Game/Player.h"
#include "camera_screen.h"
#include "tuning_field_access.h"

#include "hit_zones_entry.h"
#include <Game/Level/Breakable.h>
#include <Game/Level/ColliderBounds.h>
#include <Game/Level/CollisionInput.h>
#include <Game/Level/HitTier.h>
#include <Game/Level/ImpactInputJudge.h>
#include <Game/Level/ImpactMark.h>
#include <Game/Level/ImpactResolver.h>
#include <Game/Level/LaunchedBody.h>
#include <Game/Player/PlayerAppearance.h>
#include <Game/Player/PlayerComponent.h>
#include <Runtime/Core/AABB.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Core/Sphere.h>
#include <Runtime/Object/Components/BoxCollider.h>
#include <Runtime/Object/Components/CameraBrain.h>
#include <Runtime/Object/Components/MeshRenderer.h>
#include <Runtime/Object/Components/PlayerInput.h>
#include <Runtime/Object/Components/RigidBody.h>
#include <Runtime/Object/Components/SphereCollider.h>
#include <Runtime/Object/Components/TransformComponent.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <Runtime/Object/Reflection/TypeRegistry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Physics/PhysicsScene.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Platform/Input.h>
#include <Runtime/Platform/Mouse.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace LevelNs = NS::Game::Level;
namespace SceneNs = NS::Obj;

namespace
{
    using NS::Core::Vector3;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    constexpr float k_RunSpeed = 8.0f;
    constexpr float k_FastEntrySpeed = 16.0f;
    constexpr float k_SlamSpeed = 20.0f;
    constexpr float k_TapSlamSpeed = 10.0f;
    // 質量 1 に威力 1 で当てた時の曲線。欄「押し飛ばしの距離」「押し飛ばしの高さ」の既定
    constexpr float k_LaunchDistance = 29.0f;
    constexpr float k_LaunchApexHeight = 2.0f;
    // 質量 1 に威力 1 で当てた時の自機の反動。欄「反動の高さ」「反動の距離」の既定
    constexpr float k_ReboundApexHeight = 1.15f;
    constexpr float k_ReboundDistance = 0.575f;
    // 欄「中心近くの当たりの反動の距離の倍率」の既定
    constexpr float k_CenterHitReboundDistanceScale = 2.0f;
    // 破壊は耐久 ≤ 最終威力なので、反発と押し飛ばしを見る台は壊れない高さを既定にする
    constexpr float k_UnbreakableToughness = 99.0f;
    // 逆転を見る台の耐久。満溜め + 中心直撃の 2.0 と、素当て + 縁寄りの 0.85 の間に置く
    constexpr float k_ReversalToughness = 1.7f;

    struct Rig
    {
        NS::Game::Player::PlayerComponent* movement = nullptr;
        LevelNs::ImpactResolver* impact = nullptr;
        LevelNs::CollisionInput* input = nullptr;
        SceneNs::BoxCollider* targetBox = nullptr;
        NS::Obj::GameObject* target = nullptr;
        LevelNs::Breakable* breakable = nullptr;
        SceneNs::RigidBody* rigidBody = nullptr;
    };

    struct SlamCourse
    {
        float start = 0.0f;
        // 的は動かないので、湧き位置のずらしがそのまま当たりの横ずれになる
        float lateral = 0.0f;
        std::int16_t targetCell = 1;
        bool withBreakable = true;
        bool withCollisionInput = true;
        bool floorUnderTarget = true;
        bool alongZ = false;
        bool sphereTarget = false;
        // 的を置く段。1 は床の上に接して置き、2 は床から 1 m 浮かせる
        std::int16_t targetLayer = 1;
        // 0 以外なら、この列にもう 1 体の壊せる的を同じ段に置く
        std::int16_t extraTargetCell = 0;
        // 2 体目の的を置く行。0 は的と同じ行
        std::int16_t extraTargetLane = 0;
        // 偽なら的から MeshRenderer を外し、描く形の無い的にする
        bool targetWithMeshRenderer = true;
        // 偽なら的に段の範囲 (HitZones) を付けない。壊せる物でも体当たりの相手にならない
        bool targetWithHitZones = true;
    };

    // 置かれた壊せる物と同じ RigidBody。飛ぶまではキネマティックで、面の手触りは押し飛ばしを調整した値
    nlohmann::json MakeLaunchableRigidBodyEntry()
    {
        nlohmann::json entry = SceneNs::MakeComponentEntry("RigidBody");
        SceneNs::SetField(entry, "キネマティック", true);
        SceneNs::SetField(entry, "摩擦", 0.6f);
        SceneNs::SetField(entry, "跳ね返り", 0.35f);
        // 着地した後に転がって止まる減りは同梱の球と同じ
        SceneNs::SetField(entry, "移動の減衰", 3.0f);
        SceneNs::SetField(entry, "回転の減衰", 3.0f);
        // 速い着地の接触を床の面で作る。偽だと床へ沈んだ次のフレームに剛体へ渡る
        SceneNs::SetField(entry, "連続衝突判定", true);
        return entry;
    }

    Rig BuildSlam(SceneNs::Scene& scene, const SlamCourse& course)
    {
        NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);

        nlohmann::json data = SceneNs::MakeSceneJson();
        Vector3 spawn{course.start, 1.41f, course.lateral};
        if (course.alongZ)
            spawn = Vector3{course.lateral, 1.41f, course.start};
        nlohmann::json player = MakePlayerObject(spawn, NS::Core::Quaternion{});
        SceneNs::ObjectJsonComponents(player).push_back(SceneNs::MakeComponentEntry("ImpactResolver"));
        if (course.withCollisionInput)
            SceneNs::ObjectJsonComponents(player).push_back(SceneNs::MakeComponentEntry("CollisionInput"));
        SceneNs::SceneJsonObjects(data).push_back(player);

        // カプセル半径 0.4 の自機を横へずらすと床 1 列からはみ出すので、ずらす側にもう 1 列敷く
        std::int16_t lateralCell = 0;
        if (course.lateral > 0.0f)
            lateralCell = 1;
        if (course.lateral < 0.0f)
            lateralCell = -1;
        for (std::int16_t i = -3; i <= static_cast<std::int16_t>(course.targetCell + 3); ++i)
        {
            if (!course.floorUnderTarget && i == course.targetCell)
                continue;
            if (course.alongZ)
            {
                SceneNs::SceneJsonObjects(data).push_back(NS::Editor::MakeCellObject(0, 0, i));
                if (lateralCell != 0)
                    SceneNs::SceneJsonObjects(data).push_back(NS::Editor::MakeCellObject(lateralCell, 0, i));
            }
            else
            {
                SceneNs::SceneJsonObjects(data).push_back(NS::Editor::MakeCellObject(i, 0, 0));
                if (lateralCell != 0)
                    SceneNs::SceneJsonObjects(data).push_back(NS::Editor::MakeCellObject(i, 0, lateralCell));
            }
        }

        if (course.extraTargetCell != 0)
        {
            nlohmann::json extra =
                NS::Editor::MakeCellObject(course.extraTargetCell, course.targetLayer, course.extraTargetLane);
            SceneNs::ObjectJsonComponents(extra).push_back(MakeLaunchableRigidBodyEntry());
            SceneNs::ObjectJsonComponents(extra).push_back(SceneNs::MakeComponentEntry("Breakable"));
            SceneNs::ObjectJsonComponents(extra).push_back(NsTest::MakeTestHitZonesEntry());
            SceneNs::SceneJsonObjects(data).push_back(extra);
        }

        nlohmann::json target = NS::Editor::MakeCellObject(course.targetCell, course.targetLayer, 0);
        if (course.alongZ)
            target = NS::Editor::MakeCellObject(0, course.targetLayer, course.targetCell);
        if (course.sphereTarget)
        {
            for (nlohmann::json& entry : SceneNs::ObjectJsonComponents(target))
            {
                if (SceneNs::ComponentEntryType(entry) != "BoxCollider")
                    continue;
                entry = SceneNs::MakeComponentEntry("SphereCollider");
                SceneNs::SetField(entry, "半径", 0.5f);
            }
        }
        if (course.withBreakable)
        {
            SceneNs::ObjectJsonComponents(target).push_back(MakeLaunchableRigidBodyEntry());
            SceneNs::ObjectJsonComponents(target).push_back(SceneNs::MakeComponentEntry("Breakable"));
            if (course.targetWithHitZones)
            {
                SceneNs::ObjectJsonComponents(target).push_back(NsTest::MakeTestHitZonesEntry());
            }
        }
        if (!course.targetWithMeshRenderer)
        {
            nlohmann::json& components = SceneNs::ObjectJsonComponents(target);
            for (std::size_t i = components.size(); i > 0; --i)
            {
                if (SceneNs::ComponentEntryType(components[i - 1]) == "MeshRenderer")
                {
                    components.erase(i - 1);
                }
            }
        }
        SceneNs::SceneJsonObjects(data).push_back(target);
        scene.LoadJson(std::move(data));

        Rig rig;
        Player* live = FindPlayer(scene.Objects());
        EXPECT_NE(live, nullptr);
        if (live != nullptr)
        {
            rig.movement = live->FindComponent<NS::Game::Player::PlayerComponent>();
            rig.impact = live->FindComponent<LevelNs::ImpactResolver>();
            rig.input = live->FindComponent<LevelNs::CollisionInput>();
            // 起こしたままだと実機の入力が毎フレーム 0 を書き込むため、走行入力と向きが検証台から消える
            if (SceneNs::PlayerInput* input = live->FindComponent<SceneNs::PlayerInput>())
                input->SetActive(false);
        }
        scene.Objects().ForEachComponent<LevelNs::Breakable>(
            [&rig](LevelNs::Breakable& breakable) { rig.breakable = &breakable; });
        if (rig.breakable != nullptr)
        {
            rig.breakable->SetToughness(k_UnbreakableToughness);
            rig.target = rig.breakable->Owner();
            rig.targetBox = rig.breakable->Owner()->FindComponent<SceneNs::BoxCollider>();
        }
        if (rig.targetBox == nullptr)
        {
            scene.Objects().ForEachComponent<SceneNs::BoxCollider>([&rig](SceneNs::BoxCollider& box) {
                if (box.Owner()->Root().Position().y > 0.9f)
                    rig.targetBox = &box;
            });
        }
        if (rig.target == nullptr && rig.targetBox != nullptr)
            rig.target = rig.targetBox->Owner();
        if (rig.target != nullptr)
            rig.rigidBody = rig.target->FindComponent<SceneNs::RigidBody>();
        return rig;
    }

    LevelNs::LaunchedBody* HitBody(const Rig& rig)
    {
        if (rig.target == nullptr)
            return nullptr;
        return rig.target->FindComponent<LevelNs::LaunchedBody>();
    }

    // 帯の範囲は半開なので Update (200) の移動は入らない。押し飛ばされた物と破片を動かさずに済む
    void StepWorld(SceneNs::Scene& scene)
    {
        scene.Objects().UpdateObjects(SceneNs::TickPriority::EarlyUpdate, SceneNs::TickPriority::Update);
    }

    void Step(SceneNs::Scene& scene, const Rig& rig)
    {
        StepWorld(scene);
        if (rig.movement != nullptr)
            rig.movement->OnUpdate();
    }

    [[nodiscard]] float HorizontalSpeed(const Vector3& v) noexcept
    {
        return std::sqrt(v.x * v.x + v.z * v.z);
    }

    [[nodiscard]] float Speed(const Vector3& v) noexcept
    {
        return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    }

    void SetFloatField(SceneNs::Component& comp, std::string_view label, float value)
    {
        const SceneNs::FieldDesc* field = SceneNs::FindField(comp.GetReflection(), label);
        ASSERT_NE(field, nullptr);
        field->set(&comp, &value);
    }

    void SetBoolField(SceneNs::Component& comp, std::string_view label, bool value)
    {
        const SceneNs::FieldDesc* field = SceneNs::FindField(comp.GetReflection(), label);
        ASSERT_NE(field, nullptr);
        field->set(&comp, &value);
    }

    // ヒットストップを 0 にして、衝突の結果をそのフレームのうちに適用させる
    void SetInstantImpact(Rig& rig)
    {
        SetFloatField(*rig.impact, "ヒットストップ基準秒", 0.0f);
    }

    // 破壊は既定で止まっている。壊れる側を見る検証台は欄を立ててから当てる
    void EnableBreak(const Rig& rig)
    {
        SetBoolField(*rig.impact, "破壊を許可", true);
    }

    // 空中でも出せるが、落下が混ざると当たるフレームが揺れる。先に床へ着けて接地からの発動に揃える
    void SettleOnFloor(SceneNs::Scene& scene, const Rig& rig)
    {
        for (int i = 0; i < 30 && !rig.movement->IsGrounded(); ++i)
            Step(scene, rig);
    }

    void BeginSlam(SceneNs::Scene& scene, const Rig& rig, float entrySpeed, float charge01, bool alongZ = false)
    {
        SettleOnFloor(scene, rig);
        float axisSign = 1.0f;
        if (entrySpeed < 0.0f)
            axisSign = -1.0f;
        Vector3 velocity{entrySpeed, 0.0f, 0.0f};
        Vector3 aim{axisSign, 0.0f, 0.0f};
        if (alongZ)
        {
            velocity = Vector3{0.0f, 0.0f, entrySpeed};
            aim = Vector3{0.0f, 0.0f, axisSign};
        }
        rig.movement->SetVelocity(velocity);
        // 向きの解決は入力が最優先。入力なしだとカメラの前へ逸れるので、走りながら押した状況を入力で再現する
        rig.movement->SetDesiredMove(aim, 0.0f);
        rig.movement->RequestBodySlam(charge01);
        Step(scene, rig);
    }

    // 裁定が書いた速度をそのまま読むため、裁定が起きたフレームは移動を走らせずに返す
    int StepUntilImpact(SceneNs::Scene& scene, const Rig& rig, int maxSteps)
    {
        for (int i = 0; i < maxSteps; ++i)
        {
            StepWorld(scene);
            if (rig.impact->DidRebound() || rig.impact->DidBreak())
                return i + 1;
            rig.movement->OnUpdate();
        }
        return maxSteps;
    }

    // 解放が書いた速度をそのまま読むため、移動が起きたフレームは移動を走らせずに返す
    int StepsUntilMovementActive(SceneNs::Scene& scene, const Rig& rig, int maxSteps)
    {
        for (int i = 0; i < maxSteps; ++i)
        {
            StepWorld(scene);
            if (rig.movement->IsActiveSelf())
                return i + 1;
            rig.movement->OnUpdate();
        }
        return maxSteps;
    }

    // 助走の長さだけが k_NearCourse と違う
    constexpr SlamCourse k_FarCourse{.start = -0.5f, .targetCell = 6};
    constexpr SlamCourse k_NearCourse{.start = 0.0f, .targetCell = 1};
    // 横ずれ 0.45 ÷ (的の半幅 0.5 + 自機の半径 0.4) = 0.5 で係数 0.85。段は惜しいで、中心近くの当たりにならない
    constexpr SlamCourse k_EdgeCourse{.start = 0.0f, .lateral = 0.45f, .targetCell = 1};

    // 飛んで着地して滑り切るまでの道。狭いと端から落ちて停止の検証にならない
    constexpr std::int16_t k_FloorFirstX = -2;
    constexpr std::int16_t k_FloorLastX = 8;
    // 曲線で飛ばす台の床の最後の列。距離 10 の着地と、箱が摩擦で滑る 9 m が収まる 30 マス
    constexpr std::int16_t k_LongFloorLastX = 27;
    // 溜めきりの曲線の 58 m と、転がる距離の線 58 m が収まる 120 マス。線を超えて転がれば床の端から落ちて止まらない
    constexpr std::int16_t k_RollFloorLastX = 117;
    // 剛体へ渡してから止まるまでの線 (フレーム)
    constexpr int k_RestLineFrames = 240;
    constexpr float k_BodyRestY = 1.0f; // 床の上面 0.5 に半分の高さ 0.5 を足した静止の高さ
    // 球の的は同梱の球と同じ大きさ。半径 0.5 の球をスケール 1.5 で置き、世界の半径は 0.75
    constexpr float k_SphereScale = 1.5f;
    constexpr float k_SphereRestY = 1.25f; // 床の上面 0.5 に世界の半径 0.75 を足した静止の高さ
    constexpr std::int16_t k_WallX = 2;
    constexpr float k_LaunchGravity = -25.0f;
    constexpr int k_RestStepLimit = 600;

    struct BodyCourse
    {
        bool withWall = false;
        // 外すと RigidBody を積み忘れた配置物になる
        bool withRigidBody = true;
        std::int16_t floorLastX = k_FloorLastX;
        // 真なら的を 1 m の箱でなく球にする。箱は回りながら着地すると角の接触で角速度が変わる
        bool sphereTarget = false;
    };

    // 距離 10・高さ 2 の曲線。下りは上りの 1.4 倍の重力で、縦の速さが 1 m/s 未満の間は重力を半分にする
    const LevelNs::LaunchArc k_TestArc{.direction = Vector3{1.0f, 0.0f, 0.0f},
                                       .distance = 10.0f,
                                       .apexHeight = 2.0f,
                                       .fallGravityScale = 1.4f,
                                       .apexBandSpeed = 1.0f,
                                       .apexBandGravityScale = 0.5f};

    // 質量 1 の球に溜めきりで中心へ当てた時の曲線。距離と高さは押し飛ばしの欄の既定の 2 倍
    const LevelNs::LaunchArc k_FullChargeArc{.direction = Vector3{1.0f, 0.0f, 0.0f},
                                             .distance = 58.0f,
                                             .apexHeight = 4.0f,
                                             .fallGravityScale = 1.4f,
                                             .apexBandSpeed = 1.0f,
                                             .apexBandGravityScale = 0.5f};

    struct BodyRig
    {
        SceneNs::GameObject* object = nullptr;
        LevelNs::LaunchedBody* body = nullptr;
        SceneNs::BoxCollider* box = nullptr;
        SceneNs::RigidBody* rigidBody = nullptr;
        JPH::uint restingBodies = 0;
    };

    // 長さを合わせた 1 枚の床の上へ、飛ばされる物を 1 個置く検証台。自機は要らない
    BodyRig BuildBody(SceneNs::Scene& scene, const BodyCourse& course = {})
    {
        NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);

        nlohmann::json data = SceneNs::MakeSceneJson();
        // 床は 1 枚の箱。1 m の箱を並べると、着地で沈んだ球が継ぎ目の角に当たって斜めの法線で止められる
        nlohmann::json floor = NS::Editor::MakeCellObject(0, 0, 0);
        const float floorCells = static_cast<float>(course.floorLastX - k_FloorFirstX + 1);
        const float floorCenterX = 0.5f * static_cast<float>(k_FloorFirstX + course.floorLastX);
        SceneNs::SetObjectPosition(floor, Vector3{floorCenterX, 0.0f, 0.0f});
        SceneNs::SetObjectScale(floor, Vector3{floorCells, 1.0f, 1.0f});
        SceneNs::SceneJsonObjects(data).push_back(floor);
        if (course.withWall)
        {
            SceneNs::SceneJsonObjects(data).push_back(NS::Editor::MakeCellObject(k_WallX, 1, 0));
        }

        nlohmann::json target = NS::Editor::MakeCellObject(0, 1, 0);
        if (course.sphereTarget)
        {
            for (nlohmann::json& entry : SceneNs::ObjectJsonComponents(target))
            {
                if (SceneNs::ComponentEntryType(entry) != "BoxCollider")
                {
                    continue;
                }
                entry = SceneNs::MakeComponentEntry("SphereCollider");
                SceneNs::SetField(entry, "半径", 0.5f);
            }
            SceneNs::SetObjectPosition(target, Vector3{0.0f, k_SphereRestY, 0.0f});
            SceneNs::SetObjectScale(target, Vector3{k_SphereScale, k_SphereScale, k_SphereScale});
        }
        if (course.withRigidBody)
            SceneNs::ObjectJsonComponents(target).push_back(MakeLaunchableRigidBodyEntry());
        SceneNs::ObjectJsonComponents(target).push_back(SceneNs::MakeComponentEntry("LaunchedBody"));
        SceneNs::SceneJsonObjects(data).push_back(target);
        scene.LoadJson(std::move(data));

        BodyRig rig;
        scene.Objects().ForEachComponent<LevelNs::LaunchedBody>(
            [&rig](LevelNs::LaunchedBody& body) { rig.body = &body; });
        if (rig.body != nullptr)
        {
            rig.object = rig.body->Owner();
            rig.box = rig.object->FindComponent<SceneNs::BoxCollider>();
            rig.rigidBody = rig.object->FindComponent<SceneNs::RigidBody>();
        }
        rig.restingBodies = scene.Physics().BodyCount();
        return rig;
    }

    // 飛ばされる物が乗る帯を回す。Scene::OnUpdate と同じく物理の 1 フレームを LateUpdate 帯の手前へ挟む
    // 飛んでいる物の Transform は RigidBody が物理の直後に書くので、前後の呼び出しも Scene と揃える
    void StepBody(SceneNs::Scene& scene)
    {
        scene.Objects().UpdateObjects(SceneNs::TickPriority::Update, SceneNs::TickPriority::LateUpdate);
        scene.Objects().ForEachComponent<SceneNs::RigidBody>([](SceneNs::RigidBody& body) {
            if (body.IsActive())
                body.PrePhysicsStep();
        });
        scene.Physics().Update(k_FixedDt);
        scene.Objects().ForEachComponent<SceneNs::RigidBody>([](SceneNs::RigidBody& body) {
            if (body.IsActive())
                body.PostPhysicsStep();
        });
        scene.Objects().UpdateObjects(SceneNs::TickPriority::LateUpdate);
    }

    // 曲線で飛ばし、剛体へ渡るまでの毎フレームの根の位置と、そのフレームの物理が使った速度を控える
    struct ArcFlight
    {
        Vector3 start{};
        std::vector<Vector3> positions;
        std::vector<Vector3> velocities;
        int handFrame = 0; // 剛体へ渡ったフレーム。1 始まりで、渡らなければ 0
    };

    ArcFlight FlyArc(SceneNs::Scene& scene, const BodyRig& rig, const LevelNs::LaunchArc& arc, int maxSteps)
    {
        ArcFlight flight;
        flight.start = rig.object->Root().Position();
        rig.body->Launch(arc);
        for (int i = 0; i < maxSteps; ++i)
        {
            // 渡したフレームの物理も曲線の速度で動いている。速度は渡す前に読む
            const Vector3 used = rig.rigidBody->Velocity();
            StepBody(scene);
            flight.positions.push_back(rig.object->Root().Position());
            flight.velocities.push_back(used);
            if (rig.body->Phase() != LevelNs::LaunchPhase::Arc)
            {
                flight.handFrame = i + 1;
                break;
            }
        }
        return flight;
    }

    // 破片と同じ RigidBody の既定の減衰へ戻す。台の的は同梱の球と同じ転がりの減衰を持つので、破片の道を見る試しで使う
    void UseDebrisDamping(const BodyRig& rig)
    {
        const SceneNs::RigidBody defaults;
        rig.rigidBody->SetLinearDamping(defaults.LinearDamping());
        rig.rigidBody->SetAngularDamping(defaults.AngularDamping());
    }

    // 止まるまで回して掛かったフレーム数を返す。止まらなければ maxSteps を返す
    int RunUntilRest(SceneNs::Scene& scene, LevelNs::LaunchedBody& body, int maxSteps)
    {
        for (int i = 0; i < maxSteps; ++i)
        {
            StepBody(scene);
            if (!body.IsFlying())
                return i + 1;
        }
        return maxSteps;
    }

    // 一時オブジェクトとして湧いた破片だけ集める。押し飛ばされた配置物は数えない
    std::vector<LevelNs::LaunchedBody*> DebrisBodies(SceneNs::Scene& scene)
    {
        std::vector<LevelNs::LaunchedBody*> out;
        scene.Objects().ForEachComponent<LevelNs::LaunchedBody>([&out](LevelNs::LaunchedBody& body) {
            if (body.Owner()->IsTransient())
                out.push_back(&body);
        });
        return out;
    }

    int MarkCount(SceneNs::Scene& scene)
    {
        int count = 0;
        scene.Objects().ForEachComponent<LevelNs::ImpactMark>([&count](LevelNs::ImpactMark&) { ++count; });
        return count;
    }
} // namespace

TEST(CollisionImpact, LightContactDoesNothing)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.movement, nullptr);
    SettleOnFloor(scene, rig);

    for (int i = 0; i < 20; ++i)
    {
        rig.movement->SetVelocity(Vector3{k_FastEntrySpeed, 0.0f, 0.0f});
        Step(scene, rig);
        ASSERT_FALSE(rig.impact->DidRebound());
        ASSERT_FALSE(rig.impact->DidBreak());
    }

    EXPECT_EQ(HitBody(rig), nullptr);
    EXPECT_TRUE(rig.breakable->IsActiveSelf());
    EXPECT_TRUE(rig.movement->IsActiveSelf());
}

TEST(CollisionImpact, ReboundsAwayFromApproachedBox)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    ASSERT_TRUE(rig.movement->IsBodySlamming());

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_LT(rig.movement->Velocity().x, 0.0f);
    EXPECT_GT(rig.movement->Velocity().y, 0.0f);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().z, 0.0f);
    EXPECT_FALSE(rig.movement->IsBodySlamming());
}

// 箱に押し付けたまま出しても当たる。発動したフレームのうちに進めずに打ち切ると、
// 裁定が一度も走らないまま突進が終わる
TEST(CollisionImpact, SlamFromRestingContactStillHits)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    SettleOnFloor(scene, rig);
    for (int i = 0; i < 20; ++i)
    {
        rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});
        Step(scene, rig);
    }
    ASSERT_FALSE(rig.impact->DidRebound());

    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    ASSERT_TRUE(rig.movement->IsBodySlamming());

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_TRUE(rig.impact->DidRebound());
}

// 溜めるほど高く返る。溜めるほど損になると、溜めて放つ意味が消える
TEST(CollisionImpact, ChargedImpactReboundsHigher)
{
    SceneNs::Scene plainScene;
    Rig plain = BuildSlam(plainScene, k_NearCourse);
    SetInstantImpact(plain);
    BeginSlam(plainScene, plain, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(plainScene, plain, 30), 30);

    SceneNs::Scene chargedScene;
    Rig charged = BuildSlam(chargedScene, k_NearCourse);
    SetInstantImpact(charged);
    BeginSlam(chargedScene, charged, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(chargedScene, charged, 30), 30);

    ASSERT_TRUE(plain.impact->DidRebound());
    ASSERT_TRUE(charged.impact->DidRebound());
    const float weak = plain.impact->LastImpact().reboundApexHeight;
    const float strong = charged.impact->LastImpact().reboundApexHeight;
    EXPECT_GT(weak, 0.0f);
    EXPECT_GT(strong, weak);
    EXPECT_GT(charged.movement->Velocity().y, plain.movement->Velocity().y);
}

// 反動は横でなく上へ大きく弾く。横へ流れると、押し返された感触より滑って離れた絵になる
TEST(CollisionImpact, ReboundGoesUpMoreThanSideways)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const Vector3 self = rig.impact->LastImpact().selfVelocity;
    EXPECT_GT(HorizontalSpeed(self), 0.0f);
    EXPECT_GT(self.y, HorizontalSpeed(self));
}

TEST(CollisionImpact, ReboundDirectionFollowsBoxAxis)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.targetCell = 1, .alongZ = true});
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f, true);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, 0.0f);
    EXPECT_LT(rig.movement->Velocity().z, 0.0f);
}

TEST(CollisionImpact, ReboundsFromDeepOverlapWhenApproaching)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.55f, .targetCell = 1});
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_LT(rig.movement->Velocity().x, 0.0f);
}

TEST(CollisionImpact, DoesNotApplyWhileSeparating)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.1f, .targetCell = 1});
    SetInstantImpact(rig);
    BeginSlam(scene, rig, -k_RunSpeed, 0.0f);

    for (int i = 0; i < 20; ++i)
    {
        Step(scene, rig);
        ASSERT_FALSE(rig.impact->DidRebound());
        ASSERT_FALSE(rig.impact->DidBreak());
    }
    EXPECT_EQ(HitBody(rig), nullptr);
}

TEST(CollisionImpact, NoReboundWithoutBreakableMark)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.targetCell = 1, .withBreakable = false});
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    EXPECT_EQ(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_FALSE(rig.impact->DidRebound());
}

// 壊せる物を総当たりで見るので、重なりを見ないと離れた所に置いた物にも反発する
TEST(CollisionImpact, NoReboundAgainstDistantBox)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.targetCell = 12});
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    EXPECT_EQ(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_FALSE(rig.impact->DidRebound());
}

TEST(CollisionImpact, NoReboundAgainstTriggerBox)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.targetBox, nullptr);
    rig.targetBox->SetTrigger(true);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    EXPECT_EQ(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_FALSE(rig.impact->DidRebound());
}

// トリガーは箱だけでなく全ての形の当たり判定に付く。球でも通り抜ける体積として相手から外れる
TEST(CollisionImpact, NoReboundAgainstTriggerSphere)
{
    SceneNs::Scene scene;
    SlamCourse course = k_NearCourse;
    course.sphereTarget = true;
    Rig rig = BuildSlam(scene, course);
    ASSERT_NE(rig.target, nullptr);
    SceneNs::SphereCollider* sphere = rig.target->FindComponent<SceneNs::SphereCollider>();
    ASSERT_NE(sphere, nullptr);
    sphere->SetTrigger(true);
    scene.SyncPhysics();
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    EXPECT_EQ(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_FALSE(rig.impact->DidRebound());
}

// 外接箱で見ると、回転した的の触れてもいない隅で弾かれる
TEST(CollisionImpact, NoReboundInEmptyCornerOfRotatedTarget)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.2f, .lateral = -1.1f, .targetCell = 1});
    ASSERT_NE(rig.targetBox, nullptr);
    // 細長い板を 45° 回すと外接箱の対角の 2 隅が空く。自機はその片方に立つ
    rig.targetBox->SetHalfExtents(Vector3{1.5f, 0.5f, 0.1f});
    rig.targetBox->SetRotationEulerDegrees(Vector3{0.0f, 45.0f, 0.0f});
    scene.SyncPhysics();
    SettleOnFloor(scene, rig);

    const Vector3 position = rig.movement->Owner()->Root().Position();
    const NS::Core::AABB bounds = rig.targetBox->WorldAABB();
    ASSERT_LE(std::abs(position.x - bounds.Center.x), bounds.Extents.x);
    ASSERT_LE(std::abs(position.z - bounds.Center.z), bounds.Extents.z);
    const NS::Phys::Capsule capsule{
        position, Vector3{0.0f, 1.0f, 0.0f}, rig.movement->CapsuleHalfHeight(), rig.movement->CapsuleRadius()};
    const std::vector<JPH::BodyID> touching = scene.Physics().OverlapCapsule(capsule);
    ASSERT_EQ(std::find(touching.begin(), touching.end(), rig.targetBox->BodyId()), touching.end());

    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    EXPECT_EQ(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_FALSE(rig.impact->DidRebound());
}

// 飛んでいる相手に当て直すと、明けのフレームの位置から新しい曲線が始まる。検知のフレームの位置へは戻さない
// 跡もその位置の真下の床に出る
// 飛んでいる間も collider は RigidBody の body を返すので、置かれた物と同じく反発する
TEST(CollisionImpact, HitOnAFlyingTargetStartsTheNewArcFromWhereItIsAtRelease)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.target, nullptr);
    ASSERT_NE(rig.rigidBody, nullptr);
    LevelNs::LaunchedBody* body = rig.target->AddComponent<LevelNs::LaunchedBody>();
    body->LaunchRigid(Vector3{0.0f, 0.0f, 0.0f});
    ASSERT_TRUE(body->IsFlying());
    ASSERT_FALSE(rig.rigidBody->BodyId().IsInvalid());
    ASSERT_EQ(rig.targetBox->BodyId(), rig.rigidBody->BodyId());

    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    ASSERT_GT(rig.impact->LastImpact().hitStopSteps, 0);
    const Vector3 detected = rig.target->Root().Position();

    // 止めの間も相手は飛び続ける。この台は物理を回さないので、物理が根へ書く位置の代わりに動かした位置を書く
    // body は検知のフレームの位置に残る。跡の床探しがその body に当たらないよう、箱の幅より遠くへ動かす
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const Vector3 moved = detected + Vector3{1.0f, 0.3f, 0.0f};
    rig.target->Root().SetPosition(moved);
    ASSERT_LT(StepsUntilMovementActive(scene, rig, 60), 60);

    const Vector3 released = rig.target->Root().Position();
    EXPECT_FLOAT_EQ(released.x, moved.x);
    EXPECT_FLOAT_EQ(released.y, moved.y);
    EXPECT_FLOAT_EQ(released.z, moved.z);
    EXPECT_EQ(body->Phase(), LevelNs::LaunchPhase::Arc);
    const Vector3 bodyPosition = scene.Physics().BodyPosition(rig.rigidBody->BodyId());
    EXPECT_NEAR(bodyPosition.x, moved.x, 1.0e-4f);
    EXPECT_NEAR(bodyPosition.y, moved.y, 1.0e-4f);
    EXPECT_NEAR(bodyPosition.z, moved.z, 1.0e-4f);

    SceneNs::GameObject* mark = nullptr;
    scene.Objects().ForEachComponent<LevelNs::ImpactMark>(
        [&mark](LevelNs::ImpactMark& found) { mark = found.Owner(); });
    ASSERT_NE(mark, nullptr);
    // 床の上面 0.5 から 2cm 浮かせた高さ
    EXPECT_NEAR(mark->Root().Position().x, moved.x, 1.0e-4f);
    EXPECT_NEAR(mark->Root().Position().y, 0.52f, 1.0e-4f);
    EXPECT_NEAR(mark->Root().Position().z, moved.z, 1.0e-4f);
}

// 貫通は相手を飛ばさないので、記録の飛ばす曲線の欄は 0。前の押し飛ばしの曲線を残さない
TEST(CollisionImpact, BreakAfterAPushRecordsNoLaunch)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    ASSERT_GT(rig.impact->LastImpact().launchDistance, 0.0f);

    // 返りで下がった自機が、着地してから同じ相手へもう一度突進して壊す。この台は物理を回さないので相手はその場に居る
    // この台の反動の空中は 0.8 秒ほどあり、床へ着くまで待つ手順 (最大 30 フレーム) より長い
    for (int i = 0; i < 120 && rig.movement->IsRebounding(); ++i)
    {
        Step(scene, rig);
    }
    ASSERT_FALSE(rig.movement->IsRebounding());
    EnableBreak(rig);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.breakable->SetToughness(0.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidBreak());

    const LevelNs::ImpactRecord& hit = rig.impact->LastImpact();
    EXPECT_TRUE(hit.broke);
    EXPECT_FLOAT_EQ(hit.launchDistance, 0.0f);
    EXPECT_FLOAT_EQ(hit.launchApexHeight, 0.0f);
    EXPECT_FLOAT_EQ(hit.launchVelocity.x, 0.0f);
    EXPECT_FLOAT_EQ(hit.launchVelocity.y, 0.0f);
    EXPECT_FLOAT_EQ(hit.launchVelocity.z, 0.0f);
    // 貫通は自機も反動しない。前の押し飛ばしの反動の高さを残さない
    EXPECT_FLOAT_EQ(hit.reboundApexHeight, 0.0f);
    EXPECT_FALSE(rig.movement->IsRebounding());
}

// 反動の高さと距離の欄が、当たりの記録と自機の反動の軌道を決める。台の的は質量 1 なので質量の効きは 1
// 横ずれ 0 の当たりは中心近くなので、距離には中心近くの当たりの倍率が掛かる
TEST(CollisionImpact, ReboundFieldsDriveTheApexAndTheVelocity)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    SetFloatField(*rig.impact, "反動の高さ", 2.0f);
    SetFloatField(*rig.impact, "反動の距離", 0.8f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const float power = rig.impact->LastPower();
    const LevelNs::ImpactRecord& hit = rig.impact->LastImpact();
    EXPECT_FLOAT_EQ(hit.reboundApexHeight, 2.0f * power);
    // 的は +X にあり、自機は的の中心から -X の向きへ弾かれる
    const Vector3 expected = rig.movement->ReboundVelocityFor(
        NS::Game::Player::ReboundArc{.direction = Vector3{-1.0f, 0.0f, 0.0f},
                                     .apexHeight = 2.0f * power,
                                     .distance = 0.8f * power * k_CenterHitReboundDistanceScale});
    ASSERT_GT(expected.y, 0.0f);
    EXPECT_FLOAT_EQ(hit.selfVelocity.x, expected.x);
    EXPECT_FLOAT_EQ(hit.selfVelocity.y, expected.y);
    EXPECT_FLOAT_EQ(hit.selfVelocity.z, expected.z);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, expected.x);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().y, expected.y);
    EXPECT_TRUE(rig.movement->IsRebounding());
}

// 中心近くの当たりだけ反動の距離が倍率ぶん伸び、頂点の高さは変わらない。惜しい当たりは距離も高さも欄のまま
// 台の的は質量 1 なので、反動の高さと距離に掛かるのは威力だけ
TEST(CollisionImpact, CenterHitReboundGoesFartherBackAtTheSameHeight)
{
    struct TierCase
    {
        SlamCourse course;
        LevelNs::HitTier tier = LevelNs::HitTier::Center;
        float distanceScale = 1.0f;
    };
    const std::vector<TierCase> cases{
        {k_NearCourse, LevelNs::HitTier::Center, k_CenterHitReboundDistanceScale},
        {k_EdgeCourse, LevelNs::HitTier::Near, 1.0f},
    };

    for (const TierCase& tierCase : cases)
    {
        SceneNs::Scene scene;
        Rig rig = BuildSlam(scene, tierCase.course);
        SetInstantImpact(rig);
        BeginSlam(scene, rig, k_RunSpeed, 0.0f);
        ASSERT_LT(StepUntilImpact(scene, rig, 30), 30) << tierCase.course.lateral;

        const LevelNs::ImpactRecord& hit = rig.impact->LastImpact();
        ASSERT_EQ(hit.tier, tierCase.tier) << tierCase.course.lateral;
        ASSERT_FALSE(hit.broke) << tierCase.course.lateral;
        EXPECT_FLOAT_EQ(hit.reboundApexHeight, k_ReboundApexHeight * hit.power) << tierCase.course.lateral;
        // 横ずれのある当たりは向きが的の中心からの並びで斜めになる。向きは記録の速度から取り、速さと縦を見る
        Vector3 direction{};
        ASSERT_TRUE(NS::Core::TryNormalizeHorizontal(hit.selfVelocity, direction)) << tierCase.course.lateral;
        const Vector3 expected = rig.movement->ReboundVelocityFor(
            NS::Game::Player::ReboundArc{.direction = direction,
                                         .apexHeight = k_ReboundApexHeight * hit.power,
                                         .distance = k_ReboundDistance * hit.power * tierCase.distanceScale});
        EXPECT_FLOAT_EQ(HorizontalSpeed(hit.selfVelocity), HorizontalSpeed(expected)) << tierCase.course.lateral;
        EXPECT_FLOAT_EQ(hit.selfVelocity.y, expected.y) << tierCase.course.lateral;
    }
}

// 反動の欄が曲線にならない当たりは自機を弾けない。明けの自機の速度は記録と同じ 0 で、反動の状態へ移らない
TEST(CollisionImpact, ReboundThatIsNotAnArcLeavesThePlayerAtTheRecordedVelocity)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    SetFloatField(*rig.impact, "反動の高さ", 0.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const LevelNs::ImpactRecord& hit = rig.impact->LastImpact();
    ASSERT_FALSE(hit.broke);
    EXPECT_FLOAT_EQ(hit.reboundApexHeight, 0.0f);
    EXPECT_FLOAT_EQ(hit.selfVelocity.x, 0.0f);
    EXPECT_FLOAT_EQ(hit.selfVelocity.y, 0.0f);
    EXPECT_FLOAT_EQ(hit.selfVelocity.z, 0.0f);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, hit.selfVelocity.x);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().y, hit.selfVelocity.y);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().z, hit.selfVelocity.z);
    EXPECT_FALSE(rig.movement->IsRebounding());
}

// 同梱シーンに置く質量 (0.5 以上) で一番弱い当たり
// 溜めないタップを大きく外して当てても、自機は 0.5 m 以上上がり、相手も飛ぶ
TEST(CollisionImpact, WeakestHitInThePlacedRangeStillMovesBothSides)
{
    SceneNs::Scene scene;
    // 横ずれ 0.8 ÷ (的の半幅 0.5 + 自機の半径 0.4) = 0.89 で大きな外れ
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .lateral = 0.8f, .targetCell = 1});
    SetInstantImpact(rig);
    SetFloatField(*rig.movement, "寄せる角度の上限", 0.0f);
    rig.rigidBody->SetMass(0.5f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const LevelNs::ImpactRecord& hit = rig.impact->LastImpact();
    ASSERT_FLOAT_EQ(hit.charge01, 0.0f);
    ASSERT_EQ(hit.tier, LevelNs::HitTier::Wide);
    EXPECT_GE(hit.reboundApexHeight, 0.5f);
    EXPECT_GT(hit.selfVelocity.y, 0.0f);
    EXPECT_GT(HorizontalSpeed(hit.selfVelocity), 0.0f);
    EXPECT_GT(HorizontalSpeed(hit.launchVelocity), 0.0f);
}

TEST(CollisionImpact, WithoutCollisionInputPowerIsRatioOnly)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.targetCell = 1, .withCollisionInput = false});
    ASSERT_EQ(rig.input, nullptr);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.impact->LastPower(), 1.0f);
    EXPECT_FALSE(rig.impact->WasCenterHit());
    // 段と横ずれは記録する。段は中心近くでも、白の光と止めの倍率は掛けない
    EXPECT_EQ(rig.impact->LastImpact().tier, LevelNs::HitTier::Center);
    EXPECT_NEAR(rig.impact->LastImpact().offset01, 0.0f, 0.01f);
    EXPECT_FALSE(rig.impact->LastImpact().centerHit);
}

TEST(CollisionImpact, IsCreatableFromTypeName)
{
    SceneNs::GameObject obj;
    SceneNs::Component* comp = SceneNs::CreateComponent("ImpactResolver", obj);
    ASSERT_NE(comp, nullptr);
    ASSERT_NE(comp->GetReflection(), nullptr);
    EXPECT_STREQ(comp->GetReflection()->typeName, "ImpactResolver");
    EXPECT_EQ(obj.FindComponent<LevelNs::ImpactResolver>(), comp);
}

TEST(CollisionImpact, ReboundLaunchesHitBody)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_EQ(HitBody(rig), nullptr);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidRebound());
    LevelNs::LaunchedBody* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());
}

// 当たり 1 回の内訳を外から読めないと、止めと飛びを画面から目で数えることになる
TEST(CollisionImpact, KeepsTheNumbersOfTheLastHitForReading)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.target, nullptr);
    EXPECT_EQ(rig.impact->LastImpact().sequence, 0u);

    BeginSlam(scene, rig, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const LevelNs::ImpactRecord& hit = rig.impact->LastImpact();
    EXPECT_EQ(hit.sequence, 1u);
    EXPECT_EQ(hit.targetId, rig.target->Id());
    EXPECT_FLOAT_EQ(hit.charge01, 1.0f);
    EXPECT_GT(hit.power, 0.0f);
    EXPECT_GT(hit.positionFactor, 0.0f);
    EXPECT_GT(hit.hitStopSteps, 0);
    EXPECT_GT(hit.cameraShake, 0.0f);
    EXPECT_FALSE(hit.broke);
    // 反発と押し飛ばしは符号が逆に出る。取り違えても値の大きさでは気づけない
    EXPECT_LT(hit.selfVelocity.x, 0.0f);
    EXPECT_GT(hit.launchVelocity.x, 0.0f);
    EXPECT_GT(hit.launchDistance, 0.0f);
    EXPECT_GT(hit.launchApexHeight, 0.0f);
    EXPECT_GT(hit.reboundApexHeight, 0.0f);
    // 突進は +X へ向かう。食い込みと振動の向きも相手の飛ぶ向きも突進の向き
    EXPECT_FLOAT_EQ(hit.impactDir.x, 1.0f);
    EXPECT_FLOAT_EQ(hit.impactDir.y, 0.0f);
    EXPECT_FLOAT_EQ(hit.impactDir.z, 0.0f);
    // 中心近くの当たりの返りの始めの値。欄「中心近くの当たりの白のフレーム数」「中心近くの当たりの寄りの倍率」
    // 「中心近くの当たりの傾き」の既定と、最初の振れの大きさ = カメラ揺れの強さ × 威力 × 質量因子 × 中心近くの倍率
    ASSERT_EQ(hit.tier, LevelNs::HitTier::Center);
    const float mass = rig.rigidBody->EffectiveMass();
    EXPECT_NEAR(hit.cameraShake, 0.06f * hit.power * mass / (mass + 1.0f) * 1.25f, 1.0e-6f);
    EXPECT_EQ(hit.flashStart, 6);
    EXPECT_FLOAT_EQ(hit.zoomStart, 1.15f);
    EXPECT_FLOAT_EQ(std::abs(hit.rollStart), 3.0f);
    // 欄「中心近くの当たりのパッドの振動の強さ」の既定。中心近くは重いモーターだけ
    EXPECT_FLOAT_EQ(hit.padStart.left, 1.0f);
    EXPECT_EQ(hit.padStart.right, 0.0f);
}

TEST(CollisionImpact, ReboundsAgainstSphereTarget)
{
    SceneNs::Scene scene;
    SlamCourse course = k_NearCourse;
    course.sphereTarget = true;
    Rig rig = BuildSlam(scene, course);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_TRUE(rig.impact->DidRebound());
}

TEST(CollisionImpact, LaunchesSphereTarget)
{
    SceneNs::Scene scene;
    SlamCourse course = k_NearCourse;
    course.sphereTarget = true;
    Rig rig = BuildSlam(scene, course);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    LevelNs::LaunchedBody* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());
    EXPECT_GT(body->Velocity().x, 0.0f);
}

// 横ずれのある当たりでも、相手は突進の向きへ飛ぶ。中心の並びの向きへ飛ばすと、狙った所と別の所へ落ちる
TEST(CollisionImpact, OffCenterHitLaunchesAlongTheSlam)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_EdgeCourse);
    SetInstantImpact(rig);
    // 寄せは裁定と同じフレームの先に突進の向きを曲げる。止めて、横ずれを当たりまで残す
    SetFloatField(*rig.movement, "寄せる角度の上限", 0.0f);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    Vector3 slam{};
    bool hitFound = false;
    for (int i = 0; i < 30 && !hitFound; ++i)
    {
        slam = rig.movement->BodySlamVelocity();
        StepWorld(scene);
        hitFound = rig.impact->DidRebound();
        if (!hitFound)
        {
            rig.movement->OnUpdate();
        }
    }
    ASSERT_TRUE(hitFound);

    const LevelNs::ImpactRecord& hit = rig.impact->LastImpact();
    ASSERT_GT(hit.offset01, 0.3f);
    Vector3 slamDir{};
    Vector3 launchDir{};
    ASSERT_TRUE(NS::Core::TryNormalizeHorizontal(slam, slamDir));
    ASSERT_TRUE(NS::Core::TryNormalizeHorizontal(hit.launchVelocity, launchDir));
    EXPECT_NEAR(launchDir.x, slamDir.x, 1.0e-4f);
    EXPECT_NEAR(launchDir.z, slamDir.z, 1.0e-4f);
    EXPECT_NEAR(hit.impactDir.x, slamDir.x, 1.0e-4f);
    EXPECT_NEAR(hit.impactDir.z, slamDir.z, 1.0e-4f);
    EXPECT_FLOAT_EQ(hit.impactDir.y, 0.0f);
}

TEST(CollisionImpact, LaunchLiftsHitBody)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    LevelNs::LaunchedBody* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_GT(body->Velocity().y, 0.0f);
    EXPECT_LT(body->Velocity().y, HorizontalSpeed(body->Velocity()));
}

// 重い相手ほど相手の飛ぶ距離が縮み、自機が高く速く返る。どの質量でも両方が動く
TEST(CollisionImpact, HeavierBodyFliesShorterAndReboundsThePlayerHarder)
{
    const std::vector<float> masses{0.5f, 1.0f, 8.0f};
    std::vector<float> distances;
    std::vector<float> apexes;
    std::vector<float> rebounds;
    for (const float mass : masses)
    {
        SceneNs::Scene scene;
        Rig rig = BuildSlam(scene, k_NearCourse);
        SetInstantImpact(rig);
        rig.rigidBody->SetMass(mass);
        BeginSlam(scene, rig, k_RunSpeed, 0.0f);
        ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
        const LevelNs::ImpactRecord& hit = rig.impact->LastImpact();
        distances.push_back(hit.launchDistance);
        apexes.push_back(hit.reboundApexHeight);
        rebounds.push_back(Speed(hit.selfVelocity));

        // 反動の高さと距離は 欄 × 威力 × 2 × 質量 ÷ (質量 + 1)。的は +X にあり、自機は -X へ弾かれる
        // 横ずれ 0 の当たりは中心近くなので、距離には中心近くの当たりの倍率も掛かる
        const float scale = rig.impact->LastPower() * 2.0f * (mass / (mass + 1.0f));
        EXPECT_FLOAT_EQ(hit.reboundApexHeight, k_ReboundApexHeight * scale) << "質量 " << mass;
        const Vector3 expected = rig.movement->ReboundVelocityFor(
            NS::Game::Player::ReboundArc{.direction = Vector3{-1.0f, 0.0f, 0.0f},
                                         .apexHeight = k_ReboundApexHeight * scale,
                                         .distance = k_ReboundDistance * scale * k_CenterHitReboundDistanceScale});
        EXPECT_FLOAT_EQ(hit.selfVelocity.x, expected.x) << "質量 " << mass;
        EXPECT_FLOAT_EQ(hit.selfVelocity.y, expected.y) << "質量 " << mass;
        EXPECT_FLOAT_EQ(hit.selfVelocity.z, expected.z) << "質量 " << mass;
    }

    for (std::size_t i = 0; i < masses.size(); ++i)
    {
        EXPECT_GT(distances[i], 0.0f) << "質量 " << masses[i];
        EXPECT_GT(apexes[i], 0.0f) << "質量 " << masses[i];
        EXPECT_GT(rebounds[i], 0.0f) << "質量 " << masses[i];
    }
    for (std::size_t i = 1; i < masses.size(); ++i)
    {
        EXPECT_LT(distances[i], distances[i - 1]) << "質量 " << masses[i];
        EXPECT_GT(apexes[i], apexes[i - 1]) << "質量 " << masses[i];
        EXPECT_GT(rebounds[i], rebounds[i - 1]) << "質量 " << masses[i];
    }
}

TEST(CollisionImpact, ChargedImpactLaunchesFarther)
{
    SceneNs::Scene plainScene;
    Rig plain = BuildSlam(plainScene, k_NearCourse);
    SetInstantImpact(plain);
    BeginSlam(plainScene, plain, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(plainScene, plain, 30), 30);

    SceneNs::Scene chargedScene;
    Rig charged = BuildSlam(chargedScene, k_NearCourse);
    SetInstantImpact(charged);
    BeginSlam(chargedScene, charged, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(chargedScene, charged, 30), 30);

    EXPECT_GT(charged.impact->LastImpact().launchDistance, plain.impact->LastImpact().launchDistance);
}

// 欄から曲線を組む。距離は 押し飛ばしの距離 × 威力 ÷ 質量^指数 で、高さは距離と同じ比で伸びる
TEST(CollisionImpact, LaunchFieldsDriveTheArc)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    SetFloatField(*rig.impact, "押し飛ばしの距離", 20.0f);
    SetFloatField(*rig.impact, "押し飛ばしの高さ", 1.5f);
    SetFloatField(*rig.impact, "下りの速さの倍率", 2.0f);
    SetFloatField(*rig.impact, "頂点の帯の縦速度", 1.5f);
    SetFloatField(*rig.impact, "頂点の帯の重力倍率", 0.25f);
    // 指数は 1.0 に固定する。既定の 0.35 乗が混ざると、この欄だけを見る式にならない
    SetFloatField(*rig.impact, "押し飛ばしの質量指数", 1.0f);
    rig.rigidBody->SetMass(2.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const LevelNs::ImpactRecord& hit = rig.impact->LastImpact();
    const float distance = 20.0f * hit.power / 2.0f;
    const float apexHeight = 1.5f * (distance / 20.0f);
    EXPECT_FLOAT_EQ(hit.launchDistance, distance);
    EXPECT_FLOAT_EQ(hit.launchApexHeight, apexHeight);
    const Vector3 initial = LevelNs::LaunchArcInitialVelocity(LevelNs::LaunchArc{.direction = Vector3{1.0f, 0.0f, 0.0f},
                                                                                 .distance = distance,
                                                                                 .apexHeight = apexHeight,
                                                                                 .fallGravityScale = 2.0f,
                                                                                 .apexBandSpeed = 1.5f,
                                                                                 .apexBandGravityScale = 0.25f});
    ASSERT_GT(initial.x, 0.0f);
    EXPECT_NEAR(hit.launchVelocity.x, initial.x, 1.0e-4f);
    EXPECT_NEAR(hit.launchVelocity.y, initial.y, 1.0e-4f);
    EXPECT_NEAR(hit.launchVelocity.z, initial.z, 1.0e-4f);

    // 相手は曲線の最初の 1 フレームの変位 ÷ dt で動き出す。水平は初速のまま、縦は上りの重力の半フレームぶん低い
    LevelNs::LaunchedBody* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->Phase(), LevelNs::LaunchPhase::Arc);
    const Vector3 moving = rig.rigidBody->Velocity();
    EXPECT_NEAR(moving.x, initial.x, 1.0e-3f);
    EXPECT_NEAR(moving.y, initial.y + 0.5f * k_LaunchGravity * k_FixedDt, 1.0e-3f);
    EXPECT_NEAR(moving.z, initial.z, 1.0e-3f);
}

// 質量の効きは指数で曲げる。既定 0.35 は 4^0.35 = 約 1.62 で割る
TEST(CollisionImpact, LaunchMassExponentBendsMassEffect)
{
    SceneNs::Scene inverseScene;
    Rig inverse = BuildSlam(inverseScene, k_NearCourse);
    SetInstantImpact(inverse);
    SetFloatField(*inverse.impact, "押し飛ばしの質量指数", 1.0f);
    inverse.rigidBody->SetMass(4.0f);
    BeginSlam(inverseScene, inverse, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(inverseScene, inverse, 30), 30);

    // 欄を触らず既定の指数で当てる。既定を 0.35 から動かすとこの期待値が外れる
    SceneNs::Scene rootScene;
    Rig root = BuildSlam(rootScene, k_NearCourse);
    SetInstantImpact(root);
    root.rigidBody->SetMass(4.0f);
    BeginSlam(rootScene, root, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(rootScene, root, 30), 30);

    EXPECT_FLOAT_EQ(inverse.impact->LastImpact().launchDistance, k_LaunchDistance * inverse.impact->LastPower() / 4.0f);
    EXPECT_FLOAT_EQ(root.impact->LastImpact().launchDistance,
                    k_LaunchDistance * root.impact->LastPower() / std::pow(4.0f, 0.35f));
    EXPECT_FLOAT_EQ(root.impact->LastImpact().launchApexHeight,
                    k_LaunchApexHeight * root.impact->LastPower() / std::pow(4.0f, 0.35f));
}

// 質量 0 は基準の 1 として扱う。0 で割ると押し飛ばしの距離が無限になり、画面の外へ消える
TEST(CollisionImpact, ZeroMassLaunchesLikeUnitMass)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.rigidBody, nullptr);
    SetInstantImpact(rig);
    rig.rigidBody->SetMass(0.0f);
    ASSERT_FLOAT_EQ(rig.rigidBody->EffectiveMass(), 1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const float distance = rig.impact->LastImpact().launchDistance;
    ASSERT_TRUE(std::isfinite(distance));
    EXPECT_FLOAT_EQ(distance, k_LaunchDistance * rig.impact->LastPower());
}

// 衝突の瞬間に自機が数フレーム止まる。止まっている間は反発も発射も適用されず、明けたフレームにまとめて掛かる
// 溜めた突進は床を進んで当たるので、検知のフレームの接地の印が明けまで残る。明けのフレームに地上の摩擦を掛けない
TEST(CollisionImpact, HitStopFreezesPlayerAndDefersLaunch)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.rigidBody->SetMass(4.0f);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    // 検知のフレームは移動を止めない。最後の 1 フレームで自機が相手へ触れてから凍る
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_TRUE(rig.movement->IsActiveSelf());
    EXPECT_EQ(HitBody(rig), nullptr);

    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    EXPECT_EQ(HitBody(rig), nullptr);
    EXPECT_GE(rig.movement->Velocity().x, 0.0f);

    const int steps = StepsUntilMovementActive(scene, rig, 60);
    EXPECT_LT(steps, 60);
    EXPECT_LT(rig.movement->Velocity().x, 0.0f);
    LevelNs::LaunchedBody* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());

    ASSERT_TRUE(rig.movement->IsGrounded());
    const float released = HorizontalSpeed(rig.impact->LastImpact().selfVelocity);
    rig.movement->OnUpdate();
    EXPECT_TRUE(rig.movement->IsRebounding());
    EXPECT_NEAR(HorizontalSpeed(rig.movement->Velocity()), released, 1.0e-3f);
}

// 突進の最中の押しは当てたフレームに出さず、止めの明けへ持ち越す。止めの間の押しと同じく、明けに反動から出る
// 当てたフレームに出すと、その突進が止めの間も残り、明けの反動に終わりの通知なしで上書きされる
TEST(CollisionImpact, PressDuringTheRushCarriesOverTheHitStopIntoTheRebound)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    int started = 0;
    int ended = 0;
    rig.movement->PlayerEventsRef().onBodySlamStarted.Subscribe([&started]() { ++started; });
    rig.movement->PlayerEventsRef().onBodySlamEnded.Subscribe([&ended]() { ++ended; });
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);
    ASSERT_TRUE(rig.movement->IsBodySlamming());
    rig.movement->RequestBodySlam(0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_GT(rig.impact->LastImpact().hitStopSteps, 0);
    ASSERT_TRUE(rig.movement->IsGrounded());
    rig.movement->OnUpdate();
    EXPECT_FALSE(rig.movement->IsBodySlamming()) << "当てたフレームに次の突進が出た";

    ASSERT_LT(StepsUntilMovementActive(scene, rig, 60), 60);
    EXPECT_TRUE(rig.movement->IsRebounding());
    EXPECT_EQ(started, ended) << "終わりの通知が出ないまま反動へ移った突進がある";

    rig.movement->OnUpdate();
    EXPECT_TRUE(rig.movement->IsBodySlamming()) << "突進の最中の押しが明けに出ない";
}

// 重さは飛距離より止められた時間で出る。重い物ほど長く止まる
TEST(CollisionImpact, HeavierTargetStopsLonger)
{
    SceneNs::Scene lightScene;
    Rig light = BuildSlam(lightScene, k_NearCourse);
    // 中心直撃は中心近くの当たりの倍率が乗る。既定の基準秒だと重い側が上限 12
    // フレームに張り付くので、下げて上限の外で比べる
    SetFloatField(*light.impact, "ヒットストップ基準秒", 1.0f / 60.0f);
    light.rigidBody->SetMass(1.0f);
    BeginSlam(lightScene, light, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(lightScene, light, 30), 30);
    Step(lightScene, light);
    ASSERT_FALSE(light.movement->IsActiveSelf());
    const int lightSteps = StepsUntilMovementActive(lightScene, light, 60);

    SceneNs::Scene heavyScene;
    Rig heavy = BuildSlam(heavyScene, k_NearCourse);
    SetFloatField(*heavy.impact, "ヒットストップ基準秒", 1.0f / 60.0f);
    heavy.rigidBody->SetMass(8.0f);
    BeginSlam(heavyScene, heavy, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(heavyScene, heavy, 30), 30);
    Step(heavyScene, heavy);
    ASSERT_FALSE(heavy.movement->IsActiveSelf());
    const int heavySteps = StepsUntilMovementActive(heavyScene, heavy, 60);

    EXPECT_GT(lightSteps, 0);
    EXPECT_LT(lightSteps, 60);
    EXPECT_LT(lightSteps, heavySteps);
}

TEST(CollisionImpact, ChargedImpactStopsLonger)
{
    SceneNs::Scene plainScene;
    Rig plain = BuildSlam(plainScene, k_NearCourse);
    BeginSlam(plainScene, plain, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(plainScene, plain, 30), 30);
    Step(plainScene, plain);
    ASSERT_FALSE(plain.movement->IsActiveSelf());
    const int plainSteps = StepsUntilMovementActive(plainScene, plain, 60);

    SceneNs::Scene chargedScene;
    Rig charged = BuildSlam(chargedScene, k_NearCourse);
    BeginSlam(chargedScene, charged, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(chargedScene, charged, 30), 30);
    Step(chargedScene, charged);
    ASSERT_FALSE(charged.movement->IsActiveSelf());
    const int chargedSteps = StepsUntilMovementActive(chargedScene, charged, 60);

    EXPECT_GT(plainSteps, 0);
    EXPECT_LT(plainSteps, chargedSteps);
}

// 基準は秒で指定し、内部でフレーム数へ換算して凍結の長さを決める
TEST(CollisionImpact, HitStopBaseSecondsDrivesFreezeLength)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    // 中心直撃は中心近くの当たりの倍率 2.0 が乗る
    SetFloatField(*rig.impact, "ヒットストップ基準秒", 2.0f / 60.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    const int expected = static_cast<int>(std::lround(2.0f * rig.impact->LastPower() * 2.0f));
    EXPECT_EQ(StepsUntilMovementActive(scene, rig, 60), expected);
}

// 凍結中は自機が進行方向へ潰れる。反発の前半を潰れで見せる
TEST(CollisionImpact, FreezeSquashesPlayerShape)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.rigidBody->SetMass(4.0f);
    const Vector3 authored = rig.movement->Owner()->Root().Scale();
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    const Vector3 detected = rig.movement->Owner()->Root().Scale();
    EXPECT_FLOAT_EQ(detected.x, authored.x);
    EXPECT_FLOAT_EQ(detected.y, authored.y);

    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const Vector3 squashed = rig.movement->Owner()->Root().Scale();
    EXPECT_LT(squashed.x, authored.x);
    EXPECT_GT(squashed.y, authored.y);
    EXPECT_FLOAT_EQ(squashed.z, authored.z);
}

// 反動は上へ弾かれるので、明けのフレームに縦へ伸びる。3 フレームで縮む側へ行き過ぎ、6 フレームで元の形へ厳密に戻る
// 伸び 1.2 と行き過ぎ 0.5 は欄「弾け伸びの倍率」「弾け伸びの行き過ぎ」の既定
TEST(CollisionImpact, ReleaseStretchesUpOvershootsThenRestoresScaleExactly)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.rigidBody->SetMass(4.0f);
    const Vector3 authored = rig.movement->Owner()->Root().Scale();
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);

    const Vector3 stretched = rig.movement->Owner()->Root().Scale();
    EXPECT_FLOAT_EQ(stretched.x, authored.x);
    EXPECT_NEAR(stretched.y, authored.y * 1.2f, 1e-5f);
    EXPECT_FLOAT_EQ(stretched.z, authored.z);

    for (int i = 0; i < 3; ++i)
    {
        Step(scene, rig);
    }
    const Vector3 overshot = rig.movement->Owner()->Root().Scale();
    EXPECT_FLOAT_EQ(overshot.x, authored.x);
    EXPECT_NEAR(overshot.y, authored.y * 0.9f, 1e-5f);
    EXPECT_FLOAT_EQ(overshot.z, authored.z);

    for (int i = 0; i < 3; ++i)
    {
        Step(scene, rig);
    }
    const Vector3 restored = rig.movement->Owner()->Root().Scale();
    EXPECT_EQ(restored.x, authored.x);
    EXPECT_EQ(restored.y, authored.y);
    EXPECT_EQ(restored.z, authored.z);
}

// 行き過ぎ 0 では、明けの伸びた形から前半で元の形に着き、元より縮まない
TEST(CollisionImpact, ZeroOvershootReachesTheRestShapeWithoutShrinking)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.rigidBody->SetMass(4.0f);
    SetFloatField(*rig.impact, "弾け伸びの行き過ぎ", 0.0f);
    const Vector3 authored = rig.movement->Owner()->Root().Scale();
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    ASSERT_LT(StepsUntilMovementActive(scene, rig, 60), 60);
    // 伸びていなければ、元の形に着くのも縮まないのも当たり前になる
    ASSERT_GT(rig.movement->Owner()->Root().Scale().y, authored.y);

    float lowest = rig.movement->Owner()->Root().Scale().y;
    float atHalf = lowest;
    for (int i = 0; i < 6; ++i)
    {
        Step(scene, rig);
        const float y = rig.movement->Owner()->Root().Scale().y;
        lowest = std::min(lowest, y);
        if (i == 2)
        {
            atHalf = y;
        }
    }
    EXPECT_NEAR(atHalf, authored.y, 1e-5f);
    EXPECT_GE(lowest, authored.y - 1e-5f);
}

// 形を戻している途中で次の当たりが来ても、戻す先は配置で決めた元の形のまま。途中の形を元の形として控えない
TEST(CollisionImpact, HitDuringTheRecoveryStillRestoresTheAuthoredScale)
{
    SceneNs::Scene scene;
    // 反動で下がる側 (-X) にもう 1 体置き、明けに空中の 1 発で当てる
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .targetCell = 1, .extraTargetCell = -2});
    rig.rigidBody->SetMass(4.0f);
    const Vector3 authored = rig.movement->Owner()->Root().Scale();
    // 溜めた突進は床を走るので、明けは接地から弾かれ、空中の 1 発がまだ使える
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    // 検知のフレームの移動も走らせる。飛ばすと自機の控えが「直前のフレームは突進中」のまま残り、明けの 1 発が出ない
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    rig.movement->OnUpdate();
    ASSERT_LT(StepsUntilMovementActive(scene, rig, 60), 60);
    // 溜めた 1 発は踏み込みより速く、形を戻すフレーム数の内にもう 1 体へ届く
    rig.movement->SetDesiredMove(Vector3{-1.0f, 0.0f, 0.0f}, 0.0f);
    rig.movement->RequestBodySlam(1.0f);
    rig.movement->OnUpdate();
    ASSERT_TRUE(rig.movement->IsBodySlamming());

    ASSERT_LT(StepUntilImpact(scene, rig, 5), 5);
    // 戻しの途中の、元と違う形で当たったことを先に見る。元の形で当たったなら控え直しても同じ値になる
    ASSERT_TRUE(rig.impact->IsScaleAnimating());
    ASSERT_NE(rig.movement->Owner()->Root().Scale().y, authored.y);
    rig.movement->OnUpdate();
    ASSERT_LT(StepsUntilMovementActive(scene, rig, 60), 60);
    for (int i = 0; i < 6; ++i)
    {
        Step(scene, rig);
    }

    ASSERT_FALSE(rig.impact->IsScaleAnimating());
    const Vector3 restored = rig.movement->Owner()->Root().Scale();
    EXPECT_EQ(restored.x, authored.x);
    EXPECT_EQ(restored.y, authored.y);
    EXPECT_EQ(restored.z, authored.z);
}

// 潰れは絵だけ。凍結中も当たり判定と位置は変わらない
TEST(CollisionImpact, SquashLeavesPositionAndPhysicsAlone)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.rigidBody->SetMass(4.0f);
    const JPH::uint bodies = scene.Physics().BodyCount();
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const Vector3 frozenPos = rig.movement->Owner()->Root().Position();

    Step(scene, rig);
    const Vector3 stillPos = rig.movement->Owner()->Root().Position();
    EXPECT_FLOAT_EQ(stillPos.x, frozenPos.x);
    EXPECT_FLOAT_EQ(stillPos.y, frozenPos.y);
    EXPECT_FLOAT_EQ(stillPos.z, frozenPos.z);
    EXPECT_EQ(scene.Physics().BodyCount(), bodies);
}

// 耐久 0 の最も脆い相手へ最大の勢いで当てても壊れない。壊れて消えると重さが飛距離に出ない
TEST(CollisionImpact, BreakIsOffByDefault)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    rig.breakable->SetToughness(0.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 1.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_FALSE(rig.impact->DidBreak());
    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_TRUE(rig.breakable->IsActiveSelf());
    EXPECT_TRUE(DebrisBodies(scene).empty());
    LevelNs::LaunchedBody* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());
}

// 戻し口は欄 1 つ。立てれば耐久と威力の比較がそのまま効く
TEST(CollisionImpact, BreakFieldReenablesBreaking)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    EnableBreak(rig);
    rig.breakable->SetToughness(0.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 1.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_TRUE(rig.impact->DidBreak());
    EXPECT_FALSE(rig.impact->DidRebound());
    EXPECT_FALSE(rig.breakable->IsActiveSelf());
    EXPECT_FALSE(rig.targetBox->IsActiveSelf());
}

TEST(CollisionImpact, PlainHitBreaksThroughSoftTarget)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    rig.breakable->SetToughness(1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidBreak());
    EXPECT_FALSE(rig.impact->DidRebound());
    EXPECT_TRUE(rig.movement->IsActiveSelf());

    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);

    EXPECT_FALSE(rig.breakable->IsActiveSelf());
    EXPECT_FALSE(rig.targetBox->IsActiveSelf());
    // 数では見ない。壊すと破片が飛び、破片ぶんの body が増える
    EXPECT_TRUE(rig.targetBox->BodyId().IsInvalid());
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, k_TapSlamSpeed * 0.75f);
    EXPECT_FLOAT_EQ(rig.movement->Velocity().z, 0.0f);
}

TEST(CollisionImpact, PlainHitReboundsOffToughTarget)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    rig.breakable->SetToughness(99.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_FALSE(rig.impact->DidBreak());

    Step(scene, rig);
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);

    EXPECT_TRUE(rig.breakable->IsActiveSelf());
    EXPECT_LT(rig.movement->Velocity().x, 0.0f);
}

TEST(CollisionImpact, NormalHitAtStartCannotBreakToughTwo)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    rig.breakable->SetToughness(2.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_TRUE(rig.impact->DidRebound());
    EXPECT_FALSE(rig.impact->DidBreak());
    EXPECT_TRUE(rig.breakable->IsActiveSelf());
}

TEST(CollisionImpact, ChargedCenterHitBeatsPlainEdgeHit)
{
    SceneNs::Scene chargedScene;
    Rig charged = BuildSlam(chargedScene, k_FarCourse);
    EnableBreak(charged);
    charged.breakable->SetToughness(k_ReversalToughness);
    BeginSlam(chargedScene, charged, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(chargedScene, charged, 30), 30);

    SceneNs::Scene plainScene;
    Rig plain = BuildSlam(plainScene, k_EdgeCourse);
    EnableBreak(plain);
    plain.breakable->SetToughness(k_ReversalToughness);
    BeginSlam(plainScene, plain, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(plainScene, plain, 30), 30);

    EXPECT_TRUE(charged.impact->WasCenterHit());
    EXPECT_TRUE(charged.impact->DidBreak());
    EXPECT_FALSE(plain.impact->DidBreak());
    EXPECT_TRUE(plain.impact->DidRebound());
    EXPECT_GT(charged.impact->LastPower(), plain.impact->LastPower());
}

// 走る距離と速度と溜めを揃えてあるので、差が出れば原因は横ずれだけ
TEST(CollisionImpact, CenterHitFlagFollowsHitOffset)
{
    SceneNs::Scene centerScene;
    Rig center = BuildSlam(centerScene, k_NearCourse);
    SetInstantImpact(center);
    BeginSlam(centerScene, center, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(centerScene, center, 30), 30);

    SceneNs::Scene edgeScene;
    Rig edge = BuildSlam(edgeScene, k_EdgeCourse);
    SetInstantImpact(edge);
    BeginSlam(edgeScene, edge, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(edgeScene, edge, 30), 30);

    EXPECT_TRUE(center.impact->WasCenterHit());
    EXPECT_FALSE(edge.impact->WasCenterHit());
    EXPECT_GT(center.impact->LastPositionFactor(), edge.impact->LastPositionFactor());
    EXPECT_GT(center.impact->LastPower(), edge.impact->LastPower());
}

// 助走の長さが位置係数に混ざると、画面に出ない間合いを当てさせる作りへ戻る
TEST(CollisionImpact, PositionFactorIgnoresRushDistance)
{
    SceneNs::Scene nearScene;
    Rig nearHit = BuildSlam(nearScene, k_NearCourse);
    SetInstantImpact(nearHit);
    BeginSlam(nearScene, nearHit, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(nearScene, nearHit, 30), 30);

    SceneNs::Scene farScene;
    Rig farHit = BuildSlam(farScene, k_FarCourse);
    SetInstantImpact(farHit);
    BeginSlam(farScene, farHit, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(farScene, farHit, 30), 30);

    EXPECT_FLOAT_EQ(nearHit.impact->LastPositionFactor(), farHit.impact->LastPositionFactor());
}

// 分母が相手の半幅だけだと、自機の半径ぶん外で触れた当たりが全部 1 に張り付き、縁の近さが数に出ない
// 縁の台は触れられる横ずれの上限 (的の半幅 0.5 + 自機の半径 0.4) の手前で当て、1 の手前に来ることを見る
TEST(CollisionImpact, HitOffsetDividesByTheTargetHalfWidthPlusThePlayerRadius)
{
    SceneNs::Scene centerScene;
    Rig center = BuildSlam(centerScene, k_NearCourse);
    SetInstantImpact(center);
    BeginSlam(centerScene, center, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(centerScene, center, 30), 30);

    // 0.9 は上限ちょうどで当たらないので、触れる所まで下げた
    constexpr float k_WidestLateral = 0.88f;
    SceneNs::Scene edgeScene;
    Rig edge = BuildSlam(edgeScene, SlamCourse{.start = 0.0f, .lateral = k_WidestLateral, .targetCell = 1});
    SetInstantImpact(edge);
    BeginSlam(edgeScene, edge, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(edgeScene, edge, 30), 30);

    const float width = 0.5f + edge.movement->CapsuleRadius();
    EXPECT_NEAR(center.impact->LastImpact().offset01, 0.0f, 0.01f);
    EXPECT_NEAR(edge.impact->LastImpact().offset01, k_WidestLateral / width, 0.01f);
}

// 横ずれ 0.2 / 0.45 / 0.7 は、的の半幅 0.5 + 自機の半径 0.4 で割ると 0.22 / 0.5 / 0.78。既定の境目 0.35 と 0.7 で 3
// 段に分かれる
TEST(CollisionImpact, HitTierFollowsTheOffsetInThreeSteps)
{
    struct TierCase
    {
        float lateral = 0.0f;
        LevelNs::HitTier tier = LevelNs::HitTier::Center;
    };
    const std::vector<TierCase> cases{
        {0.2f, LevelNs::HitTier::Center},
        {0.45f, LevelNs::HitTier::Near},
        {0.7f, LevelNs::HitTier::Wide},
    };

    for (const TierCase& tierCase : cases)
    {
        SceneNs::Scene scene;
        Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .lateral = tierCase.lateral, .targetCell = 1});
        SetInstantImpact(rig);
        BeginSlam(scene, rig, k_RunSpeed, 0.0f);
        ASSERT_LT(StepUntilImpact(scene, rig, 30), 30) << tierCase.lateral;

        const LevelNs::ImpactRecord& hit = rig.impact->LastImpact();
        EXPECT_EQ(hit.tier, tierCase.tier) << tierCase.lateral;
        EXPECT_EQ(hit.centerHit, tierCase.tier == LevelNs::HitTier::Center) << tierCase.lateral;
    }
}

// 寄せる相手は前方の角度と距離の内に居る物だけで、2 体居れば近い方。真横や後ろの相手へ曲がると狙った先から外れる
TEST(CollisionImpact, HomingTargetIsTheNearestAheadWithinTheConeAndTheDistance)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.targetCell = 3, .extraTargetCell = 2});
    ASSERT_NE(rig.impact, nullptr);

    std::vector<NS::Core::AABB> targets;
    scene.Objects().ForEachComponent<LevelNs::Breakable>([&targets](LevelNs::Breakable& breakable) {
        NS::Core::AABB bounds{};
        if (LevelNs::TryGetColliderBounds(*breakable.Owner(), bounds))
        {
            targets.push_back(bounds);
        }
    });
    ASSERT_EQ(targets.size(), 2u);
    const float nearestX = std::min(targets[0].Center.x, targets[1].Center.x);
    const float farthestX = std::max(targets[0].Center.x, targets[1].Center.x);
    ASSERT_LT(nearestX, farthestX);

    Vector3 center{};
    ASSERT_TRUE(rig.impact->FindHomingTarget(Vector3{1.0f, 0.0f, 0.0f}, 30.0f, 6.0f, center));
    EXPECT_FLOAT_EQ(center.x, nearestX);

    EXPECT_FALSE(rig.impact->FindHomingTarget(Vector3{0.0f, 0.0f, 1.0f}, 30.0f, 6.0f, center));
    EXPECT_FALSE(rig.impact->FindHomingTarget(Vector3{-1.0f, 0.0f, 0.0f}, 30.0f, 6.0f, center));
    EXPECT_FALSE(rig.impact->FindHomingTarget(Vector3{1.0f, 0.0f, 0.0f}, 30.0f, 1.0f, center));
}

// 同じ横ずれで放しても、寄せる角度の上限が 0 より 3 の方が相手の中心の近くに当たる。寄せた角度は上限を超えない
TEST(CollisionImpact, HomingNarrowsTheHitOffsetWithinTheLimit)
{
    struct HomingCase
    {
        float limitDegrees = 0.0f;
        float offset01 = 0.0f;
        float largestHoming = 0.0f;
    };
    std::vector<HomingCase> cases{{0.0f}, {3.0f}};

    for (HomingCase& homingCase : cases)
    {
        SceneNs::Scene scene;
        Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .lateral = 0.45f, .targetCell = 4});
        SetInstantImpact(rig);
        SetFloatField(*rig.movement, "寄せる角度の上限", homingCase.limitDegrees);
        BeginSlam(scene, rig, k_RunSpeed, 1.0f);
        ASSERT_TRUE(rig.movement->IsBodySlamming());

        bool hit = false;
        for (int i = 0; i < 30 && !hit; ++i)
        {
            StepWorld(scene);
            hit = rig.impact->DidRebound() || rig.impact->DidBreak();
            homingCase.largestHoming = std::max(homingCase.largestHoming, std::abs(rig.movement->HomingAngleDegrees()));
            if (!hit)
            {
                rig.movement->OnUpdate();
            }
        }
        ASSERT_TRUE(hit) << homingCase.limitDegrees;
        homingCase.offset01 = rig.impact->LastImpact().offset01;
    }

    EXPECT_FLOAT_EQ(cases[0].largestHoming, 0.0f);
    EXPECT_GT(cases[1].largestHoming, 0.0f);
    EXPECT_LE(cases[1].largestHoming, 3.0f + 1e-4f);
    EXPECT_LT(cases[1].offset01, cases[0].offset01);
}

namespace
{
    // BuildSlam の rig.target でない方の壊せる物。2 体目を置いていなければ nullptr
    NS::Obj::GameObject* OtherTarget(SceneNs::Scene& scene, const Rig& rig)
    {
        NS::Obj::GameObject* other = nullptr;
        scene.Objects().ForEachComponent<LevelNs::Breakable>([&](LevelNs::Breakable& breakable) {
            if (breakable.Owner() != rig.target)
            {
                other = breakable.Owner();
            }
        });
        return other;
    }

    // 置いた姿勢と拡縮へ物理の body を置き直す。予測は玉を掃いて body に触れるかを見るので、Transform だけでは足りない
    void SyncBodyToTransform(NS::Obj::GameObject& object)
    {
        SceneNs::RigidBody* rigidBody = object.FindComponent<SceneNs::RigidBody>();
        SceneNs::Scene* scene = object.OwningScene();
        ASSERT_NE(rigidBody, nullptr);
        ASSERT_NE(scene, nullptr);
        rigidBody->SyncToPhysics(scene->Physics());
    }

    // 高さを保って水平の位置だけを置き直し、物理の body も同じ所へ置き直す
    void PlaceHorizontally(NS::Obj::GameObject& object, float x, float z)
    {
        const Vector3 position = object.Root().Position();
        object.Root().SetPosition(Vector3{x, position.y, z});
        SyncBodyToTransform(object);
    }
} // namespace

// 予測の横ずれの比と段は裁定と同じ HitZones::Judge で出すので、同じ置き方で当てた時の横ずれと段と同じになる
// 横ずれ 0 / 0.45 / 0.765 は、的の半幅 0.5 + 自機の半径 0.4 で割ると 0 / 0.5 / 0.85
// 試験の的の範囲 0.315 m / 0.63 m では真ん中 / 惜しい / 外れで、3 つの段を 1 回ずつ通る
TEST(CollisionImpact, SlamLineTargetOffsetMatchesTheOffsetOfTheHit)
{
    const LevelNs::HitTier expectedTiers[] = {LevelNs::HitTier::Center, LevelNs::HitTier::Near, LevelNs::HitTier::Wide};
    const float laterals[] = {0.0f, 0.45f, 0.765f};
    for (int i = 0; i < 3; ++i)
    {
        const float lateral = laterals[i];
        SceneNs::Scene scene;
        Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .lateral = lateral, .targetCell = 1});
        SetInstantImpact(rig);
        SettleOnFloor(scene, rig);

        LevelNs::SlamLineTarget predicted{};
        ASSERT_TRUE(rig.impact->FindSlamLineTarget(Vector3{1.0f, 0.0f, 0.0f}, 10.0f, predicted)) << lateral;
        NS::Core::AABB bounds{};
        ASSERT_TRUE(LevelNs::TryGetColliderBounds(*rig.target, bounds));
        const Vector3 position = rig.movement->Owner()->Root().Position();
        EXPECT_EQ(predicted.target, SceneNs::ObjectRef{rig.target->Id()}) << lateral;
        EXPECT_FLOAT_EQ(predicted.bounds.Center.x, bounds.Center.x) << lateral;
        EXPECT_FLOAT_EQ(predicted.bounds.Center.z, bounds.Center.z) << lateral;
        EXPECT_NEAR(predicted.along, bounds.Center.x - position.x, 1e-4f) << lateral;
        EXPECT_NEAR(predicted.offset, lateral / (0.5f + rig.movement->CapsuleRadius()), 1e-3f) << lateral;
        EXPECT_EQ(predicted.tier, expectedTiers[i]) << lateral;

        BeginSlam(scene, rig, k_RunSpeed, 0.0f);
        ASSERT_LT(StepUntilImpact(scene, rig, 30), 30) << lateral;
        EXPECT_NEAR(predicted.offset, rig.impact->LastImpact().offset01, 1e-3f) << lateral;
        EXPECT_EQ(predicted.tier, rig.impact->LastImpact().tier) << lateral;
    }
}

// 段の範囲を持たない壊せる物は、当たり・寄せる相手の探索・予測のどれにも出ない
TEST(CollisionImpact, TargetWithoutHitZonesIsNotARushTarget)
{
    SceneNs::Scene scene;
    SlamCourse course = k_NearCourse;
    course.targetWithHitZones = false;
    Rig rig = BuildSlam(scene, course);
    ASSERT_NE(rig.breakable, nullptr);
    SetInstantImpact(rig);
    SettleOnFloor(scene, rig);

    LevelNs::SlamLineTarget predicted{};
    EXPECT_FALSE(rig.impact->FindSlamLineTarget(Vector3{1.0f, 0.0f, 0.0f}, 10.0f, predicted));
    Vector3 center{};
    EXPECT_FALSE(rig.impact->FindHomingTarget(Vector3{1.0f, 0.0f, 0.0f}, 90.0f, 10.0f, center));

    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    EXPECT_EQ(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_FALSE(rig.impact->DidRebound());
}

// 線の外の近い相手より線の上の遠い相手を選ぶ。線の上に 2 体居れば、中心の近さでなく玉が先に触れる方
TEST(CollisionImpact, SlamLineTargetIsTheFirstAlongTheLine)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .targetCell = 4, .extraTargetCell = 2});
    SettleOnFloor(scene, rig);
    NS::Obj::GameObject* other = OtherTarget(scene, rig);
    ASSERT_NE(other, nullptr);
    const Vector3 position = rig.movement->Owner()->Root().Position();

    // 近い方を線から 2 m 横へ外す
    PlaceHorizontally(*rig.target, position.x + 4.0f, position.z);
    PlaceHorizontally(*other, position.x + 2.0f, position.z + 2.0f);
    LevelNs::SlamLineTarget found{};
    ASSERT_TRUE(rig.impact->FindSlamLineTarget(Vector3{1.0f, 0.0f, 0.0f}, 10.0f, found));
    EXPECT_EQ(found.target, SceneNs::ObjectRef{rig.target->Id()});

    // 横ずれ 0.85 で中心が線に沿って 3 m の相手は、真正面 3.1 m の相手より中心が手前だが、角をかすめるので奥で触れる
    // 真正面は 3.1 − 0.5 − 0.4 = 2.2 m、横の相手は 3.0 − 0.5 − √(0.4² − 0.35²) = 2.31 m 進んだ所
    PlaceHorizontally(*rig.target, position.x + 3.1f, position.z);
    PlaceHorizontally(*other, position.x + 3.0f, position.z + 0.85f);
    ASSERT_TRUE(rig.impact->FindSlamLineTarget(Vector3{1.0f, 0.0f, 0.0f}, 10.0f, found));
    EXPECT_EQ(found.target, SceneNs::ObjectRef{rig.target->Id()});
    EXPECT_NEAR(found.along, 3.1f, 1e-4f);
    EXPECT_NEAR(found.contact, 2.2f, 1e-3f);
}

// 触れる横の幅の外で比が 1 を超える相手、線に沿って見る距離の内で触れない相手、後ろの相手は返さない
TEST(CollisionImpact, SlamLineTargetSkipsTargetsOffTheReachOutOfRangeAndBehind)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .targetCell = 3});
    SettleOnFloor(scene, rig);
    const Vector3 position = rig.movement->Owner()->Root().Position();
    const Vector3 forward{1.0f, 0.0f, 0.0f};
    LevelNs::SlamLineTarget found{};

    // 横ずれ 1.0 は的の半幅 0.5 + 自機の半径 0.4 = 0.9 の外で、比は 1.11
    PlaceHorizontally(*rig.target, position.x + 3.0f, position.z + 1.0f);
    EXPECT_FALSE(rig.impact->FindSlamLineTarget(forward, 10.0f, found));

    // 3.0 m 先の的に自機の縁が触れるのは 3.0 − 的の半分の奥行き 0.5 − 自機の半径 0.4 = 2.1 m 進んだ所
    PlaceHorizontally(*rig.target, position.x + 3.0f, position.z);
    EXPECT_FALSE(rig.impact->FindSlamLineTarget(forward, 2.0f, found));
    EXPECT_TRUE(rig.impact->FindSlamLineTarget(forward, 2.2f, found));
    EXPECT_FALSE(rig.impact->FindSlamLineTarget(Vector3{-1.0f, 0.0f, 0.0f}, 10.0f, found));
}

// 中心が見る距離の外でも、自機の縁が触れる所が内の大きい相手は返す。線に沿った距離は中心までのまま
// 6 倍の的は半分の奥行き 3.0 で、10 m 先の中心に対して触れる所は 10 − 3.0 − 0.4 = 6.6 m
TEST(CollisionImpact, SlamLineTargetReachesALargeTargetByItsNearFace)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .targetCell = 3});
    SettleOnFloor(scene, rig);
    const Vector3 position = rig.movement->Owner()->Root().Position();
    rig.target->Root().SetScale(Vector3{6.0f, 6.0f, 6.0f});
    PlaceHorizontally(*rig.target, position.x + 10.0f, position.z);

    LevelNs::SlamLineTarget found{};
    ASSERT_TRUE(rig.impact->FindSlamLineTarget(Vector3{1.0f, 0.0f, 0.0f}, 8.0f, found));
    EXPECT_EQ(found.target, SceneNs::ObjectRef{rig.target->Id()});
    EXPECT_NEAR(found.along, 10.0f, 1e-4f);
}

// 高い所に中心がある大きな球は、線から横へ離れるほど玉が触れる所が奥へ下がる。見積もりが届くと言う時だけ
// 本当の突進も当たり、触れる所は当たったフレームの玉の位置と 1 フレームの進みの内で合う。寄せを外して真っすぐ走らせる
// 半径 2.75 の球を床の上面 0.5 に載せると中心の高さは 3.25 で、玉 (半径 0.4) の中心 0.9 との差は 2.35。中心は 11.5 m 先
// 横 0.5 m は触れる所が 11.5 − √(3.15² − 0.5² − 2.35²) = 9.46 m で、突進距離 10 m の内
// 横 2.05 m は 11.5 − √(3.15² − 2.05² − 2.35²) = 11.05 m で外。外接箱を水平に見ると 11.5 − 3.15 = 8.35 m で内に出る
TEST(CollisionImpact, SlamLineTargetReachesATallLargeTargetOnlyWhereTheRushTouchesIt)
{
    for (const float lateral : {0.5f, 2.05f})
    {
        SceneNs::Scene scene;
        Rig rig = BuildSlam(
            scene, SlamCourse{.start = 0.0f, .targetCell = 12, .withCollisionInput = false, .sphereTarget = true});
        SetInstantImpact(rig);
        SettleOnFloor(scene, rig);
        rig.movement->SetCurled(true);
        const Vector3 origin = rig.movement->Owner()->Root().Position();
        rig.target->Root().SetScale(Vector3{5.5f, 5.5f, 5.5f});
        rig.target->Root().SetPosition(Vector3{origin.x + 11.5f, 3.25f, origin.z - lateral});
        SyncBodyToTransform(*rig.target);

        LevelNs::SlamLineTarget found{};
        const bool predicted =
            rig.impact->FindSlamLineTarget(Vector3{1.0f, 0.0f, 0.0f}, rig.movement->BodySlamDistance(), found);

        BeginSlam(scene, rig, k_RunSpeed, 1.0f);
        ASSERT_TRUE(rig.movement->IsBodySlamming()) << lateral;
        const bool hit = StepUntilImpact(scene, rig, 40) < 40;
        EXPECT_EQ(predicted, hit) << lateral;
        if (!predicted || !hit)
        {
            continue;
        }
        EXPECT_EQ(found.target, SceneNs::ObjectRef{rig.target->Id()}) << lateral;
        EXPECT_EQ(rig.impact->LastImpact().targetId, rig.target->Id()) << lateral;
        // 裁定は玉を 1 フレーム進めた所で重なりを見る。当たったフレームの位置からその先までの間で初めて触れている
        const float travelled = rig.movement->Owner()->Root().Position().x - origin.x;
        EXPECT_GT(found.contact, travelled - 1e-3f) << lateral;
        EXPECT_LE(found.contact, travelled + k_SlamSpeed * k_FixedDt + 1e-3f) << lateral;
    }
}

// 裁定と同じ絞り。有効でない相手とトリガの箱は、線の上に居ても返さない
TEST(CollisionImpact, SlamLineTargetSkipsInactiveAndTriggerTargets)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .targetCell = 3});
    SettleOnFloor(scene, rig);
    ASSERT_NE(rig.targetBox, nullptr);
    const Vector3 forward{1.0f, 0.0f, 0.0f};
    LevelNs::SlamLineTarget found{};
    ASSERT_TRUE(rig.impact->FindSlamLineTarget(forward, 10.0f, found));

    rig.targetBox->SetTrigger(true);
    EXPECT_FALSE(rig.impact->FindSlamLineTarget(forward, 10.0f, found));

    rig.targetBox->SetTrigger(false);
    rig.breakable->SetActive(false);
    EXPECT_FALSE(rig.impact->FindSlamLineTarget(forward, 10.0f, found));
}

// 線の向きが決まらない時は探さず、結果を書き換えない
TEST(CollisionImpact, SlamLineTargetRejectsAZeroOrNonFiniteDirection)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .targetCell = 3});
    SettleOnFloor(scene, rig);

    LevelNs::SlamLineTarget kept{};
    kept.target = SceneNs::ObjectRef{9999};
    kept.along = 123.0f;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (const Vector3& direction : {Vector3{0.0f, 0.0f, 0.0f},
                                     Vector3{0.0f, 1.0f, 0.0f},
                                     Vector3{nan, 0.0f, 0.0f},
                                     Vector3{infinity, 0.0f, 0.0f}})
    {
        EXPECT_FALSE(rig.impact->FindSlamLineTarget(direction, 10.0f, kept));
        EXPECT_EQ(kept.target, SceneNs::ObjectRef{9999});
        EXPECT_FLOAT_EQ(kept.along, 123.0f);
    }
}

// 溜め中に最高速へ掛ける倍率は 1 − 減速率。0〜1 の外へ出る減速率は端へ寄せる
TEST(CollisionInput, ChargingSpeedScaleIsWhatTheSlowRateLeaves)
{
    SceneNs::GameObject owner;
    LevelNs::CollisionInput* input = owner.AddComponent<LevelNs::CollisionInput>();
    ASSERT_NE(input, nullptr);

    EXPECT_FLOAT_EQ(input->ChargingSpeedScale(), 0.3f);

    SetFloatField(*input, "チャージ減速率", 0.3f);
    EXPECT_FLOAT_EQ(input->ChargingSpeedScale(), 0.7f);

    SetFloatField(*input, "チャージ減速率", 1.5f);
    EXPECT_FLOAT_EQ(input->ChargingSpeedScale(), 0.0f);

    SetFloatField(*input, "チャージ減速率", -0.5f);
    EXPECT_FLOAT_EQ(input->ChargingSpeedScale(), 1.0f);
}

TEST(CollisionImpact, StoresChargeAndPositionForNextPhase)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    EXPECT_FLOAT_EQ(rig.impact->LastCharge01(), 1.0f);
    EXPECT_FLOAT_EQ(rig.impact->LastPositionFactor(), 1.0f);
    EXPECT_FLOAT_EQ(rig.impact->LastPower(), 1.0f * 2.0f * 1.0f);
}

TEST(CollisionImpact, ChargeScalesPowerByCurve)
{
    SceneNs::Scene fullScene;
    Rig full = BuildSlam(fullScene, k_NearCourse);
    SetInstantImpact(full);
    BeginSlam(fullScene, full, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(fullScene, full, 30), 30);

    SceneNs::Scene halfScene;
    Rig half = BuildSlam(halfScene, k_NearCourse);
    SetInstantImpact(half);
    BeginSlam(halfScene, half, k_RunSpeed, 0.5f);
    ASSERT_LT(StepUntilImpact(halfScene, half, 30), 30);

    EXPECT_FLOAT_EQ(full.impact->LastPower(), half.impact->LastPower() * 2.0f / 1.5f);
    LevelNs::LaunchedBody* fullBody = HitBody(full);
    LevelNs::LaunchedBody* halfBody = HitBody(half);
    ASSERT_NE(fullBody, nullptr);
    ASSERT_NE(halfBody, nullptr);
    EXPECT_GT(HorizontalSpeed(fullBody->Velocity()), HorizontalSpeed(halfBody->Velocity()));
}

TEST(CollisionImpact, TapImpactIsWeakerThanCharged)
{
    SceneNs::Scene tapScene;
    Rig tap = BuildSlam(tapScene, k_NearCourse);
    SetInstantImpact(tap);
    BeginSlam(tapScene, tap, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(tapScene, tap, 30), 30);

    SceneNs::Scene chargedScene;
    Rig charged = BuildSlam(chargedScene, k_NearCourse);
    SetInstantImpact(charged);
    BeginSlam(chargedScene, charged, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(chargedScene, charged, 30), 30);

    EXPECT_FLOAT_EQ(tap.impact->LastCharge01(), 0.0f);
    EXPECT_LT(tap.impact->LastPower(), charged.impact->LastPower());
}

// キーを離すと減速が始まる。実速度で威力が変わると、同じ助走で当てたのに飛びが揺れる
TEST(CollisionImpact, PowerIgnoresEntrySpeed)
{
    SceneNs::Scene fullScene;
    Rig full = BuildSlam(fullScene, k_NearCourse);
    SetInstantImpact(full);
    BeginSlam(fullScene, full, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(fullScene, full, 30), 30);

    SceneNs::Scene slowScene;
    Rig slow = BuildSlam(slowScene, k_NearCourse);
    SetInstantImpact(slow);
    BeginSlam(slowScene, slow, 2.0f, 0.0f);
    ASSERT_LT(StepUntilImpact(slowScene, slow, 30), 30);

    ASSERT_TRUE(full.impact->DidRebound());
    ASSERT_TRUE(slow.impact->DidRebound());
    EXPECT_FLOAT_EQ(slow.impact->LastPower(), full.impact->LastPower());
    EXPECT_FLOAT_EQ(HorizontalSpeed(slow.movement->Velocity()), HorizontalSpeed(full.movement->Velocity()));
}

TEST(CollisionImpact, StandingChargedSlamStillCarriesPower)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    EnableBreak(rig);
    rig.breakable->SetToughness(0.6f);
    SettleOnFloor(scene, rig);
    rig.movement->SetVelocity(Vector3{0.0f, 0.0f, 0.0f});
    rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
    rig.movement->RequestBodySlam(1.0f);
    Step(scene, rig);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_TRUE(rig.impact->DidBreak());

    SceneNs::Scene runScene;
    Rig run = BuildSlam(runScene, k_NearCourse);
    SetInstantImpact(run);
    EnableBreak(run);
    run.breakable->SetToughness(0.6f);
    BeginSlam(runScene, run, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpact(runScene, run, 30), 30);

    EXPECT_FLOAT_EQ(rig.impact->LastPower(), run.impact->LastPower());
}

// 反発後の後ろ滑りなど残った速度が向きに勝つと狙いと食い違う方へ飛ぶ。入力が無い発動はカメラの前へ出す
TEST(CollisionImpact, SlamWithoutInputAimsCameraForward)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SettleOnFloor(scene, rig);
    rig.movement->SetVelocity(Vector3{-2.0f, 0.0f, 0.0f});
    rig.movement->RequestBodySlam(1.0f);
    Step(scene, rig);

    ASSERT_TRUE(rig.movement->IsBodySlamming());
    const Vector3 velocity = rig.movement->BodySlamVelocity();
    EXPECT_GT(velocity.z, 0.0f);
    EXPECT_NEAR(velocity.x, 0.0f, 1e-3f);
}

TEST(CollisionImpact, CenterHitStretchesHitStop)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_FarCourse);
    // 既定の基準秒では上限 12 フレームで頭打ちになるため、中心近くの当たりの倍率がフレーム数に出るまで基準を下げる
    SetFloatField(*rig.impact, "ヒットストップ基準秒", 2.0f / 60.0f);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->WasCenterHit());
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    const int expected = static_cast<int>(std::lround(2.0f * rig.impact->LastPower() * 2.0f));
    EXPECT_EQ(StepsUntilMovementActive(scene, rig, 60), expected);
}

// 白は検知のフレームには出ず、潰れと同じ止めの頭から 6 フレーム出る。引いたフレームには、まだ潰れが残っている
TEST(CollisionImpact, CenterHitFlashRunsFromTheFreezeFrameWhileTheSquashIsHeld)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_FarCourse);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->WasCenterHit());
    EXPECT_EQ(rig.impact->CenterHitFlashStepsRemaining(), 0);

    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    for (int remaining = 6; remaining > 0; --remaining)
    {
        EXPECT_EQ(rig.impact->CenterHitFlashStepsRemaining(), remaining);
        Step(scene, rig);
    }
    EXPECT_EQ(rig.impact->CenterHitFlashStepsRemaining(), 0);
    EXPECT_TRUE(rig.impact->IsScaleAnimating());
}

// 止めの頭と明けは、そのフレームの 1 回だけ読める。応答の Component が止めのフレーム数を数え直さずに済む
// charge_and_hit と同じく、溜めきりで中心近くに当てて止めを上限まで伸ばす
TEST(CollisionImpact, FreezeBeganAndReleasedAreReadOnlyOnTheirOwnSteps)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_FarCourse);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    int detected = -1;
    std::vector<int> began;
    std::vector<int> released;
    for (int step = 0; step < 60; ++step)
    {
        Step(scene, rig);
        if (detected < 0 && rig.impact->DidRebound())
        {
            detected = step;
        }
        if (rig.impact->FreezeBeganThisStep())
        {
            began.push_back(step);
        }
        if (rig.impact->ReleasedThisStep())
        {
            released.push_back(step);
        }
    }

    ASSERT_GE(detected, 0);
    ASSERT_TRUE(rig.impact->WasCenterHit());
    const int stopSteps = rig.impact->LastImpact().hitStopSteps;
    ASSERT_GT(stopSteps, 1);
    ASSERT_EQ(began.size(), 1u);
    EXPECT_EQ(began[0], detected + 1);
    ASSERT_EQ(released.size(), 1u);
    EXPECT_EQ(released[0], detected + 1 + stopSteps);
}

// 止めが 0 の当たりは検知のフレームのうちに明けを済ませ、返りを出さない (判断 143)。頭も明けも立てない
TEST(CollisionImpact, HitWithoutAFreezeRaisesNeitherTheFreezeNorTheRelease)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    bool detected = false;
    for (int step = 0; step < 30; ++step)
    {
        Step(scene, rig);
        if (rig.impact->DidRebound())
        {
            detected = true;
        }
        EXPECT_FALSE(rig.impact->FreezeBeganThisStep());
        EXPECT_FALSE(rig.impact->ReleasedThisStep());
    }
    ASSERT_TRUE(detected);
}

TEST(CollisionImpact, ButtonReleaseStartsBodySlam)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.input, nullptr);
    SettleOnFloor(scene, rig);
    rig.movement->SetVelocity(Vector3{k_RunSpeed, 0.0f, 0.0f});

    for (int i = 0; i < 15; ++i)
        rig.input->Judge().Step(true);
    ASSERT_TRUE(rig.input->IsCharging());

    Step(scene, rig);

    EXPECT_TRUE(rig.movement->IsBodySlamming());
    EXPECT_GT(rig.movement->BodySlamCharge01(), 0.0f);
}

namespace
{
    // 押しの入口は CollisionInput が読む実機のマウスなので、試しの後に押したまま残すと他の試しが押しを拾う
    struct MouseLeftPress
    {
        MouseLeftPress() noexcept { NS::Platform::Input::Get().Mouse().OnButtonDown(NS::Platform::MouseButton::Left); }
        ~MouseLeftPress() noexcept { NS::Platform::Input::Get().Mouse().ClearState(); }
        MouseLeftPress(const MouseLeftPress&) = delete;
        MouseLeftPress& operator=(const MouseLeftPress&) = delete;

        void Release() noexcept { NS::Platform::Input::Get().Mouse().OnButtonUp(NS::Platform::MouseButton::Left); }
    };
} // namespace

// 溜め中は最高速が走行速度の 0.3 倍へ落ち、放すと 1 倍へ戻る。押しっぱなしで動き回るのを最適にしない
TEST(CollisionImpact, ChargingSlowsTheMaxSpeedUntilTheRelease)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_FarCourse);
    ASSERT_NE(rig.input, nullptr);
    SettleOnFloor(scene, rig);
    ASSERT_FLOAT_EQ(rig.movement->MaxSpeed(), rig.movement->RunSpeed());

    MouseLeftPress press;
    for (int i = 0; i < 16; ++i)
    {
        Step(scene, rig);
    }
    ASSERT_TRUE(rig.input->IsCharging());
    EXPECT_FLOAT_EQ(rig.movement->MaxSpeed(), rig.movement->RunSpeed() * 0.3f);

    press.Release();
    Step(scene, rig);
    ASSERT_FALSE(rig.input->IsCharging());
    EXPECT_FLOAT_EQ(rig.movement->MaxSpeed(), rig.movement->RunSpeed());
}

// 狙う相手は押している間だけ控える。押す前は無く、溜めに入る前の押しでも控え、放したフレームから消える
TEST(CollisionImpact, AimTargetIsKeptWhileHeldAndDroppedOnRelease)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .targetCell = 4});
    ASSERT_NE(rig.input, nullptr);
    SettleOnFloor(scene, rig);
    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{1.0f, 0.0f, 0.0f}));
    rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
    LevelNs::SlamLineTarget aim{};

    Step(scene, rig);
    EXPECT_FALSE(rig.input->TryGetAimTarget(aim));

    MouseLeftPress press;
    Step(scene, rig);
    ASSERT_TRUE(rig.input->Judge().IsHeld());
    ASSERT_FALSE(rig.input->IsCharging());
    ASSERT_TRUE(rig.input->TryGetAimTarget(aim));
    EXPECT_EQ(aim.target, SceneNs::ObjectRef{rig.target->Id()});

    for (int i = 0; i < 15; ++i)
    {
        Step(scene, rig);
    }
    ASSERT_TRUE(rig.input->IsCharging());
    EXPECT_TRUE(rig.input->TryGetAimTarget(aim));

    press.Release();
    Step(scene, rig);
    EXPECT_FALSE(rig.input->TryGetAimTarget(aim));
}

// シーンの実カメラが無ければ、押している間も狙いの線を控えない。倒した向きへ線を引き直さない
TEST(CollisionImpact, AimLineIsNotKeptWithoutACamera)
{
    NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::GameObject owner;
    NS::Game::Player::PlayerComponent* movement = owner.AddComponent<NS::Game::Player::PlayerComponent>();
    LevelNs::CollisionInput* input = owner.AddComponent<LevelNs::CollisionInput>();
    movement->OnStart();
    input->OnStart();
    movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);

    MouseLeftPress press;
    input->OnUpdate();
    ASSERT_TRUE(input->Judge().IsHeld());
    LevelNs::AimLine line{};
    EXPECT_FALSE(input->TryGetAimLine(line));
}

namespace
{
    // UI の取り分は実機の Input に残るので、試しの後に戻さないと他の試しが押しを読めなくなる
    struct UiCaptureScope
    {
        explicit UiCaptureScope(const NS::Platform::UiCaptureDesc& desc) noexcept
        {
            NS::Platform::Input::Get().SetUiCapture(desc);
        }
        ~UiCaptureScope() noexcept { NS::Platform::Input::Get().SetUiCapture({}); }
        UiCaptureScope(const UiCaptureScope&) = delete;
        UiCaptureScope& operator=(const UiCaptureScope&) = delete;
    };
} // namespace

// エディタのプレイ中に Scene のタブの画像で左を押すと、UI がマウスを持ったままでも溜めの押しになる
TEST(CollisionImpact, LeftButtonPassedFromTheSceneTabIsReadAsTheSlamPress)
{
    NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::GameObject owner;
    NS::Game::Player::PlayerComponent* movement = owner.AddComponent<NS::Game::Player::PlayerComponent>();
    LevelNs::CollisionInput* input = owner.AddComponent<LevelNs::CollisionInput>();
    movement->OnStart();
    input->OnStart();

    MouseLeftPress press;
    {
        const UiCaptureScope capture({.wantMouse = true});
        input->OnUpdate();
        EXPECT_FALSE(input->Judge().IsHeld());
    }
    {
        const UiCaptureScope capture({.wantMouse = true, .leftButtonToGame = true});
        input->OnUpdate();
        EXPECT_TRUE(input->Judge().IsHeld());
    }
}

// 寄せの角度の内に居ても、狙いの線の外の相手は狙う相手にならない
TEST(CollisionImpact, AimTargetIsNotKeptForATargetOffTheLine)
{
    SceneNs::Scene scene;
    // 横ずれ 1.2 は的の半幅 0.5 + 自機の半径 0.4 = 0.9 の外。狙いの +X から 17 度・4.2 m
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .lateral = 1.2f, .targetCell = 4});
    ASSERT_NE(rig.input, nullptr);
    SettleOnFloor(scene, rig);
    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{1.0f, 0.0f, 0.0f}));
    rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
    Vector3 homingCenter{};
    ASSERT_TRUE(rig.impact->FindHomingTarget(Vector3{1.0f, 0.0f, 0.0f}, 30.0f, 6.0f, homingCenter));

    MouseLeftPress press;
    Step(scene, rig);
    Step(scene, rig);
    ASSERT_TRUE(rig.input->Judge().IsHeld());
    LevelNs::SlamLineTarget aim{};
    EXPECT_FALSE(rig.input->TryGetAimTarget(aim));
}

namespace
{
    // 自機を 0.3 m 横へずらし、線の上の的を 5 m 先 (横ずれ 0.3) に、2 体目を 4 m 先の 1.7 m 横に置く
    // 2 体目は寄せの角度 30 度と距離 6 m の内で線の上の的より近いが、線の外 (横ずれの比 1.9)
    // 正の寄せは +X を -Z の側へ回すので線の上の的の側
    constexpr SlamCourse k_NearerOffLineCourse{
        .start = 0.0f, .lateral = 0.3f, .targetCell = 5, .extraTargetCell = 4, .extraTargetLane = 2};
} // namespace

// 押している間、線の上の相手が寄せの角度と距離の内に居れば、それより近い線の外の相手でなく線の上の相手へ寄せる
TEST(CollisionImpact, HomingPrefersTheTargetOnTheLineOverANearerOneOffIt)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearerOffLineCourse);
    ASSERT_NE(rig.input, nullptr);
    SettleOnFloor(scene, rig);
    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{1.0f, 0.0f, 0.0f}));
    rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
    Vector3 nearest{};
    ASSERT_TRUE(rig.impact->FindHomingTarget(Vector3{1.0f, 0.0f, 0.0f}, 30.0f, 6.0f, nearest));
    ASSERT_NEAR(nearest.x, 4.0f, 1e-4f);

    MouseLeftPress press;
    for (int i = 0; i < 16; ++i)
    {
        Step(scene, rig);
    }
    ASSERT_TRUE(rig.input->IsCharging());
    EXPECT_GT(rig.movement->HomingAngleDegrees(), 0.0f);
}

// 線の上に相手が居なければ、今までどおり寄せの角度と距離の内で一番近い相手へ寄せる
TEST(CollisionImpact, HomingFallsBackToTheNearestWithoutATargetOnTheLine)
{
    SceneNs::Scene scene;
    // 線の上の的は突進の距離 10 m の外
    SlamCourse course = k_NearerOffLineCourse;
    course.targetCell = 12;
    Rig rig = BuildSlam(scene, course);
    ASSERT_NE(rig.input, nullptr);
    SettleOnFloor(scene, rig);
    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{1.0f, 0.0f, 0.0f}));
    rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);

    MouseLeftPress press;
    for (int i = 0; i < 16; ++i)
    {
        Step(scene, rig);
    }
    LevelNs::SlamLineTarget aim{};
    ASSERT_FALSE(rig.input->TryGetAimTarget(aim));
    EXPECT_LT(rig.movement->HomingAngleDegrees(), 0.0f);
}

// 溜めて放した突進は、放す前のフレームに矢印を引いたカメラの正面へ出る。溜めている間に倒した横と後ろの向きは見ない
TEST(CollisionImpact, ChargedReleaseRushesTowardTheCameraFrontWhateverTheStick)
{
    const Vector3 sticks[] = {Vector3{0.0f, 0.0f, 1.0f}, Vector3{-1.0f, 0.0f, 0.0f}};
    for (const Vector3& stick : sticks)
    {
        SceneNs::Scene scene;
        // 的は突進の距離 10 m と寄せる相手を探す距離 6 m の外に置き、寄せで向きが回らないようにする
        Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .targetCell = 12});
        ASSERT_NE(rig.input, nullptr);
        SettleOnFloor(scene, rig);
        ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{1.0f, 0.0f, 0.0f}));
        rig.movement->SetDesiredMove(stick, 1.0f);

        MouseLeftPress press;
        for (int i = 0; i < 16; ++i)
        {
            Step(scene, rig);
        }
        ASSERT_TRUE(rig.input->IsCharging());
        press.Release();
        Step(scene, rig);

        ASSERT_TRUE(rig.movement->IsBodySlamming());
        const Vector3 velocity = rig.movement->BodySlamVelocity();
        EXPECT_GT(velocity.x, 0.0f);
        EXPECT_NEAR(velocity.z, 0.0f, 1e-3f);
    }
}

// タップは矢印が出ないので、カメラの正面でなく倒した向きへ踏み込む
TEST(CollisionImpact, TapRushesAlongTheStickNotTheCameraFront)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .targetCell = 12});
    ASSERT_NE(rig.input, nullptr);
    SettleOnFloor(scene, rig);
    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{1.0f, 0.0f, 0.0f}));
    rig.movement->SetDesiredMove(Vector3{0.0f, 0.0f, 1.0f}, 1.0f);

    MouseLeftPress press;
    for (int i = 0; i < 3; ++i)
    {
        Step(scene, rig);
    }
    ASSERT_TRUE(rig.input->Judge().IsHeld());
    ASSERT_FALSE(rig.input->IsCharging());
    press.Release();
    Step(scene, rig);

    ASSERT_TRUE(rig.movement->IsBodySlamming());
    const Vector3 velocity = rig.movement->BodySlamVelocity();
    EXPECT_GT(velocity.z, 0.0f);
    EXPECT_NEAR(velocity.x, 0.0f, 1e-3f);
}

// 溜めている間の寄せは、倒した向きでなくカメラの正面から相手を探し、正面から相手への角度まで寄せる
TEST(CollisionImpact, ChargingHomingMeasuresFromTheCameraFrontNotTheStick)
{
    SceneNs::Scene scene;
    // 自機を 0.3 m 横へずらし、的を 5 m 先に置く。正面 +X から的への角度は約 3.4 度
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .lateral = 0.3f, .targetCell = 5});
    ASSERT_NE(rig.input, nullptr);
    // 上限の 3 度で頭打ちになると、どの向きから測っても同じ値になる
    SetFloatField(*rig.movement, "寄せる角度の上限", 20.0f);
    SettleOnFloor(scene, rig);
    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{1.0f, 0.0f, 0.0f}));
    // 倒した向きは +Z。倒し具合を 0 にして歩かせず、的への角度を変えない
    rig.movement->SetDesiredMove(Vector3{0.0f, 0.0f, 1.0f}, 0.0f);

    MouseLeftPress press;
    for (int i = 0; i < 30; ++i)
    {
        Step(scene, rig);
    }
    ASSERT_TRUE(rig.input->IsCharging());

    const Vector3 toTarget = rig.target->Root().Position() - rig.movement->Owner()->Root().Position();
    // 正の角度は +X の向きを -Z の側へ回す
    const float expected = NS::Core::ToDegrees(NS::Core::Radians{std::atan2(-toTarget.z, toTarget.x)}).value;
    ASSERT_GT(expected, 3.0f);
    EXPECT_NEAR(rig.movement->HomingAngleDegrees(), expected, 0.05f);
}

// 溜めている間の玉は、倒した向きでなく狙いの線の向き (カメラの正面) へ前転する。放せば出る向きへ回って見える
TEST(CollisionImpact, ChargingSpinsTheBallTowardTheAimLineNotTheStick)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_FarCourse);
    ASSERT_NE(rig.input, nullptr);
    NS::Game::Player::PlayerAppearance* appearance =
        rig.movement->Owner()->FindComponent<NS::Game::Player::PlayerAppearance>();
    ASSERT_NE(appearance, nullptr);
    SettleOnFloor(scene, rig);
    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{1.0f, 0.0f, 0.0f}));
    // 倒した向きは +Z。倒し具合を 0 にして歩かせない
    rig.movement->SetDesiredMove(Vector3{0.0f, 0.0f, 1.0f}, 0.0f);

    MouseLeftPress press;
    for (int i = 0; i < 30; ++i)
    {
        Step(scene, rig);
    }
    ASSERT_TRUE(rig.input->IsCharging());
    // 倒した向きが狙いの向きに効いていることを先に見る。効いていなければ軸が線に沿うのは当たり前になる
    ASSERT_GT(rig.movement->AimDirection().z, 0.9f);
    appearance->OnUpdate();

    // 線の向き (1, 0, 0) から (forward.z, 0, -forward.x) = (0, 0, -1)
    const Vector3 axis = appearance->SpinAxis();
    EXPECT_NEAR(axis.x, 0.0f, 1e-5f);
    EXPECT_NEAR(axis.y, 0.0f, 1e-5f);
    EXPECT_NEAR(axis.z, -1.0f, 1e-5f);
}

// 突進中も突進の向きの線で同じ決まり。線の外の近い的の側へ回らない
TEST(CollisionImpact, RushHomingPrefersTheTargetOnTheRushLine)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearerOffLineCourse);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);
    ASSERT_TRUE(rig.movement->IsBodySlamming());

    for (int i = 0; i < 3; ++i)
    {
        Step(scene, rig);
    }
    ASSERT_TRUE(rig.movement->IsBodySlamming());
    EXPECT_GT(rig.movement->HomingAngleDegrees(), 0.0f);
}

// 押した瞬間に玉になり、当たり・凍結・反動の間は玉のまま、着地して初めて立ち姿へ戻る
// 反動が明けたフレームは接地の印が残ったまま上向きの速度が入るので、そこで解けると宙で立ち姿に戻る
TEST(CollisionImpact, StaysCurledFromThePressUntilTheLandingAfterTheRebound)
{
    {
        SceneNs::Scene tapScene;
        Rig tap = BuildSlam(tapScene, k_NearCourse);
        ASSERT_NE(tap.input, nullptr);
        SettleOnFloor(tapScene, tap);
        tap.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
        MouseLeftPress press;
        Step(tapScene, tap);
        EXPECT_TRUE(tap.movement->IsCurled()) << "短く押しても押したフレームから玉になる";
        press.Release();
        Step(tapScene, tap);
        ASSERT_TRUE(tap.movement->IsBodySlamming());
        EXPECT_TRUE(tap.movement->IsCurled());
    }

    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.input, nullptr);
    SettleOnFloor(scene, rig);
    // 溜めて放した突進はカメラの正面へ出るので、カメラを的の +X へ向ける
    ASSERT_TRUE(NsTest::FaceSceneCamera(scene, Vector3{1.0f, 0.0f, 0.0f}));
    rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);

    MouseLeftPress press;
    Step(scene, rig);
    EXPECT_TRUE(rig.movement->IsCurled()) << "押したフレームから玉になる";
    for (int i = 0; i < 15; ++i)
    {
        Step(scene, rig);
    }
    ASSERT_TRUE(rig.input->IsCharging());
    press.Release();
    Step(scene, rig);
    ASSERT_TRUE(rig.movement->IsBodySlamming());

    // 当たって凍結に入るまで
    int steps = 0;
    while (rig.movement->IsActiveSelf() && steps < 30)
    {
        Step(scene, rig);
        EXPECT_TRUE(rig.movement->IsCurled()) << "放してから " << steps << " フレーム目";
        ++steps;
    }
    ASSERT_LT(steps, 30);

    // 凍結が明けるまで
    steps = 0;
    while (steps < 60)
    {
        StepWorld(scene);
        if (rig.movement->IsActiveSelf())
        {
            break;
        }
        rig.movement->OnUpdate();
        EXPECT_TRUE(rig.movement->IsCurled()) << "凍結の " << steps << " フレーム目";
        ++steps;
    }
    ASSERT_TRUE(rig.movement->IsActiveSelf());

    // 明けたフレーム。移動が動く前は接地の印が残り、上向きの反動が入っている
    ASSERT_TRUE(rig.movement->IsGrounded());
    ASSERT_FLOAT_EQ(rig.movement->VerticalVelocity(), rig.impact->LastImpact().selfVelocity.y);
    rig.movement->OnUpdate();
    EXPECT_TRUE(rig.movement->IsCurled()) << "反動が明けたフレームに解けている";

    bool sawAirborne = false;
    bool groundedBeforeUncurl = false;
    steps = 0;
    while (rig.movement->IsCurled() && steps < 180)
    {
        groundedBeforeUncurl = rig.movement->IsGrounded();
        if (!groundedBeforeUncurl)
        {
            sawAirborne = true;
        }
        Step(scene, rig);
        ++steps;
    }
    ASSERT_LT(steps, 180);
    EXPECT_TRUE(sawAirborne) << "反動で浮かないまま解けた";
    EXPECT_TRUE(groundedBeforeUncurl) << "着地する前に解けた";
}

// 宙で下りながら当てても、当てたフレームから止めが明けるまで玉のまま。当てたフレームは突進が終わって落下の
// 1 フレームを走るので、的の上面が手の高さの帯に入っていると、そのまま縁を掴めば立ち姿へ戻ってしまう
// 的は床から 1 m 浮かせ、上面 2.5 m。自機の根を 1.9 m に置き、当たるまでの数フレームの落下で根が
// 1.5〜2.0 m に居る。玉の手の高さは根 + 1.0 m なので、上面 2.5 m が掴める帯に入る
TEST(CollisionImpact, HitInTheAirAtLedgeHeightStaysCurledThroughTheHitStop)
{
    SceneNs::Scene scene;
    SlamCourse course = k_NearCourse;
    course.targetLayer = 2;
    Rig rig = BuildSlam(scene, course);
    rig.movement->Owner()->Root().SetPosition(Vector3{-0.5f, 1.9f, 0.0f});
    rig.movement->SetVelocity(Vector3{0.0f, 0.0f, 0.0f});
    rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
    rig.movement->RequestBodySlam(1.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 20), 20);
    ASSERT_TRUE(rig.impact->DidRebound());
    ASSERT_FALSE(rig.movement->IsGrounded()) << "宙で当たっていない";
    ASSERT_TRUE(rig.movement->IsCurled());

    rig.movement->OnUpdate();
    EXPECT_TRUE(rig.movement->IsCurled()) << "当てたフレームに解けた";

    int steps = 0;
    while (steps < 60)
    {
        StepWorld(scene);
        if (rig.movement->IsActiveSelf())
        {
            break;
        }
        rig.movement->OnUpdate();
        EXPECT_TRUE(rig.movement->IsCurled()) << "止めの " << steps << " フレーム目";
        ++steps;
    }
    ASSERT_TRUE(rig.movement->IsActiveSelf());
    EXPECT_TRUE(rig.movement->IsCurled()) << "止めが明けたフレームに立ち姿で居る";
}

// 押したまま出直しても、押している間は玉に戻る。出直しは丸まりを解くが、ボタンの判定は押しの途中のまま続く
// 出直しの Respawner::RestartRun は根を出現位置へ置いてから PlayerComponent の ResetState を呼ぶ
// ここも同じ順に呼ぶ
TEST(CollisionImpact, HeldThroughARestartCurlsAgain)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.input, nullptr);
    SettleOnFloor(scene, rig);
    const Vector3 standing = rig.movement->Owner()->Root().Position();

    MouseLeftPress press;
    Step(scene, rig);
    ASSERT_TRUE(rig.movement->IsCurled());

    rig.movement->Owner()->Root().SetPosition(standing);
    rig.movement->ResetState();
    ASSERT_FALSE(rig.movement->IsCurled());
    SettleOnFloor(scene, rig);
    Step(scene, rig);
    EXPECT_TRUE(rig.movement->IsCurled()) << "押したまま出直した後に立ち姿のまま";

    rig.movement->SetDesiredMove(Vector3{1.0f, 0.0f, 0.0f}, 0.0f);
    press.Release();
    Step(scene, rig);
    ASSERT_TRUE(rig.movement->IsBodySlamming());
    EXPECT_TRUE(rig.movement->IsCurled());
}

// 丸まった突進は玉の大きさで当たる。立ち姿のカプセルなら胴に掛かる高さに浮いた的でも、玉はその下をくぐる
// 自機は当たりの component を積まないので既定の半径 0.4・半長 0.5
// 玉の上端は床 0.5 + 直径 0.8 = 1.3 で、的の下面 1.5 に届かない
TEST(CollisionImpact, CurledRushPassesUnderATargetAtChestHeight)
{
    SceneNs::Scene scene;
    SlamCourse course = k_NearCourse;
    course.targetLayer = 2;
    Rig rig = BuildSlam(scene, course);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);
    ASSERT_TRUE(rig.movement->IsBodySlamming());
    ASSERT_TRUE(rig.movement->IsCurled());

    EXPECT_EQ(StepUntilImpact(scene, rig, 20), 20);
    EXPECT_FALSE(rig.impact->DidRebound());
    const float targetFarFaceX = rig.target->Root().Position().x + 0.5f;
    EXPECT_GT(rig.movement->Owner()->Root().Position().x, targetFarFaceX + rig.movement->CapsuleRadius());
}

// 壊した物へもう一度向かっても何も起きない。印が寝ているので探索から外れる
TEST(CollisionImpact, BrokenTargetIsIgnoredAfterwards)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    rig.breakable->SetToughness(1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);
    for (int i = 0; i < 10; ++i)
        Step(scene, rig);

    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);
    EXPECT_EQ(StepUntilImpact(scene, rig, 30), 30);
    EXPECT_FALSE(rig.impact->DidBreak());
    EXPECT_FALSE(rig.impact->DidRebound());
}

// 貫通は相手を飛ばさない。破片は別の仕組みが出す
TEST(CollisionImpact, BreakDoesNotLaunchTarget)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    rig.breakable->SetToughness(1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);

    const LevelNs::LaunchedBody* body = HitBody(rig);
    EXPECT_TRUE(body == nullptr || !body->IsFlying());
}

// 貫通の止め秒を 0 にすると凍結を挟まず、そのフレームのうちに壊れて減速する
TEST(CollisionImpact, BreakStopZeroAppliesInstantly)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    EnableBreak(rig);
    rig.breakable->SetToughness(1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidBreak());
    EXPECT_TRUE(rig.movement->IsActiveSelf());
    EXPECT_FALSE(rig.targetBox->IsActiveSelf());
    EXPECT_FLOAT_EQ(rig.movement->Velocity().x, k_TapSlamSpeed * 0.75f);
}

// 凍結の途中で裁定が外れても移動は止まったまま残らない
TEST(CollisionImpact, OnEndPlayWakesFrozenMovement)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    rig.impact->OnEndPlay();

    EXPECT_TRUE(rig.movement->IsActiveSelf());
}

// 貫通は潰れない。潰れは押し返されている反発だけの絵。貫通は前へ伸びる
TEST(CollisionImpact, BreakSkipsSquashButStretchesForward)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    rig.breakable->SetToughness(1.0f);
    const Vector3 authored = rig.movement->Owner()->Root().Scale();
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const Vector3 frozen = rig.movement->Owner()->Root().Scale();
    EXPECT_FLOAT_EQ(frozen.x, authored.x);
    EXPECT_FLOAT_EQ(frozen.y, authored.y);
    EXPECT_FLOAT_EQ(frozen.z, authored.z);

    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);
    const Vector3 stretched = rig.movement->Owner()->Root().Scale();
    EXPECT_GT(stretched.x, authored.x);
    EXPECT_FLOAT_EQ(stretched.y, authored.y);

    for (int i = 0; i < 10; ++i)
        Step(scene, rig);
    const Vector3 restored = rig.movement->Owner()->Root().Scale();
    EXPECT_FLOAT_EQ(restored.x, authored.x);
    EXPECT_FLOAT_EQ(restored.y, authored.y);
    EXPECT_FLOAT_EQ(restored.z, authored.z);
}

// 検知したフレームは移動を最後まで走らせ、次で凍る。自機が相手へ押し付けられた構図で止まる
TEST(CollisionImpact, FreezeWaitsOneStepAfterDetection)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.rigidBody->SetMass(4.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_TRUE(rig.movement->IsActiveSelf());

    Step(scene, rig);
    EXPECT_FALSE(rig.movement->IsActiveSelf());
}

// 待ちの 1 フレームと凍結中に同じ衝突を二重に検知しない
TEST(CollisionImpact, DetectsOnlyOncePerImpact)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.rigidBody->SetMass(4.0f);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    int detections = 0;
    for (int i = 0; i < 40; ++i)
    {
        Step(scene, rig);
        if (rig.impact->DidRebound())
            ++detections;
    }

    EXPECT_EQ(detections, 1);
    EXPECT_TRUE(rig.movement->IsActiveSelf());
    LevelNs::LaunchedBody* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());
}

// 凍結が始まるフレームで相手が発射方向へ食い込む。当たりは動かさない
TEST(CollisionImpact, HitStopPushesRockWhenFreezeBegins)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.rigidBody->SetMass(4.0f);
    const Vector3 home = rig.targetBox->Owner()->Root().Position();
    const JPH::uint bodies = scene.Physics().BodyCount();
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_FLOAT_EQ(rig.targetBox->Owner()->Root().Position().x, home.x);

    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const Vector3 pushed = rig.targetBox->Owner()->Root().Position();
    EXPECT_GT(pushed.x, home.x + 1.0e-4f);
    EXPECT_FLOAT_EQ(pushed.y, home.y);
    EXPECT_FLOAT_EQ(pushed.z, home.z);
    EXPECT_EQ(scene.Physics().BodyCount(), bodies);
    EXPECT_TRUE(rig.targetBox->IsActiveSelf());
}

// 凍結中はフレームごとに相手が発射軸に沿って往復する
TEST(CollisionImpact, RockVibratesWhileFrozen)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.rigidBody->SetMass(4.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    Step(scene, rig);
    const float x1 = rig.targetBox->Owner()->Root().Position().x;
    Step(scene, rig);
    const float x2 = rig.targetBox->Owner()->Root().Position().x;
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    EXPECT_GT(std::abs(x2 - x1), 1.0e-4f);
}

// 重い物は揺れない。振幅の差が質量の表現になる
TEST(CollisionImpact, HeavierRockVibratesLess)
{
    SceneNs::Scene lightScene;
    Rig light = BuildSlam(lightScene, k_NearCourse);
    light.rigidBody->SetMass(0.5f);
    BeginSlam(lightScene, light, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(lightScene, light, 30), 30);
    Step(lightScene, light);
    ASSERT_FALSE(light.movement->IsActiveSelf());
    float lightMin = light.targetBox->Owner()->Root().Position().x;
    float lightMax = lightMin;
    for (int i = 0; i < 30; ++i)
    {
        Step(lightScene, light);
        if (light.movement->IsActiveSelf())
            break;
        const float x = light.targetBox->Owner()->Root().Position().x;
        lightMin = std::min(lightMin, x);
        lightMax = std::max(lightMax, x);
    }

    SceneNs::Scene heavyScene;
    Rig heavy = BuildSlam(heavyScene, k_NearCourse);
    heavy.rigidBody->SetMass(8.0f);
    BeginSlam(heavyScene, heavy, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(heavyScene, heavy, 30), 30);
    Step(heavyScene, heavy);
    ASSERT_FALSE(heavy.movement->IsActiveSelf());
    float heavyMin = heavy.targetBox->Owner()->Root().Position().x;
    float heavyMax = heavyMin;
    for (int i = 0; i < 30; ++i)
    {
        Step(heavyScene, heavy);
        if (heavy.movement->IsActiveSelf())
            break;
        const float x = heavy.targetBox->Owner()->Root().Position().x;
        heavyMin = std::min(heavyMin, x);
        heavyMax = std::max(heavyMax, x);
    }

    EXPECT_GT(lightMax - lightMin, 1.0e-4f);
    EXPECT_LT(heavyMax - heavyMin, lightMax - lightMin);
}

// 食い込みも振動も絵だけ。明けたフレームに元位置へ厳密に戻してから発射する
TEST(CollisionImpact, ReleaseRestoresRockExactlyBeforeLaunch)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.rigidBody->SetMass(4.0f);
    const Vector3 home = rig.targetBox->Owner()->Root().Position();
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);

    const Vector3 restored = rig.targetBox->Owner()->Root().Position();
    EXPECT_FLOAT_EQ(restored.x, home.x);
    EXPECT_FLOAT_EQ(restored.y, home.y);
    EXPECT_FLOAT_EQ(restored.z, home.z);
    LevelNs::LaunchedBody* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());
    // 当たりは外れず、RigidBody の body として一緒に飛ぶ
    ASSERT_NE(rig.rigidBody, nullptr);
    EXPECT_TRUE(rig.targetBox->IsActiveSelf());
    EXPECT_EQ(rig.targetBox->BodyId(), rig.rigidBody->BodyId());
}

// 止めの頭に、置かれていた相手の描く形を自機の潰れと同じ倍率で突進の向きに縮める。根のスケールと当たりの球は変えない
// 突進は +X と +Z の 2 通り。厚みは進行の軸にだけ掛かる
TEST(CollisionImpact, FreezeShrinksThePlacedTargetDrawnShapeAlongTheRush)
{
    for (const bool alongZ : {false, true})
    {
        SCOPED_TRACE(alongZ);
        SceneNs::Scene scene;
        SlamCourse course = k_NearCourse;
        course.sphereTarget = true;
        course.alongZ = alongZ;
        Rig rig = BuildSlam(scene, course);
        ASSERT_NE(rig.target, nullptr);
        SceneNs::MeshRenderer* look = rig.target->FindComponent<SceneNs::MeshRenderer>();
        ASSERT_NE(look, nullptr);
        const SceneNs::SphereCollider* sphere = rig.target->FindComponent<SceneNs::SphereCollider>();
        ASSERT_NE(sphere, nullptr);
        const Vector3 rootScale = rig.target->Root().Scale();
        const float radius = sphere->WorldSphere().radius;
        BeginSlam(scene, rig, k_RunSpeed, 0.0f, alongZ);

        // 進行の軸の成分の 2 乗は進行の軸で 1、もう一方の水平の軸で 0 なので、厚み 0.7 は進行の軸だけに掛かり、
        // 縦は伸び上がり 1.1
        Vector3 rush{1.0f, 0.0f, 0.0f};
        Vector3 shrunk{0.7f, 1.1f, 1.0f};
        if (alongZ)
        {
            rush = Vector3{0.0f, 0.0f, 1.0f};
            shrunk = Vector3{1.0f, 1.1f, 0.7f};
        }

        ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
        ASSERT_TRUE(rig.impact->DidRebound());
        ASSERT_GT(rig.impact->LastImpact().hitStopSteps, 0);
        ASSERT_NEAR(rig.impact->LastImpact().impactDir.x, rush.x, 1e-6f);
        ASSERT_NEAR(rig.impact->LastImpact().impactDir.z, rush.z, 1e-6f);
        // 縮むのは止めの頭から。検知のフレームは元の形
        EXPECT_EQ(look->DrawScale(), Vector3(1.0f, 1.0f, 1.0f));

        Step(scene, rig);
        ASSERT_FALSE(rig.movement->IsActiveSelf());
        EXPECT_NEAR(look->DrawScale().x, shrunk.x, 1e-5f);
        EXPECT_NEAR(look->DrawScale().y, shrunk.y, 1e-5f);
        EXPECT_NEAR(look->DrawScale().z, shrunk.z, 1e-5f);
        EXPECT_EQ(rig.target->Root().Scale(), rootScale);
        EXPECT_FLOAT_EQ(sphere->WorldSphere().radius, radius);
    }
}

// 明けのフレームに相手の描く形を元へ戻してから飛ばす。前のフレームの値も揃え、縮んだ形から補間しない
TEST(CollisionImpact, ReleaseRestoresTheTargetDrawnShapeBeforeItFlies)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.target, nullptr);
    SceneNs::MeshRenderer* look = rig.target->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(look, nullptr);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    ASSERT_NE(look->DrawScale(), Vector3(1.0f, 1.0f, 1.0f));

    ASSERT_LT(StepsUntilMovementActive(scene, rig, 60), 60);

    EXPECT_EQ(look->DrawScale(), Vector3(1.0f, 1.0f, 1.0f));
    const NS::Core::Matrix drawn = look->DrawWorldMatrix(0.0f);
    const NS::Core::Matrix root = rig.target->Root().InterpolatedWorldMatrix(0.0f);
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            EXPECT_EQ(drawn.m[row][column], root.m[row][column]) << row << "," << column;
        }
    }
    LevelNs::LaunchedBody* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());
}

// 止めの途中で裁定が外れても、相手を縮んだ形のまま残さない
TEST(CollisionImpact, OnEndPlayRestoresTheShrunkTarget)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.target, nullptr);
    SceneNs::MeshRenderer* look = rig.target->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(look, nullptr);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    ASSERT_NE(look->DrawScale(), Vector3(1.0f, 1.0f, 1.0f));

    rig.impact->OnEndPlay();

    EXPECT_EQ(look->DrawScale(), Vector3(1.0f, 1.0f, 1.0f));
}

// 飛んでいる相手は止めの間も飛び続けるので縮めない。食い込みと振動と同じく、置かれていた相手だけの絵
TEST(CollisionImpact, FlyingTargetKeepsItsDrawnShapeThroughTheHitStop)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    ASSERT_NE(rig.target, nullptr);
    SceneNs::MeshRenderer* look = rig.target->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(look, nullptr);
    LevelNs::LaunchedBody* body = rig.target->AddComponent<LevelNs::LaunchedBody>();
    body->LaunchRigid(Vector3{0.0f, 0.0f, 0.0f});
    ASSERT_TRUE(body->IsFlying());

    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    ASSERT_GT(rig.impact->LastImpact().hitStopSteps, 0);
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    EXPECT_EQ(look->DrawScale(), Vector3(1.0f, 1.0f, 1.0f));
}

// 貫通は押し勝っている側なので、自機と同じく相手も縮めない
TEST(CollisionImpact, BreakLeavesTheTargetDrawnShapeAlone)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    rig.breakable->SetToughness(1.0f);
    ASSERT_NE(rig.target, nullptr);
    SceneNs::MeshRenderer* look = rig.target->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(look, nullptr);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidBreak());
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    EXPECT_EQ(look->DrawScale(), Vector3(1.0f, 1.0f, 1.0f));
}

// 描く形の無い相手でも、止めて明けて飛ばすまでが通る
TEST(CollisionImpact, TargetWithoutAMeshRendererStillStopsAndFlies)
{
    SceneNs::Scene scene;
    SlamCourse course = k_NearCourse;
    course.targetWithMeshRenderer = false;
    Rig rig = BuildSlam(scene, course);
    ASSERT_NE(rig.target, nullptr);
    ASSERT_EQ(rig.target->FindComponent<SceneNs::MeshRenderer>(), nullptr);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());

    ASSERT_LT(StepsUntilMovementActive(scene, rig, 60), 60);

    LevelNs::LaunchedBody* body = HitBody(rig);
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(body->IsFlying());
}

// 凍結中だけカメラが揺れる。ImpactResolver がシーンの CameraBrain へ揺れを渡す
TEST(CollisionImpact, HitStopShakesCamera)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    rig.rigidBody->SetMass(4.0f);

    SceneNs::CameraBrain* brain = scene.CameraBrain();
    ASSERT_NE(brain, nullptr);
    // 追う相手の無い追従カメラは固定の既定視点を返す。揺れの有無だけを見るのでそれで足りる
    SceneNs::ThirdPersonFollow* follow = brain->Owner()->AddComponent<SceneNs::ThirdPersonFollow>();
    follow->SetActive(true);
    brain->AddVirtualCamera(follow);
    brain->Evaluate(1.0f);
    const Vector3 before = brain->LastPose().position;

    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidRebound());
    brain->Evaluate(1.0f);
    EXPECT_FLOAT_EQ(brain->LastPose().position.y, before.y);

    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    brain->Evaluate(1.0f);
    const Vector3 during = brain->LastPose().position;
    EXPECT_GT(std::abs(during.y - before.y), 1.0e-4f);
}

namespace
{
    // 欄「カメラ揺れの強さ」「中心近くの当たりの揺れの倍率」の既定
    constexpr float k_CameraShakeScale = 0.06f;
    constexpr float k_CenterHitShakeScale = 1.25f;
    // 欄「大きな外れの揺れのフレーム数」「大きな外れの揺れの縦と横の比」の既定
    constexpr int k_WideShakeFrames = 16;
    constexpr float k_WideShakeUpOverSide = 0.35f;
    // 欄「中心近くの当たりの寄りの倍率」「中心近くの当たりの傾き」(度)「寄りと傾きを戻すフレーム数」の既定
    constexpr float k_CenterHitZoom = 1.15f;
    constexpr float k_CenterHitRollDegrees = 3.0f;
    constexpr int k_ZoomRollReturnFrames = 6;
    // 欄「惜しい当たりの返りの割合」「惜しい当たりの返りを引き始める割合」の既定
    constexpr float k_NearHitReturnRatio = 0.4f;
    constexpr float k_NearHitPullBackRatio = 0.5f;
    constexpr float k_ReturnTolerance = 1.0e-5f;
    // 欄「中心近くの当たりのパッドの振動の強さ」「大きな外れのパッドの振動の強さ」の既定
    constexpr float k_CenterHitPadStrength = 1.0f;
    constexpr float k_WidePadStrength = 0.6f;

    // 試しの前後でパッドの振動を 0 に戻す。この試しは Input::Update を回さないので、書いた振動は実機へ送られない
    struct PadVibrationReset
    {
        PadVibrationReset() { NS::Platform::Input::Get().Gamepad(0).StopVibration(); }
        ~PadVibrationReset() { NS::Platform::Input::Get().Gamepad(0).StopVibration(); }
        PadVibrationReset(const PadVibrationReset&) = delete;
        PadVibrationReset& operator=(const PadVibrationReset&) = delete;
    };

    NS::Platform::GamepadVibration PadVibration()
    {
        return NS::Platform::Input::Get().Gamepad(0).Vibration();
    }

    // 横ずれ 0.2 と 0.7 は、的の半幅 0.5 + 自機の半径 0.4 で割ると 0.22 と 0.78。既定の境目で中心近くと大きな外れ
    constexpr SlamCourse k_CenterTierCourse{.start = 0.0f, .lateral = 0.2f, .targetCell = 1};
    constexpr SlamCourse k_WideTierCourse{.start = 0.0f, .lateral = 0.7f, .targetCell = 1};

    // ゲームの帯と同じ順で、台の 1 フレームの後に CameraBrain の LateUpdate を回す
    void StepWithCamera(SceneNs::Scene& scene, const Rig& rig)
    {
        Step(scene, rig);
        scene.CameraBrain()->OnUpdate();
    }

    // 検知のフレームまで進める。検知のフレームも移動と CameraBrain を回し、次のフレームが止めの頭になる
    int StepUntilImpactWithCamera(SceneNs::Scene& scene, const Rig& rig, int maxSteps)
    {
        for (int i = 0; i < maxSteps; ++i)
        {
            StepWorld(scene);
            const bool detected = rig.impact->DidRebound() || rig.impact->DidBreak();
            rig.movement->OnUpdate();
            scene.CameraBrain()->OnUpdate();
            if (detected)
            {
                return i + 1;
            }
        }
        return maxSteps;
    }

    // カメラの水平の前から作った右と direction の内積が負なら -1、それ以外は 1
    float ScreenSideSign(const SceneNs::CameraBrain& brain, const Vector3& direction)
    {
        const Vector3 forward = brain.ForwardHorizontal();
        const Vector3 right{forward.z, 0.0f, -forward.x};
        if (NS::Core::Dot(right, direction) < 0.0f)
        {
            return -1.0f;
        }
        return 1.0f;
    }

    float FirstSwing(const Rig& rig, float power)
    {
        const float mass = rig.rigidBody->EffectiveMass();
        return k_CameraShakeScale * power * mass / (mass + 1.0f);
    }

    int SignOf(float value)
    {
        if (value > 0.0f)
        {
            return 1;
        }
        if (value < 0.0f)
        {
            return -1;
        }
        return 0;
    }

    // 大きな外れの当たりの止めの頭から、揺れのフレーム数ぶんの横と縦の向きの符号
    struct ShakeSigns
    {
        std::vector<int> side;
        std::vector<int> up;
    };

    ShakeSigns RecordWideShakeSigns(float lateral)
    {
        SceneNs::Scene scene;
        Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .lateral = lateral, .targetCell = 1});
        ShakeSigns signs;
        BeginSlam(scene, rig, k_RunSpeed, 1.0f);
        EXPECT_LT(StepUntilImpactWithCamera(scene, rig, 30), 30) << lateral;
        EXPECT_EQ(rig.impact->LastImpact().tier, LevelNs::HitTier::Wide) << lateral;
        for (int frame = 0; frame < k_WideShakeFrames; ++frame)
        {
            StepWithCamera(scene, rig);
            signs.side.push_back(SignOf(scene.CameraBrain()->ShakeOffset().x));
            signs.up.push_back(SignOf(scene.CameraBrain()->ShakeOffset().y));
        }
        return signs;
    }
} // namespace

// 中心近くは止めの頭から縦に 1.25 倍で揺れ、止めのフレーム数で収まる。寄りと傾きは止めの間保ち、明けから戻す
TEST(CollisionImpact, CenterHitShakesZoomsAndTiltsThroughTheFreeze)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_CenterTierCourse);
    SceneNs::CameraBrain* brain = scene.CameraBrain();
    ASSERT_NE(brain, nullptr);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpactWithCamera(scene, rig, 30), 30);
    const LevelNs::ImpactRecord hit = rig.impact->LastImpact();
    ASSERT_EQ(hit.tier, LevelNs::HitTier::Center);
    const int stop = hit.hitStopSteps;
    ASSERT_GE(stop, 2);
    // 検知のフレームには揺れも寄りも無い
    EXPECT_EQ(brain->ShakeOffset().x, 0.0f);
    EXPECT_EQ(brain->ShakeOffset().y, 0.0f);
    EXPECT_EQ(brain->ZoomRoll().zoom, 1.0f);
    EXPECT_EQ(brain->ZoomRoll().rollDegrees, 0.0f);

    const float swing = FirstSwing(rig, hit.power) * k_CenterHitShakeScale;
    const float roll = k_CenterHitRollDegrees * ScreenSideSign(*brain, hit.impactDir);
    for (int frame = 0; frame < stop; ++frame)
    {
        StepWithCamera(scene, rig);
        const NS::Core::Vector2 offset = brain->ShakeOffset();
        EXPECT_EQ(offset.x, 0.0f) << "frame " << frame;
        EXPECT_NEAR(
            std::abs(offset.y), swing * static_cast<float>(stop - frame) / static_cast<float>(stop), k_ReturnTolerance)
            << "frame " << frame;
        EXPECT_FLOAT_EQ(brain->ZoomRoll().zoom, k_CenterHitZoom) << "frame " << frame;
        EXPECT_FLOAT_EQ(brain->ZoomRoll().rollDegrees, roll) << "frame " << frame;
        if (frame == 0)
        {
            EXPECT_LT(offset.y, 0.0f);
            EXPECT_NEAR(offset.Length(), hit.cameraShake, k_ReturnTolerance);
            EXPECT_FLOAT_EQ(brain->ZoomRoll().zoom, hit.zoomStart);
            EXPECT_FLOAT_EQ(brain->ZoomRoll().rollDegrees, hit.rollStart);
            EXPECT_EQ(rig.impact->CenterHitFlashStepsRemaining(), hit.flashStart);
        }
    }

    // 明けから戻す。揺れは止めの間で終わっている
    float previousZoom = k_CenterHitZoom;
    for (int frame = 0; frame < k_ZoomRollReturnFrames; ++frame)
    {
        StepWithCamera(scene, rig);
        EXPECT_TRUE(rig.movement->IsActiveSelf()) << "frame " << frame;
        EXPECT_EQ(brain->ShakeOffset().Length(), 0.0f) << "frame " << frame;
        EXPECT_LT(brain->ZoomRoll().zoom, previousZoom) << "frame " << frame;
        previousZoom = brain->ZoomRoll().zoom;
    }
    EXPECT_EQ(brain->ZoomRoll().zoom, 1.0f);
    EXPECT_EQ(brain->ZoomRoll().rollDegrees, 0.0f);
}

// 惜しいは中心近くの寄りと傾きを割合で小さく出し、止めの途中で戻し始める。揺れは縦で倍率を掛けず、白は出ない
TEST(CollisionImpact, NearHitZoomsSmallerAndPullsBackBeforeTheRelease)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_EdgeCourse);
    SceneNs::CameraBrain* brain = scene.CameraBrain();
    ASSERT_NE(brain, nullptr);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpactWithCamera(scene, rig, 30), 30);
    const LevelNs::ImpactRecord hit = rig.impact->LastImpact();
    ASSERT_EQ(hit.tier, LevelNs::HitTier::Near);
    const int stop = hit.hitStopSteps;
    const int hold = static_cast<int>(std::ceil(static_cast<float>(stop) * k_NearHitPullBackRatio));
    ASSERT_LT(hold, stop);

    const float swing = FirstSwing(rig, hit.power);
    const float zoom = 1.0f + (k_CenterHitZoom - 1.0f) * k_NearHitReturnRatio;
    const float roll = k_CenterHitRollDegrees * k_NearHitReturnRatio * ScreenSideSign(*brain, hit.impactDir);
    int shakeFrames = 0;
    for (int frame = 0; frame < stop + 2; ++frame)
    {
        StepWithCamera(scene, rig);
        const NS::Core::Vector2 offset = brain->ShakeOffset();
        if (offset.Length() > 0.0f)
        {
            ++shakeFrames;
        }
        EXPECT_EQ(offset.x, 0.0f) << "frame " << frame;
        EXPECT_EQ(rig.impact->CenterHitFlashStepsRemaining(), 0) << "frame " << frame;
        if (frame == 0)
        {
            EXPECT_NEAR(offset.y, -swing, k_ReturnTolerance);
            EXPECT_NEAR(offset.Length(), hit.cameraShake, k_ReturnTolerance);
            EXPECT_FLOAT_EQ(brain->ZoomRoll().zoom, hit.zoomStart);
            EXPECT_FLOAT_EQ(brain->ZoomRoll().rollDegrees, hit.rollStart);
            EXPECT_EQ(hit.flashStart, 0);
        }
        if (frame < hold)
        {
            EXPECT_FLOAT_EQ(brain->ZoomRoll().zoom, zoom) << "frame " << frame;
            EXPECT_FLOAT_EQ(brain->ZoomRoll().rollDegrees, roll) << "frame " << frame;
        }
        if (frame == hold)
        {
            // 明けより前に戻し始める
            EXPECT_FALSE(rig.movement->IsActiveSelf());
            EXPECT_LT(brain->ZoomRoll().zoom, zoom);
            EXPECT_GT(brain->ZoomRoll().zoom, 1.0f);
        }
    }
    EXPECT_EQ(shakeFrames, stop);
}

// 大きな外れは横が主の揺れを止めから切り離した長さで続け、明けの後も揺れる。最初の横は自機の弾かれる側、縦は下。寄りと傾きは無い
TEST(CollisionImpact, WideHitShakeRunsSidewaysPastTheRelease)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_WideTierCourse);
    SceneNs::CameraBrain* brain = scene.CameraBrain();
    ASSERT_NE(brain, nullptr);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpactWithCamera(scene, rig, 30), 30);
    const LevelNs::ImpactRecord hit = rig.impact->LastImpact();
    ASSERT_EQ(hit.tier, LevelNs::HitTier::Wide);
    const int stop = hit.hitStopSteps;
    ASSERT_LT(stop, k_WideShakeFrames);
    EXPECT_EQ(brain->ShakeOffset().Length(), 0.0f);

    const float swing = FirstSwing(rig, hit.power);
    const float side = swing / std::sqrt(1.0f + k_WideShakeUpOverSide * k_WideShakeUpOverSide);
    Vector3 rebound{};
    ASSERT_TRUE(NS::Core::TryNormalizeHorizontal(hit.selfVelocity, rebound));
    const float firstSide = side * ScreenSideSign(*brain, rebound);

    int shakeFrames = 0;
    int shakeFramesAfterRelease = 0;
    for (int frame = 0; frame < k_WideShakeFrames + 2; ++frame)
    {
        StepWithCamera(scene, rig);
        const NS::Core::Vector2 offset = brain->ShakeOffset();
        if (offset.Length() > 0.0f)
        {
            ++shakeFrames;
            if (rig.movement->IsActiveSelf())
            {
                ++shakeFramesAfterRelease;
            }
        }
        EXPECT_EQ(brain->ZoomRoll().zoom, 1.0f) << "frame " << frame;
        EXPECT_EQ(brain->ZoomRoll().rollDegrees, 0.0f) << "frame " << frame;
        EXPECT_EQ(rig.impact->CenterHitFlashStepsRemaining(), 0) << "frame " << frame;
        if (frame == 0)
        {
            EXPECT_NEAR(offset.x, firstSide, k_ReturnTolerance);
            EXPECT_NEAR(offset.y, -side * k_WideShakeUpOverSide, k_ReturnTolerance);
            EXPECT_NEAR(offset.Length(), swing, k_ReturnTolerance);
            EXPECT_NEAR(offset.Length(), hit.cameraShake, k_ReturnTolerance);
        }
    }
    EXPECT_EQ(shakeFrames, k_WideShakeFrames);
    EXPECT_EQ(shakeFramesAfterRelease, k_WideShakeFrames - stop);
    EXPECT_EQ(hit.flashStart, 0);
    EXPECT_EQ(hit.zoomStart, 1.0f);
    EXPECT_EQ(hit.rollStart, 0.0f);
}

// 揺れの並びは当たりの中身から決まる。作り直した台の同じ当たりは同じ並び、横ずれだけ違う当たりは違う並び
TEST(CollisionImpact, ShakeSeedComesFromTheHitItself)
{
    const ShakeSigns first = RecordWideShakeSigns(0.7f);
    const ShakeSigns again = RecordWideShakeSigns(0.7f);
    const ShakeSigns shifted = RecordWideShakeSigns(0.75f);

    EXPECT_EQ(first.side, again.side);
    EXPECT_EQ(first.up, again.up);
    EXPECT_TRUE(first.side != shifted.side || first.up != shifted.up);
}

// CollisionInput の無い台は段を見ない。縦の揺れを倍率なしで出し、白と寄りと傾きと振動は出さない
TEST(CollisionImpact, HitWithoutCollisionInputShakesUpOnly)
{
    const PadVibrationReset reset;
    SceneNs::Scene scene;
    SlamCourse course = k_NearCourse;
    course.withCollisionInput = false;
    Rig rig = BuildSlam(scene, course);
    SceneNs::CameraBrain* brain = scene.CameraBrain();
    ASSERT_NE(brain, nullptr);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);

    ASSERT_LT(StepUntilImpactWithCamera(scene, rig, 30), 30);
    const LevelNs::ImpactRecord hit = rig.impact->LastImpact();
    ASSERT_FALSE(hit.centerHit);
    StepWithCamera(scene, rig);

    EXPECT_EQ(brain->ShakeOffset().x, 0.0f);
    EXPECT_NEAR(brain->ShakeOffset().y, -FirstSwing(rig, hit.power), k_ReturnTolerance);
    EXPECT_EQ(brain->ZoomRoll().zoom, 1.0f);
    EXPECT_EQ(brain->ZoomRoll().rollDegrees, 0.0f);
    EXPECT_EQ(rig.impact->CenterHitFlashStepsRemaining(), 0);
    EXPECT_EQ(PadVibration().left, 0.0f);
    EXPECT_EQ(PadVibration().right, 0.0f);
    EXPECT_EQ(hit.flashStart, 0);
    EXPECT_EQ(hit.zoomStart, 1.0f);
    EXPECT_EQ(hit.rollStart, 0.0f);
    EXPECT_EQ(hit.padStart.left, 0.0f);
    EXPECT_EQ(hit.padStart.right, 0.0f);
}

// 1 回目の大きな外れの揺れと振動が残っている所へ 2 回目を当てると、2 回目の止めの頭で 2 回目の返りから始め直す
TEST(CollisionImpact, SecondHitRestartsTheReturnsOverTheFirstShake)
{
    const PadVibrationReset reset;
    SceneNs::Scene scene;
    // 反動で下がる側 (-X) にもう 1 体置き、明けに空中の 1 発で当てる
    Rig rig = BuildSlam(scene, SlamCourse{.start = 0.0f, .lateral = 0.7f, .targetCell = 1, .extraTargetCell = -2});
    SceneNs::CameraBrain* brain = scene.CameraBrain();
    ASSERT_NE(brain, nullptr);
    NS::Obj::GameObject* extra = nullptr;
    scene.Objects().ForEachComponent<LevelNs::Breakable>([&](LevelNs::Breakable& breakable) {
        if (breakable.Owner() != rig.target)
        {
            extra = breakable.Owner();
        }
    });
    ASSERT_NE(extra, nullptr);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpactWithCamera(scene, rig, 30), 30);
    ASSERT_EQ(rig.impact->LastImpact().tier, LevelNs::HitTier::Wide);
    int released = 0;
    while (released < 60)
    {
        StepWorld(scene);
        ++released;
        if (rig.movement->IsActiveSelf())
        {
            break;
        }
        rig.movement->OnUpdate();
        brain->OnUpdate();
    }
    ASSERT_LT(released, 60);
    // 明けのフレームに 2 体目へ向けて空中の 1 発を出す
    const Vector3 toExtra = extra->Root().Position() - rig.movement->Owner()->Root().Position();
    Vector3 aim{};
    ASSERT_TRUE(NS::Core::TryNormalizeHorizontal(toExtra, aim));
    rig.movement->SetDesiredMove(aim, 0.0f);
    rig.movement->RequestBodySlam(1.0f);
    rig.movement->OnUpdate();
    brain->OnUpdate();
    ASSERT_TRUE(rig.movement->IsBodySlamming());

    ASSERT_LT(StepUntilImpactWithCamera(scene, rig, 10), 10);
    const LevelNs::ImpactRecord second = rig.impact->LastImpact();
    ASSERT_EQ(second.sequence, 2u);
    ASSERT_EQ(second.tier, LevelNs::HitTier::Center);
    // 2 回目の検知のフレームに 1 回目の揺れと軽いモーターが残っている。残っていなければ重なった場面になっていない
    ASSERT_GT(brain->ShakeOffset().Length(), 0.0f);
    ASSERT_GT(PadVibration().right, 0.0f);

    StepWithCamera(scene, rig);
    EXPECT_NEAR(brain->ShakeOffset().Length(), second.cameraShake, k_ReturnTolerance);
    EXPECT_EQ(brain->ShakeOffset().x, 0.0f);
    EXPECT_LT(brain->ShakeOffset().y, 0.0f);
    EXPECT_FLOAT_EQ(brain->ZoomRoll().zoom, second.zoomStart);
    EXPECT_FLOAT_EQ(brain->ZoomRoll().rollDegrees, second.rollStart);
    EXPECT_EQ(rig.impact->CenterHitFlashStepsRemaining(), second.flashStart);
    EXPECT_FLOAT_EQ(PadVibration().left, second.padStart.left);
    EXPECT_FLOAT_EQ(PadVibration().right, second.padStart.right);
    EXPECT_GT(second.padStart.left, 0.0f);
}

// プレイを終えると、揺れと寄りと傾きと白と振動が残らない
TEST(CollisionImpact, OnEndPlayStopsTheShakeAndTheZoom)
{
    const PadVibrationReset reset;
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_CenterTierCourse);
    SceneNs::CameraBrain* brain = scene.CameraBrain();
    ASSERT_NE(brain, nullptr);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);
    ASSERT_LT(StepUntilImpactWithCamera(scene, rig, 30), 30);
    StepWithCamera(scene, rig);
    ASSERT_GT(brain->ShakeOffset().Length(), 0.0f);
    ASSERT_GT(brain->ZoomRoll().zoom, 1.0f);
    ASSERT_GT(rig.impact->CenterHitFlashStepsRemaining(), 0);
    ASSERT_GT(PadVibration().left, 0.0f);

    rig.impact->OnEndPlay();

    EXPECT_EQ(brain->ShakeOffset().Length(), 0.0f);
    EXPECT_EQ(brain->ZoomRoll().zoom, 1.0f);
    EXPECT_EQ(brain->ZoomRoll().rollDegrees, 0.0f);
    EXPECT_EQ(rig.impact->CenterHitFlashStepsRemaining(), 0);
    EXPECT_EQ(PadVibration().left, 0.0f);
    EXPECT_EQ(PadVibration().right, 0.0f);
}

// 中心近くは止めの頭から重いモーターを最大で始め、止めの間に直線に減らして明けで 0 にする
TEST(CollisionImpact, CenterHitVibratesTheHeavyMotorThroughTheFreeze)
{
    const PadVibrationReset reset;
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_CenterTierCourse);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpactWithCamera(scene, rig, 30), 30);
    const LevelNs::ImpactRecord hit = rig.impact->LastImpact();
    ASSERT_EQ(hit.tier, LevelNs::HitTier::Center);
    const int stop = hit.hitStopSteps;
    ASSERT_GE(stop, 2);
    EXPECT_FLOAT_EQ(hit.padStart.left, k_CenterHitPadStrength);
    EXPECT_EQ(hit.padStart.right, 0.0f);

    for (int frame = 0; frame < stop; ++frame)
    {
        Step(scene, rig);
        ASSERT_FALSE(rig.movement->IsActiveSelf()) << "frame " << frame;
        const float fade = static_cast<float>(stop - frame) / static_cast<float>(stop);
        EXPECT_NEAR(PadVibration().left, k_CenterHitPadStrength * fade, k_ReturnTolerance) << "frame " << frame;
        EXPECT_EQ(PadVibration().right, 0.0f) << "frame " << frame;
    }

    Step(scene, rig);
    ASSERT_TRUE(rig.movement->IsActiveSelf());
    EXPECT_EQ(PadVibration().left, 0.0f);
    EXPECT_EQ(PadVibration().right, 0.0f);
}

// 惜しいは中心近くの重いモーターを割合で小さく出し、同じ傾きで減らして止めの途中で 0 にする
TEST(CollisionImpact, NearHitVibratesSmallerAndStopsBeforeTheRelease)
{
    const PadVibrationReset reset;
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_EdgeCourse);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpactWithCamera(scene, rig, 30), 30);
    const LevelNs::ImpactRecord hit = rig.impact->LastImpact();
    ASSERT_EQ(hit.tier, LevelNs::HitTier::Near);
    const int stop = hit.hitStopSteps;
    const int hold = static_cast<int>(std::ceil(static_cast<float>(stop) * k_NearHitPullBackRatio));
    ASSERT_LT(hold, stop);
    const float strength = k_CenterHitPadStrength * k_NearHitReturnRatio;
    EXPECT_FLOAT_EQ(hit.padStart.left, strength);
    EXPECT_EQ(hit.padStart.right, 0.0f);

    for (int frame = 0; frame < hold; ++frame)
    {
        Step(scene, rig);
        const float fade = static_cast<float>(stop - frame) / static_cast<float>(stop);
        EXPECT_NEAR(PadVibration().left, strength * fade, k_ReturnTolerance) << "frame " << frame;
        EXPECT_EQ(PadVibration().right, 0.0f) << "frame " << frame;
    }

    Step(scene, rig);
    // 明けより前に 0 になる
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    EXPECT_EQ(PadVibration().left, 0.0f);
    EXPECT_EQ(PadVibration().right, 0.0f);
}

// 大きな外れは軽いモーターを揺れと同じフレーム数で減らし、明けの後も震わせる。重いモーターは使わない
TEST(CollisionImpact, WideHitVibratesTheLightMotorPastTheRelease)
{
    const PadVibrationReset reset;
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_WideTierCourse);
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpactWithCamera(scene, rig, 30), 30);
    const LevelNs::ImpactRecord hit = rig.impact->LastImpact();
    ASSERT_EQ(hit.tier, LevelNs::HitTier::Wide);
    const int stop = hit.hitStopSteps;
    ASSERT_LT(stop, k_WideShakeFrames);
    EXPECT_EQ(hit.padStart.left, 0.0f);
    EXPECT_FLOAT_EQ(hit.padStart.right, k_WidePadStrength);

    int framesAfterRelease = 0;
    for (int frame = 0; frame < k_WideShakeFrames; ++frame)
    {
        Step(scene, rig);
        const float fade = static_cast<float>(k_WideShakeFrames - frame) / static_cast<float>(k_WideShakeFrames);
        EXPECT_EQ(PadVibration().left, 0.0f) << "frame " << frame;
        EXPECT_NEAR(PadVibration().right, k_WidePadStrength * fade, k_ReturnTolerance) << "frame " << frame;
        if (rig.movement->IsActiveSelf())
        {
            ++framesAfterRelease;
        }
    }
    EXPECT_EQ(framesAfterRelease, k_WideShakeFrames - stop);

    Step(scene, rig);
    EXPECT_EQ(PadVibration().left, 0.0f);
    EXPECT_EQ(PadVibration().right, 0.0f);
}

// 中心近くの当たりの返りは、検知のフレームに 1 つも無く、止めの頭に全部ある
TEST(CollisionImpact, EveryReturnStartsOnTheFreezeFrame)
{
    const PadVibrationReset reset;
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_CenterTierCourse);
    SceneNs::CameraBrain* brain = scene.CameraBrain();
    ASSERT_NE(brain, nullptr);
    ASSERT_NE(rig.target, nullptr);
    const SceneNs::MeshRenderer* look = rig.target->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(look, nullptr);
    const Vector3 authored = rig.movement->Owner()->Root().Scale();
    const Vector3 unshrunk{1.0f, 1.0f, 1.0f};
    BeginSlam(scene, rig, k_RunSpeed, 1.0f);

    ASSERT_LT(StepUntilImpactWithCamera(scene, rig, 30), 30);
    const LevelNs::ImpactRecord hit = rig.impact->LastImpact();
    ASSERT_EQ(hit.tier, LevelNs::HitTier::Center);
    ASSERT_GT(hit.hitStopSteps, 0);
    ASSERT_FALSE(hit.broke);

    EXPECT_TRUE(rig.movement->IsActiveSelf());
    EXPECT_EQ(rig.movement->Owner()->Root().Scale(), authored);
    EXPECT_EQ(rig.target->Root().Position(), hit.targetPos);
    EXPECT_EQ(look->DrawScale(), unshrunk);
    EXPECT_EQ(rig.impact->CenterHitFlashStepsRemaining(), 0);
    EXPECT_EQ(brain->ShakeOffset().Length(), 0.0f);
    EXPECT_EQ(brain->ZoomRoll().zoom, 1.0f);
    EXPECT_EQ(brain->ZoomRoll().rollDegrees, 0.0f);
    EXPECT_EQ(PadVibration().left, 0.0f);
    EXPECT_EQ(PadVibration().right, 0.0f);

    StepWithCamera(scene, rig);
    EXPECT_FALSE(rig.movement->IsActiveSelf());
    EXPECT_NE(rig.movement->Owner()->Root().Scale(), authored);
    EXPECT_NE(rig.target->Root().Position(), hit.targetPos);
    EXPECT_NE(look->DrawScale(), unshrunk);
    EXPECT_GT(rig.impact->CenterHitFlashStepsRemaining(), 0);
    EXPECT_EQ(rig.impact->CenterHitFlashStepsRemaining(), hit.flashStart);
    EXPECT_GT(brain->ShakeOffset().Length(), 0.0f);
    EXPECT_NEAR(brain->ShakeOffset().Length(), hit.cameraShake, k_ReturnTolerance);
    EXPECT_GT(brain->ZoomRoll().zoom, 1.0f);
    EXPECT_FLOAT_EQ(brain->ZoomRoll().zoom, hit.zoomStart);
    EXPECT_NE(brain->ZoomRoll().rollDegrees, 0.0f);
    EXPECT_FLOAT_EQ(brain->ZoomRoll().rollDegrees, hit.rollStart);
    EXPECT_GT(PadVibration().left, 0.0f);
    EXPECT_FLOAT_EQ(PadVibration().left, hit.padStart.left);
    EXPECT_FLOAT_EQ(PadVibration().right, hit.padStart.right);
}

// 壊した瞬間に破片と跡が出る。破片は壊れた物の位置から飛び始める
TEST(CollisionImpact, BreakScattersDebrisAndLeavesMark)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.breakable->SetToughness(1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);
    const std::size_t before = scene.Objects().ObjectCount();

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidBreak());
    EXPECT_EQ(scene.Objects().ObjectCount(), before + 6);
    EXPECT_EQ(MarkCount(scene), 1);
    const std::vector<LevelNs::LaunchedBody*> debris = DebrisBodies(scene);
    ASSERT_EQ(debris.size(), 5u);
    const Vector3 home = rig.targetBox->Owner()->Root().Position();
    for (LevelNs::LaunchedBody* body : debris)
    {
        EXPECT_TRUE(body->IsFlying());
        const Vector3 pos = body->Owner()->Root().Position();
        EXPECT_FLOAT_EQ(pos.x, home.x);
        EXPECT_FLOAT_EQ(pos.y, home.y);
        EXPECT_FLOAT_EQ(pos.z, home.z);
    }
}

// 壊した物の本体は見えなくなる
TEST(CollisionImpact, BreakHidesTarget)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.breakable->SetToughness(1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidBreak());
    ASSERT_NE(rig.target, nullptr);
    SceneNs::MeshRenderer* mesh = rig.target->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_FALSE(mesh->IsActiveSelf());
}

// 壊した時の破片は破片の数が 0。0 でないと壁に当たるたびに撒き直す
TEST(CollisionImpact, BreakDebrisDoNotScatterAgain)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.breakable->SetToughness(1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const std::vector<LevelNs::LaunchedBody*> debris = DebrisBodies(scene);
    ASSERT_EQ(debris.size(), 5u);
    for (LevelNs::LaunchedBody* body : debris)
    {
        const SceneNs::Component& comp = *body;
        const SceneNs::FieldDesc* field = SceneNs::FindField(comp.GetReflection(), "破片の数");
        ASSERT_NE(field, nullptr);
        int count = -1;
        field->get(&comp, &count);
        EXPECT_EQ(count, 0);
    }
}

// 破片は同じ速さで別の向きへ散る。1 方向に固まると壊れた量が見えない
TEST(CollisionImpact, DebrisScatterDirectionsDifferButShareSpeed)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.breakable->SetToughness(1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const std::vector<LevelNs::LaunchedBody*> debris = DebrisBodies(scene);
    ASSERT_EQ(debris.size(), 5u);
    for (LevelNs::LaunchedBody* body : debris)
        EXPECT_NEAR(HorizontalSpeed(body->Velocity()), 6.0f, 0.001f);
    const Vector3 first = debris[0]->Velocity();
    const Vector3 second = debris[1]->Velocity();
    EXPECT_GT(std::abs(first.x - second.x) + std::abs(first.z - second.z), 0.1f);
}

// 散り方は決定論。同じ状況で 2 回壊すと同じ向きへ散る
TEST(CollisionImpact, DebrisScatterIsDeterministic)
{
    SceneNs::Scene firstScene;
    Rig first = BuildSlam(firstScene, k_NearCourse);
    EnableBreak(first);
    SetFloatField(*first.impact, "貫通の止め秒", 0.0f);
    first.breakable->SetToughness(1.0f);
    BeginSlam(firstScene, first, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(firstScene, first, 30), 30);

    SceneNs::Scene secondScene;
    Rig second = BuildSlam(secondScene, k_NearCourse);
    EnableBreak(second);
    SetFloatField(*second.impact, "貫通の止め秒", 0.0f);
    second.breakable->SetToughness(1.0f);
    BeginSlam(secondScene, second, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(secondScene, second, 30), 30);

    const std::vector<LevelNs::LaunchedBody*> firstDebris = DebrisBodies(firstScene);
    const std::vector<LevelNs::LaunchedBody*> secondDebris = DebrisBodies(secondScene);
    ASSERT_EQ(firstDebris.size(), secondDebris.size());
    ASSERT_EQ(firstDebris.size(), 5u);
    for (std::size_t i = 0; i < firstDebris.size(); ++i)
    {
        const Vector3 a = firstDebris[i]->Velocity();
        const Vector3 b = secondDebris[i]->Velocity();
        EXPECT_FLOAT_EQ(a.x, b.x);
        EXPECT_FLOAT_EQ(a.y, b.y);
        EXPECT_FLOAT_EQ(a.z, b.z);
    }
}

// 重い物の破片は飛ばない。破片の飛び方も質量の表示にする
TEST(CollisionImpact, HeavierTargetScattersSlowerDebris)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    rig.rigidBody->SetMass(4.0f);
    rig.breakable->SetToughness(1.0f);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidBreak());
    const std::vector<LevelNs::LaunchedBody*> debris = DebrisBodies(scene);
    ASSERT_EQ(debris.size(), 5u);
    for (LevelNs::LaunchedBody* body : debris)
        EXPECT_NEAR(HorizontalSpeed(body->Velocity()), 1.5f, 0.001f);
}

// 押し飛ばしは跡だけ出す。破片は貫通の絵
TEST(CollisionImpact, LaunchLeavesMarkWithoutDebris)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    const std::size_t before = scene.Objects().ObjectCount();

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_EQ(scene.Objects().ObjectCount(), before + 1);
    EXPECT_EQ(MarkCount(scene), 1);
    EXPECT_TRUE(DebrisBodies(scene).empty());
}

// 壊れた物の破片の数が 0 なら破片を出さない
TEST(CollisionImpact, ZeroDebrisCountScattersNone)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    ASSERT_NE(rig.target, nullptr);
    rig.target->AddComponent<LevelNs::LaunchedBody>()->SetDebrisCount(0);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.breakable->SetToughness(1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);
    const std::size_t before = scene.Objects().ObjectCount();

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidBreak());
    EXPECT_EQ(scene.Objects().ObjectCount(), before + 1);
    EXPECT_TRUE(DebrisBodies(scene).empty());
    EXPECT_EQ(MarkCount(scene), 1);
}

// 真下に床が無ければ跡を出さない
TEST(CollisionImpact, NoMarkWithoutFloorBelow)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, SlamCourse{.targetCell = 2, .floorUnderTarget = false});
    SetInstantImpact(rig);
    BeginSlam(scene, rig, k_RunSpeed, 0.0f);
    const std::size_t before = scene.Objects().ObjectCount();

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    ASSERT_TRUE(rig.impact->DidRebound());
    EXPECT_EQ(scene.Objects().ObjectCount(), before);
    EXPECT_EQ(MarkCount(scene), 0);
}

TEST(CollisionImpact, DebrisLooksLikeSmallCube)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    rig.breakable->SetToughness(1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const std::vector<LevelNs::LaunchedBody*> debris = DebrisBodies(scene);
    ASSERT_EQ(debris.size(), 5u);
    SceneNs::MeshRenderer* mesh = debris[0]->Owner()->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->MeshRef(), "cube");
    const Vector3 scale = debris[0]->Owner()->Root().Scale();
    const Vector3 targetScale = rig.targetBox->Owner()->Root().Scale();
    EXPECT_LT(scale.x, targetScale.x);
    EXPECT_FLOAT_EQ(scale.x, 0.25f);
}

// 破片は転がって止まり、しばらくして描画ごと消える。配置物は破棄しない
TEST(CollisionImpact, DebrisRestsThenExpires)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    SetFloatField(*rig.impact, "貫通の止め秒", 0.0f);
    ASSERT_NE(rig.target, nullptr);
    LevelNs::LaunchedBody* targetBody = rig.target->AddComponent<LevelNs::LaunchedBody>();
    SetFloatField(*targetBody, "破片の速さ", 1.0f);
    SetFloatField(*targetBody, "破片の寿命秒", 0.05f);
    rig.breakable->SetToughness(1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);
    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);

    const std::vector<LevelNs::LaunchedBody*> debris = DebrisBodies(scene);
    ASSERT_EQ(debris.size(), 5u);
    int guard = 0;
    while ([&debris]() {
        for (LevelNs::LaunchedBody* body : debris)
        {
            if (body->IsFlying())
                return true;
        }
        return false;
    }() && guard < 300)
    {
        StepBody(scene);
        ++guard;
    }
    ASSERT_LT(guard, 300);

    for (int i = 0; i < 5; ++i)
        StepBody(scene);
    for (LevelNs::LaunchedBody* body : debris)
    {
        SceneNs::MeshRenderer* mesh = body->Owner()->FindComponent<SceneNs::MeshRenderer>();
        ASSERT_NE(mesh, nullptr);
        EXPECT_FALSE(mesh->IsActiveSelf());
        EXPECT_FALSE(body->IsActiveSelf());
    }
}

// 破片と跡は解放のフレームに出る。止まった 1 枚の横で破片だけが飛ばない
TEST(CollisionImpact, DebrisWaitForRelease)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    EnableBreak(rig);
    rig.breakable->SetToughness(1.0f);
    BeginSlam(scene, rig, k_FastEntrySpeed, 0.0f);
    const std::size_t before = scene.Objects().ObjectCount();

    ASSERT_LT(StepUntilImpact(scene, rig, 30), 30);
    ASSERT_TRUE(rig.impact->DidBreak());
    EXPECT_EQ(scene.Objects().ObjectCount(), before);

    Step(scene, rig);
    ASSERT_FALSE(rig.movement->IsActiveSelf());
    EXPECT_EQ(scene.Objects().ObjectCount(), before);

    const int rest = StepsUntilMovementActive(scene, rig, 60);
    ASSERT_LT(rest, 60);
    EXPECT_EQ(scene.Objects().ObjectCount(), before + 6);
}

TEST(LaunchedBody, LaunchMakesRigidBodyDynamicAndKeepsCollider)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    ASSERT_NE(rig.box, nullptr);
    ASSERT_NE(rig.rigidBody, nullptr);
    ASSERT_TRUE(rig.box->IsActiveSelf());
    ASSERT_TRUE(rig.rigidBody->IsKinematic());

    rig.body->Launch(k_TestArc);

    EXPECT_EQ(rig.body->Phase(), LevelNs::LaunchPhase::Arc);
    EXPECT_FALSE(rig.rigidBody->IsKinematic());
    // collider は RigidBody の形のまま残る。飛ぶのは RigidBody の body で、別の body は立たない
    EXPECT_TRUE(rig.box->IsActiveSelf());
    EXPECT_FALSE(rig.rigidBody->BodyId().IsInvalid());
    EXPECT_EQ(rig.box->BodyId(), rig.rigidBody->BodyId());
    EXPECT_EQ(scene.Physics().BodyCount(), rig.restingBodies);
}

TEST(LaunchedBody, LaunchRigidMakesRigidBodyDynamicAndKeepsCollider)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    ASSERT_NE(rig.box, nullptr);
    ASSERT_NE(rig.rigidBody, nullptr);
    ASSERT_TRUE(rig.box->IsActiveSelf());
    ASSERT_TRUE(rig.rigidBody->IsKinematic());

    rig.body->LaunchRigid(Vector3{10.0f, 4.0f, 0.0f});

    EXPECT_EQ(rig.body->Phase(), LevelNs::LaunchPhase::Rigid);
    EXPECT_FALSE(rig.rigidBody->IsKinematic());
    // collider は RigidBody の形のまま残る。飛ぶのは RigidBody の body で、別の body は立たない
    EXPECT_TRUE(rig.box->IsActiveSelf());
    EXPECT_FALSE(rig.rigidBody->BodyId().IsInvalid());
    EXPECT_EQ(rig.box->BodyId(), rig.rigidBody->BodyId());
    EXPECT_EQ(scene.Physics().BodyCount(), rig.restingBodies);
}

// 頂点で欄の高さまで上がり、発射の高さへ戻った所で欄の距離だけ進んでいる
TEST(LaunchedBody, ArcReachesItsApexHeightAndLandsAtItsDistance)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX, .sphereTarget = true});
    ASSERT_NE(rig.body, nullptr);

    const ArcFlight flight = FlyArc(scene, rig, k_TestArc, 120);

    ASSERT_GT(flight.handFrame, 0);
    float top = 0.0f;
    for (const Vector3& position : flight.positions)
    {
        top = std::max(top, position.y - flight.start.y);
    }
    EXPECT_NEAR(top, k_TestArc.apexHeight, 0.05f);
    // この速さでは 1 フレームの移動が掃引の下限 (内接球の半径の 0.75 倍) に届かない
    // 着地の接触は床へ沈んだ次のフレームに出るので、1 フレームぶんの水平の移動を足して許す
    const float oneFrame = LevelNs::LaunchArcInitialVelocity(k_TestArc).x * k_FixedDt;
    EXPECT_NEAR(flight.positions.back().x - flight.start.x, k_TestArc.distance, 0.3f + oneFrame);
}

// 下りは上りより短い
TEST(LaunchedBody, ArcFallsInFewerFramesThanItRises)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX, .sphereTarget = true});
    ASSERT_NE(rig.body, nullptr);

    const ArcFlight flight = FlyArc(scene, rig, k_TestArc, 120);

    ASSERT_GT(flight.handFrame, 0);
    int rising = 0;
    for (const Vector3& velocity : flight.velocities)
    {
        if (velocity.y > 0.0f)
        {
            ++rising;
        }
    }
    const int falling = flight.handFrame - rising;
    EXPECT_GT(rising, falling);
}

// 上りの重力を弱めると、同じ高さへ遅い初速で上がり、上りに長く掛かる
TEST(LaunchedBody, WeakerRiseGravityRisesMoreSlowlyToTheSameHeight)
{
    LevelNs::LaunchArc weak = k_TestArc;
    weak.riseGravity = 12.5f;
    const float defaultRise = LevelNs::LaunchArcInitialVelocity(k_TestArc).y;
    const float weakRise = LevelNs::LaunchArcInitialVelocity(weak).y;
    ASSERT_GT(weakRise, 0.0f);
    EXPECT_LT(weakRise, defaultRise);

    SceneNs::Scene defaultScene;
    BodyRig defaultRig = BuildBody(defaultScene, {.floorLastX = k_LongFloorLastX, .sphereTarget = true});
    SceneNs::Scene weakScene;
    BodyRig weakRig = BuildBody(weakScene, {.floorLastX = k_LongFloorLastX, .sphereTarget = true});
    ASSERT_NE(defaultRig.body, nullptr);
    ASSERT_NE(weakRig.body, nullptr);
    const ArcFlight defaultFlight = FlyArc(defaultScene, defaultRig, k_TestArc, 240);
    const ArcFlight weakFlight = FlyArc(weakScene, weakRig, weak, 240);

    int defaultRising = 0;
    for (const Vector3& velocity : defaultFlight.velocities)
    {
        if (velocity.y > 0.0f)
        {
            ++defaultRising;
        }
    }
    int weakRising = 0;
    float weakTop = 0.0f;
    for (std::size_t i = 0; i < weakFlight.velocities.size(); ++i)
    {
        if (weakFlight.velocities[i].y > 0.0f)
        {
            ++weakRising;
        }
        weakTop = std::max(weakTop, weakFlight.positions[i].y - weakFlight.start.y);
    }
    EXPECT_GT(weakRising, defaultRising);
    EXPECT_NEAR(weakTop, weak.apexHeight, 0.05f);
}

// 頂点の近くで縦の速さが帯の縦速度より小さいフレームが続き、一瞬止まって見える
TEST(LaunchedBody, ArcLingersNearItsApex)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX, .sphereTarget = true});
    ASSERT_NE(rig.body, nullptr);

    const ArcFlight flight = FlyArc(scene, rig, k_TestArc, 120);

    ASSERT_GT(flight.handFrame, 0);
    int lingering = 0;
    for (const Vector3& velocity : flight.velocities)
    {
        if (std::abs(velocity.y) < k_TestArc.apexBandSpeed)
        {
            ++lingering;
        }
    }
    EXPECT_GE(lingering, 6);
}

// 飛び出しを緩めない。最初の 1 フレームから水平の速さ × dt 進む
TEST(LaunchedBody, ArcMovesAtFullHorizontalSpeedFromTheFirstFrame)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    const Vector3 start = rig.object->Root().Position();
    const Vector3 initial = LevelNs::LaunchArcInitialVelocity(k_TestArc);
    ASSERT_GT(initial.x, 0.0f);

    rig.body->Launch(k_TestArc);
    StepBody(scene);

    const Vector3 moved = rig.object->Root().Position();
    EXPECT_NEAR(moved.x - start.x, initial.x * k_FixedDt, 1.0e-4f);
    EXPECT_NEAR(moved.z, start.z, 1.0e-4f);
}

// 物理を回す前に、最初の 1 フレームの変位 ÷ dt が body の速度に入っている。縦は上りの重力の半フレームぶん低い
TEST(LaunchedBody, LaunchWritesTheFirstFrameDisplacementAsVelocity)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    const Vector3 initial = LevelNs::LaunchArcInitialVelocity(k_TestArc);
    ASSERT_GT(initial.y, 0.0f);

    rig.body->Launch(k_TestArc);

    const Vector3 velocity = rig.rigidBody->Velocity();
    EXPECT_NEAR(velocity.x, initial.x, 1.0e-4f);
    EXPECT_NEAR(velocity.z, 0.0f, 1.0e-4f);
    EXPECT_NEAR(velocity.y, initial.y + 0.5f * k_LaunchGravity * k_FixedDt, 1.0e-3f);
}

// 床に接して置かれた物は、飛び出したフレームにも床との接触が出る。離れる向きの接触では渡さない
TEST(LaunchedBody, LaunchFromTheFloorStaysOnTheArcToItsApex)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    const Vector3 start = rig.object->Root().Position();

    rig.body->Launch(k_TestArc);
    StepBody(scene);
    EXPECT_EQ(rig.body->Phase(), LevelNs::LaunchPhase::Arc) << "発射のフレームの床との接触で渡った";

    float top = 0.0f;
    for (int i = 0; i < 60 && rig.body->Phase() == LevelNs::LaunchPhase::Arc; ++i)
    {
        top = std::max(top, rig.object->Root().Position().y - start.y);
        StepBody(scene);
    }
    EXPECT_GT(top, k_TestArc.apexHeight - 0.05f);
}

// 曲線の間は重力と減衰を切り、床に触れたフレームに剛体へ渡して欄を戻す
TEST(LaunchedBody, HandsOffOnTheFrameItTouchesTheFloorAndRestoresTheFields)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    const float linearDamping = rig.rigidBody->LinearDamping();
    const float angularDamping = rig.rigidBody->AngularDamping();
    ASSERT_GT(linearDamping, 0.0f);
    ASSERT_GT(angularDamping, 0.0f);
    ASSERT_TRUE(rig.rigidBody->UsesGravity());

    rig.body->Launch(k_TestArc);
    EXPECT_FALSE(rig.rigidBody->UsesGravity());
    EXPECT_FLOAT_EQ(rig.rigidBody->LinearDamping(), 0.0f);
    EXPECT_FLOAT_EQ(rig.rigidBody->AngularDamping(), 0.0f);

    int touched = 0;
    int handed = 0;
    for (int i = 0; i < 120 && handed == 0; ++i)
    {
        StepBody(scene);
        // 1 フレーム目は置かれていた床との接触が出る
        if (i > 0 && touched == 0 && !rig.rigidBody->Contacts().empty())
        {
            touched = i + 1;
        }
        if (rig.body->Phase() != LevelNs::LaunchPhase::Arc)
        {
            handed = i + 1;
        }
    }

    ASSERT_GT(touched, 0);
    EXPECT_EQ(handed, touched);
    EXPECT_EQ(rig.body->Phase(), LevelNs::LaunchPhase::Rigid);
    EXPECT_TRUE(rig.rigidBody->UsesGravity());
    EXPECT_FLOAT_EQ(rig.rigidBody->LinearDamping(), linearDamping);
    EXPECT_FLOAT_EQ(rig.rigidBody->AngularDamping(), angularDamping);
}

// 連続衝突判定の掃引の命中も接触に出る。速く落ちる球が床へ食い込まず、床に触れたフレームに面の上で止まる
// Jolt が掃引するのは 1 フレームの移動が内接球の半径の 0.75 倍 (この球で 0.56 m)
// を超えた時だけなので、それより速く落とす
TEST(LaunchedBody, ContinuousCollisionReportsTheSweptHitOnTheFloor)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.sphereTarget = true});
    ASSERT_NE(rig.body, nullptr);
    ASSERT_TRUE(rig.rigidBody->IsContinuousCollision());
    rig.rigidBody->Teleport(Vector3{0.0f, k_SphereRestY + 3.0f, 0.0f}, rig.object->Root().Rotation());
    // 曲線の間と同じく減衰を切る。減衰 3.0 では 1 フレームに 5% 遅くなり、床に届く前に掃引の下限を割る
    rig.rigidBody->SetLinearDamping(0.0f);
    rig.rigidBody->SetAngularDamping(0.0f);

    rig.body->LaunchRigid(Vector3{0.0f, -40.0f, 0.0f});
    int touched = 0;
    for (int i = 0; i < 60 && touched == 0; ++i)
    {
        StepBody(scene);
        if (!rig.rigidBody->Contacts().empty())
        {
            touched = i + 1;
        }
    }

    ASSERT_GT(touched, 0);
    EXPECT_NEAR(rig.object->Root().Position().y, k_SphereRestY, 0.03f);
}

// 渡す瞬間に速度も回る速さも書き換えない。摩擦も跳ね返りも無い球なら、着地は縦の速さを消すだけ
// 曲線の回る速さは毎フレーム同じ値なので、渡すフレームに曲線の値を書き直しても見分けられない
// 渡す直前に曲線と違う回る速さを置き、渡した後もその値のまま残ることを見る
TEST(LaunchedBody, HandOffKeepsTheArcVelocityAndSpin)
{
    const Vector3 initial = LevelNs::LaunchArcInitialVelocity(k_TestArc);
    // 曲線の回る速さは水平の軸まわり。縦の軸まわりの値なら、曲線の式から出た値と取り違えない
    const Vector3 marker{0.0f, 5.0f, 0.0f};

    // 1 回目は渡すフレームを数えるだけ
    int handed = 0;
    {
        SceneNs::Scene scene;
        BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX, .sphereTarget = true});
        ASSERT_NE(rig.body, nullptr);
        rig.rigidBody->SetFriction(0.0f);
        rig.rigidBody->SetRestitution(0.0f);
        rig.body->Launch(k_TestArc);
        for (int i = 0; i < 120 && handed == 0; ++i)
        {
            StepBody(scene);
            if (rig.body->Phase() != LevelNs::LaunchPhase::Arc)
            {
                handed = i + 1;
            }
        }
    }
    ASSERT_GT(handed, 1);

    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX, .sphereTarget = true});
    ASSERT_NE(rig.body, nullptr);
    rig.rigidBody->SetFriction(0.0f);
    rig.rigidBody->SetRestitution(0.0f);
    rig.body->Launch(k_TestArc);
    for (int i = 0; i < handed - 1; ++i)
    {
        StepBody(scene);
        ASSERT_EQ(rig.body->Phase(), LevelNs::LaunchPhase::Arc) << i;
    }
    rig.rigidBody->SetAngularVelocity(marker);
    StepBody(scene);

    ASSERT_EQ(rig.body->Phase(), LevelNs::LaunchPhase::Rigid);
    const Vector3 velocity = rig.rigidBody->Velocity();
    EXPECT_NEAR(velocity.x, initial.x, 1.0e-3f);
    EXPECT_NEAR(velocity.z, initial.z, 1.0e-3f);
    EXPECT_NEAR(velocity.y, 0.0f, 1.0e-3f);
    const Vector3 spinAfter = rig.rigidBody->AngularVelocity();
    EXPECT_NEAR(spinAfter.x, marker.x, 1.0e-3f);
    EXPECT_NEAR(spinAfter.y, marker.y, 1.0e-3f);
    EXPECT_NEAR(spinAfter.z, marker.z, 1.0e-3f);
}

// 同じ曲線なら同じ所に止まる
TEST(LaunchedBody, SameArcRestsAtTheSameSpot)
{
    SceneNs::Scene first;
    BodyRig one = BuildBody(first, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(one.body, nullptr);
    SceneNs::Scene second;
    BodyRig two = BuildBody(second, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(two.body, nullptr);

    one.body->Launch(k_TestArc);
    two.body->Launch(k_TestArc);
    ASSERT_LT(RunUntilRest(first, *one.body, k_RestStepLimit), k_RestStepLimit);
    ASSERT_LT(RunUntilRest(second, *two.body, k_RestStepLimit), k_RestStepLimit);

    const Vector3 a = one.object->Root().Position();
    const Vector3 b = two.object->Root().Position();
    EXPECT_EQ(a.x, b.x);
    EXPECT_EQ(a.y, b.y);
    EXPECT_EQ(a.z, b.z);
}

// 飛んでいる最中に飛ばし直すと、今の位置から新しい曲線が始まる。切る前の欄は最初の発射の時の値のまま戻る
TEST(LaunchedBody, RelaunchWhileFlyingStartsTheNewArcFromWhereItIs)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    const float linearDamping = rig.rigidBody->LinearDamping();
    rig.body->Launch(k_TestArc);
    for (int i = 0; i < 10; ++i)
    {
        StepBody(scene);
    }
    ASSERT_EQ(rig.body->Phase(), LevelNs::LaunchPhase::Arc);
    const Vector3 before = rig.object->Root().Position();
    // 前の曲線と速さが違う曲線にし、前の曲線を辿り続けていれば落ちるようにする
    LevelNs::LaunchArc again = k_TestArc;
    again.distance = 6.0f;
    const Vector3 initial = LevelNs::LaunchArcInitialVelocity(again);

    rig.body->Launch(again);
    StepBody(scene);

    const Vector3 after = rig.object->Root().Position();
    EXPECT_NEAR(after.x - before.x, initial.x * k_FixedDt, 1.0e-4f);
    EXPECT_NEAR(after.y - before.y, (initial.y + 0.5f * k_LaunchGravity * k_FixedDt) * k_FixedDt, 1.0e-4f);
    for (int i = 0; i < 120 && rig.body->Phase() == LevelNs::LaunchPhase::Arc; ++i)
    {
        StepBody(scene);
    }
    EXPECT_EQ(rig.body->Phase(), LevelNs::LaunchPhase::Rigid);
    EXPECT_TRUE(rig.rigidBody->UsesGravity());
    EXPECT_FLOAT_EQ(rig.rigidBody->LinearDamping(), linearDamping);
}

// 曲線の間の読み口は、そのフレームの物理が動かした速度を返す
// body の速度は LateUpdate が次のフレームのために書いた後の値で、上りでは縦が重力の 1 フレームぶん低い
TEST(LaunchedBody, VelocityDuringTheArcIsWhatTheLastFrameMovedBy)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.sphereTarget = true});
    ASSERT_NE(rig.body, nullptr);
    rig.body->Launch(k_TestArc);
    // 上りの途中で、縦の速さが頂点の帯より大きい間。帯の外なので 1 フレームの縦の変化は上りの重力 × dt
    constexpr int k_RisingFrames = 10;
    Vector3 before = rig.object->Root().Position();
    for (int i = 0; i < k_RisingFrames; ++i)
    {
        before = rig.object->Root().Position();
        StepBody(scene);
    }
    ASSERT_EQ(rig.body->Phase(), LevelNs::LaunchPhase::Arc);

    const Vector3 moved = (rig.object->Root().Position() - before) / k_FixedDt;
    const Vector3 velocity = rig.body->Velocity();
    EXPECT_NEAR(velocity.x, moved.x, 1.0e-4f);
    EXPECT_NEAR(velocity.y, moved.y, 1.0e-4f);
    EXPECT_NEAR(velocity.z, moved.z, 1.0e-4f);
    EXPECT_NEAR(velocity.y - rig.rigidBody->Velocity().y, -k_LaunchGravity * k_FixedDt, 1.0e-3f);
}

// RigidBody を積み忘れた配置物も飛ぶ。飛ばす時にキネマティックで足し、collider はその形になる
TEST(LaunchedBody, LaunchAddsRigidBodyWhenMissing)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.withRigidBody = false});
    ASSERT_NE(rig.body, nullptr);
    ASSERT_NE(rig.box, nullptr);
    ASSERT_EQ(rig.rigidBody, nullptr);

    rig.body->Launch(k_TestArc);

    SceneNs::RigidBody* added = rig.object->FindComponent<SceneNs::RigidBody>();
    ASSERT_NE(added, nullptr);
    EXPECT_TRUE(rig.body->IsFlying());
    EXPECT_FALSE(added->IsKinematic());
    EXPECT_TRUE(rig.box->IsActiveSelf());
    EXPECT_FALSE(added->BodyId().IsInvalid());
    EXPECT_EQ(rig.box->BodyId(), added->BodyId());
    // collider の静的な body は外れ、RigidBody の body が代わりに立つので数は変わらない
    EXPECT_EQ(scene.Physics().BodyCount(), rig.restingBodies);
}

// 空気抵抗の分だけ狙いより短く進む。狙いを丸ごと超えたり横へ逸れたりはしない
TEST(LaunchedBody, AdvancesHorizontallyByVelocityPerStep)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    UseDebrisDamping(rig);
    const Vector3 start = rig.object->Root().Position();
    rig.body->LaunchRigid(Vector3{10.0f, 4.0f, 0.0f});

    StepBody(scene);

    const Vector3 moved = rig.object->Root().Position();
    EXPECT_NEAR(moved.x - start.x, 10.0f * k_FixedDt, 0.005f);
    EXPECT_NEAR(moved.z, start.z, 1.0e-4f);
}

TEST(LaunchedBody, GravityReducesVerticalSpeedEachStep)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    UseDebrisDamping(rig);
    rig.body->LaunchRigid(Vector3{0.0f, 6.0f, 0.0f});

    float expected = 6.0f;
    expected += k_LaunchGravity * k_FixedDt;
    StepBody(scene);
    EXPECT_NEAR(rig.body->Velocity().y, expected, 0.01f);

    expected += k_LaunchGravity * k_FixedDt;
    StepBody(scene);
    EXPECT_NEAR(rig.body->Velocity().y, expected, 0.02f);
    EXPECT_TRUE(rig.body->IsFlying());
}

TEST(LaunchedBody, LandsOnFloorTopAndZeroesVerticalSpeed)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    rig.body->Launch(k_TestArc);
    ASSERT_TRUE(rig.body->IsFlying());

    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);

    // 高さ 2 の曲線は発射の高さへ戻るまで 48 フレーム。着地より前には止まらない
    EXPECT_GT(steps, 48);
    EXPECT_LT(steps, k_RestStepLimit);
    EXPECT_NEAR(rig.object->Root().Position().y, k_BodyRestY, 1.0e-4f);
    EXPECT_FLOAT_EQ(rig.body->Velocity().y, 0.0f);
}

TEST(LaunchedBody, GroundFrictionSlowsHorizontalSpeed)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    rig.body->LaunchRigid(Vector3{4.0f, 0.0f, 0.0f});

    StepBody(scene);
    const float first = rig.body->Velocity().x;
    StepBody(scene);
    const float second = rig.body->Velocity().x;

    EXPECT_LT(first, 4.0f);
    EXPECT_LT(second, first);
    EXPECT_GT(second, 0.0f);
}

// 止まった所でキネマティックへ戻る。次に飛ばされるまでその場の当たりとして残る
TEST(LaunchedBody, RestReturnsRigidBodyToKinematic)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    ASSERT_NE(rig.rigidBody, nullptr);
    rig.body->Launch(k_TestArc);
    ASSERT_FALSE(rig.rigidBody->IsKinematic());

    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);

    EXPECT_LT(steps, k_RestStepLimit);
    EXPECT_FALSE(rig.body->IsFlying());
    EXPECT_TRUE(rig.rigidBody->IsKinematic());
    EXPECT_TRUE(rig.box->IsActiveSelf());
    EXPECT_EQ(rig.box->BodyId(), rig.rigidBody->BodyId());
    EXPECT_EQ(scene.Physics().BodyCount(), rig.restingBodies);
    const Vector3 velocity = rig.rigidBody->Velocity();
    EXPECT_FLOAT_EQ(velocity.x, 0.0f);
    EXPECT_FLOAT_EQ(velocity.y, 0.0f);
    EXPECT_FLOAT_EQ(velocity.z, 0.0f);
    EXPECT_FLOAT_EQ(rig.body->Velocity().x, 0.0f);
    EXPECT_FLOAT_EQ(rig.body->Velocity().z, 0.0f);
}

// 曲線の距離より先で、床の端より手前に止まる
TEST(LaunchedBody, RestsAwayFromLaunchPosition)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    const Vector3 start = rig.object->Root().Position();
    rig.body->Launch(k_TestArc);

    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);
    ASSERT_LT(steps, k_RestStepLimit);

    const float travelled = rig.object->Root().Position().x - start.x;
    EXPECT_GT(travelled, k_TestArc.distance);
    EXPECT_LT(rig.object->Root().Position().x, static_cast<float>(k_LongFloorLastX) + 0.5f);
}

// 曲線の間は毎フレーム、回転の強さ × 水平の速さで回る。上面が進行方向へ倒れる前転
// 押されて回る速さが変わっても、次のフレームに曲線の回り方へ戻る
TEST(LaunchedBody, TumblesForwardWhileFlying)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    const float spinPerSpeed = NsTest::ReadTuningField(*rig.body, "回転の強さ");
    ASSERT_GT(spinPerSpeed, 0.0f);
    // +X へ飛ぶので、軸は上向きと進む向きの外積 (0, 0, -1)
    const float rate = spinPerSpeed * HorizontalSpeed(LevelNs::LaunchArcInitialVelocity(k_TestArc));
    const Vector3 expected{0.0f, 0.0f, -rate};

    rig.body->Launch(k_TestArc);
    for (int i = 0; i < 10; ++i)
    {
        // 後ろから別の物に押された時と同じく、接触で回る速さが変わった形
        if (i == 5)
        {
            rig.rigidBody->SetAngularVelocity(Vector3{0.0f, 0.0f, 0.0f});
        }
        StepBody(scene);
        ASSERT_EQ(rig.body->Phase(), LevelNs::LaunchPhase::Arc) << i;
        const Vector3 spin = rig.rigidBody->AngularVelocity();
        EXPECT_NEAR(spin.x, expected.x, 1.0e-3f) << i;
        EXPECT_NEAR(spin.y, expected.y, 1.0e-3f) << i;
        EXPECT_NEAR(spin.z, expected.z, 1.0e-3f) << i;
    }

    const Vector3 up = Vector3::Transform(Vector3{0.0f, 1.0f, 0.0f}, rig.object->Root().Rotation());
    EXPECT_GT(up.x, 0.0f) << "上面が進行方向へ倒れていない";
    EXPECT_NEAR(up.z, 0.0f, 1.0e-4f);
}

// 高さが同じなら滞空も同じで、距離の長い曲線ほど水平に速く、速く回る
TEST(LaunchedBody, FasterFlightSpinsFaster)
{
    SceneNs::Scene slowScene;
    BodyRig slow = BuildBody(slowScene);
    ASSERT_NE(slow.body, nullptr);
    slow.body->Launch(k_TestArc);

    SceneNs::Scene fastScene;
    BodyRig fast = BuildBody(fastScene);
    ASSERT_NE(fast.body, nullptr);
    LevelNs::LaunchArc farArc = k_TestArc;
    farArc.distance = 2.0f * k_TestArc.distance;
    fast.body->Launch(farArc);

    for (int i = 0; i < 5; ++i)
    {
        StepBody(slowScene);
        StepBody(fastScene);
    }
    ASSERT_EQ(slow.body->Phase(), LevelNs::LaunchPhase::Arc);
    ASSERT_EQ(fast.body->Phase(), LevelNs::LaunchPhase::Arc);

    const Vector3 slowUp = Vector3::Transform(Vector3{0.0f, 1.0f, 0.0f}, slow.object->Root().Rotation());
    const Vector3 fastUp = Vector3::Transform(Vector3{0.0f, 1.0f, 0.0f}, fast.object->Root().Rotation());
    EXPECT_GT(fastUp.x, slowUp.x);
    EXPECT_GT(slowUp.x, 0.0f);
}

// 転がった向きのまま止まる。姿勢を配置へ戻すと、転がった跡が消えて置き直したように見える
TEST(LaunchedBody, KeepsTheRolledRotationAfterItStops)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    const NS::Core::Quaternion home = rig.object->Root().Rotation();
    rig.body->Launch(k_TestArc);

    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);
    ASSERT_LT(steps, k_RestStepLimit);

    const NS::Core::Quaternion rest = rig.object->Root().Rotation();
    const float alignment = std::abs(rest.x * home.x + rest.y * home.y + rest.z * home.z + rest.w * home.w);
    EXPECT_LT(alignment, 0.999f);
}

// 着地しただけで止まらず、勢いの残りだけ転がって進む
TEST(LaunchedBody, KeepsRollingAfterItLands)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    const ArcFlight flight = FlyArc(scene, rig, k_TestArc, k_RestStepLimit);
    ASSERT_GT(flight.handFrame, 0);
    const float landedX = flight.positions.back().x;

    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);
    ASSERT_LT(steps, k_RestStepLimit);

    EXPECT_GT(rig.object->Root().Position().x, landedX + 0.2f);
}

// 同梱の球と同じ欄の球を溜めきりの曲線で飛ばすと、剛体へ渡してから線の内に止まり、転がる距離が飛んだ距離を超えない
TEST(LaunchedBody, ShippedSphereRestsWithinTheLineAfterAFullChargeArc)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_RollFloorLastX, .sphereTarget = true});
    ASSERT_NE(rig.body, nullptr);
    const ArcFlight flight = FlyArc(scene, rig, k_FullChargeArc, k_RestStepLimit);
    ASSERT_GT(flight.handFrame, 0);
    const float landedX = flight.positions.back().x;
    const float flown = landedX - flight.start.x;

    // 線ちょうどで止まった時と止まらなかった時を分けるため、1 フレーム多く回す
    const int steps = RunUntilRest(scene, *rig.body, k_RestLineFrames + 1);
    EXPECT_LE(steps, k_RestLineFrames) << "剛体へ渡してから線の内に止まっていない";
    ASSERT_FALSE(rig.body->IsFlying());

    const float rolled = rig.object->Root().Position().x - landedX;
    EXPECT_LE(rolled, flown) << "飛んだ " << flown << " m より長く転がった";
    EXPECT_GT(rolled, 0.0f);
}

TEST(LaunchedBody, SpinStrengthFieldStopsRotation)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    SetFloatField(*rig.body, "回転の強さ", 0.0f);
    rig.body->Launch(k_TestArc);

    for (int i = 0; i < 10; ++i)
    {
        StepBody(scene);
    }
    ASSERT_EQ(rig.body->Phase(), LevelNs::LaunchPhase::Arc);

    const Vector3 up = Vector3::Transform(Vector3{0.0f, 1.0f, 0.0f}, rig.object->Root().Rotation());
    EXPECT_FLOAT_EQ(up.x, 0.0f);
    EXPECT_FLOAT_EQ(up.y, 1.0f);
}

TEST(LaunchedBody, IdleStaysPutAndKeepsCollider)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);
    const Vector3 start = rig.object->Root().Position();

    for (int i = 0; i < 60; ++i)
        StepBody(scene);

    const Vector3 now = rig.object->Root().Position();
    EXPECT_FALSE(rig.body->IsFlying());
    EXPECT_TRUE(rig.box->IsActiveSelf());
    EXPECT_FLOAT_EQ(now.x, start.x);
    EXPECT_FLOAT_EQ(now.y, start.y);
    EXPECT_FLOAT_EQ(now.z, start.z);
    EXPECT_EQ(scene.Physics().BodyCount(), rig.restingBodies);
}

TEST(LaunchedBody, NonFiniteOrNonPositiveArcIsIgnored)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene);
    ASSERT_NE(rig.body, nullptr);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    std::vector<LevelNs::LaunchArc> broken;
    for (const float bad : {nan, infinity, 0.0f, -1.0f})
    {
        LevelNs::LaunchArc arc = k_TestArc;
        arc.distance = bad;
        broken.push_back(arc);
        arc = k_TestArc;
        arc.apexHeight = bad;
        broken.push_back(arc);
        arc = k_TestArc;
        arc.riseGravity = bad;
        broken.push_back(arc);
    }

    for (const LevelNs::LaunchArc& arc : broken)
    {
        rig.body->Launch(arc);
        EXPECT_FALSE(rig.body->IsFlying())
            << "距離 " << arc.distance << "・高さ " << arc.apexHeight << "・上りの重力 " << arc.riseGravity;
    }
    EXPECT_TRUE(rig.rigidBody->IsKinematic());
    EXPECT_TRUE(rig.box->IsActiveSelf());
    EXPECT_EQ(scene.Physics().BodyCount(), rig.restingBodies);
}

// 押し飛ばされた配置物は消えない。0 は消えない指定
TEST(LaunchedBody, RestWithZeroLifeStaysVisible)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    rig.body->Launch(k_TestArc);
    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);
    ASSERT_LT(steps, k_RestStepLimit);

    for (int i = 0; i < 120; ++i)
        StepBody(scene);

    SceneNs::MeshRenderer* mesh = rig.object->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_TRUE(mesh->IsActiveSelf());
    EXPECT_TRUE(rig.box->IsActiveSelf());
    EXPECT_TRUE(rig.body->IsActiveSelf());
}

// 寿命を入れた物は止まってから消え、当たりも外れる
TEST(LaunchedBody, SetRestLifeSecondsHidesAfterRest)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    rig.body->SetRestLifeSeconds(0.05f);
    rig.body->Launch(k_TestArc);
    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);
    ASSERT_LT(steps, k_RestStepLimit);

    for (int i = 0; i < 5; ++i)
        StepBody(scene);

    SceneNs::MeshRenderer* mesh = rig.object->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_FALSE(mesh->IsActiveSelf());
    EXPECT_FALSE(rig.box->IsActiveSelf());
    EXPECT_FALSE(rig.body->IsActiveSelf());
    EXPECT_EQ(scene.Physics().BodyCount(), rig.restingBodies - 1);
}

// 壊れた値は捨てる。非有限値と負で寿命が入らない
TEST(LaunchedBody, RestLifeRejectsNonFiniteAndNegative)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    rig.body->SetRestLifeSeconds(std::numeric_limits<float>::quiet_NaN());
    rig.body->SetRestLifeSeconds(-2.0f);
    rig.body->Launch(k_TestArc);
    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);
    ASSERT_LT(steps, k_RestStepLimit);

    for (int i = 0; i < 120; ++i)
        StepBody(scene);

    SceneNs::MeshRenderer* mesh = rig.object->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_TRUE(mesh->IsActiveSelf());
}

// 跡は指定位置に出る一時オブジェクト。保存や凍結に写らない印が立つ
TEST(ImpactMark, SpawnAtPlacesTransientMark)
{
    NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    const std::size_t before = scene.Objects().ObjectCount();

    SceneNs::GameObject* mark = LevelNs::ImpactMark::SpawnAt(&scene, Vector3{3.0f, 0.02f, 5.0f});

    ASSERT_NE(mark, nullptr);
    EXPECT_EQ(scene.Objects().ObjectCount(), before + 1);
    EXPECT_TRUE(mark->IsTransient());
    const Vector3 pos = mark->Root().Position();
    EXPECT_FLOAT_EQ(pos.x, 3.0f);
    EXPECT_FLOAT_EQ(pos.y, 0.02f);
    EXPECT_FLOAT_EQ(pos.z, 5.0f);
    EXPECT_EQ(LevelNs::ImpactMark::SpawnAt(nullptr, Vector3{}), nullptr);
}

// 跡は保存に写らない
TEST(ImpactMark, SkipsSaveCapture)
{
    NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    SceneNs::GameObject* mark = LevelNs::ImpactMark::SpawnAt(&scene, Vector3{0.0f, 0.02f, 0.0f});
    ASSERT_NE(mark, nullptr);

    const nlohmann::json data = scene.ToJson();

    EXPECT_TRUE(SceneNs::SceneJsonObjects(data).empty());
}

// 見た目は床へ寝かせた半透明の板
TEST(ImpactMark, UsesShadowQuadLook)
{
    NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    SceneNs::GameObject* mark = LevelNs::ImpactMark::SpawnAt(&scene, Vector3{0.0f, 0.02f, 0.0f});
    ASSERT_NE(mark, nullptr);

    SceneNs::MeshRenderer* mesh = mark->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->MeshRef(), "shadowQuad");
    EXPECT_EQ(mesh->MaterialRef(), "shadow");
}

// 出た直後の水平の大きさが跡の直径
TEST(ImpactMark, StartsAtDiameter)
{
    NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    SceneNs::GameObject* mark = LevelNs::ImpactMark::SpawnAt(&scene, Vector3{0.0f, 0.02f, 0.0f});
    ASSERT_NE(mark, nullptr);

    const Vector3 scale = mark->Root().Scale();
    EXPECT_FLOAT_EQ(scale.x, 1.5f);
    EXPECT_FLOAT_EQ(scale.z, 1.5f);
}

// 寿命の半分で大きさも半分。線形に縮む
TEST(ImpactMark, ShrinksToHalfAtHalfLife)
{
    NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    SceneNs::GameObject* mark = LevelNs::ImpactMark::SpawnAt(&scene, Vector3{0.0f, 0.02f, 0.0f});
    ASSERT_NE(mark, nullptr);

    for (int i = 0; i < 180; ++i)
        StepBody(scene);

    const Vector3 scale = mark->Root().Scale();
    EXPECT_NEAR(scale.x, 0.75f, 0.02f);
    EXPECT_NEAR(scale.z, 0.75f, 0.02f);
}

// 寿命が尽きたら描画と更新を止める。配置物は破棄しない
TEST(ImpactMark, HidesAfterLifeWithoutDestroy)
{
    NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    SceneNs::GameObject* mark = LevelNs::ImpactMark::SpawnAt(&scene, Vector3{0.0f, 0.02f, 0.0f});
    ASSERT_NE(mark, nullptr);
    const std::size_t after = scene.Objects().ObjectCount();

    for (int i = 0; i < 370; ++i)
        StepBody(scene);

    SceneNs::MeshRenderer* mesh = mark->FindComponent<SceneNs::MeshRenderer>();
    ASSERT_NE(mesh, nullptr);
    EXPECT_FALSE(mesh->IsActiveSelf());
    LevelNs::ImpactMark* comp = mark->FindComponent<LevelNs::ImpactMark>();
    ASSERT_NE(comp, nullptr);
    EXPECT_FALSE(comp->IsActiveSelf());
    EXPECT_EQ(scene.Objects().ObjectCount(), after);
}

// 壁に当たっても割れず、そのフレームに剛体へ渡して跳ね返る
TEST(LaunchedBody, WallHandsOffWithoutShattering)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.withWall = true});
    ASSERT_NE(rig.body, nullptr);
    const std::size_t debrisBefore = DebrisBodies(scene).size();
    // 回っている箱は角で当たり、跳ね返る向きが回転で変わる。回転は別の試しで見る
    SetFloatField(*rig.body, "回転の強さ", 0.0f);
    // 低く速い曲線。上がり切る前に 1 m 先の壁の面へ、箱の底が壁の上端より低いまま届く
    LevelNs::LaunchArc low = k_TestArc;
    low.apexHeight = 1.0f;

    const ArcFlight flight = FlyArc(scene, rig, low, 30);

    ASSERT_GT(flight.handFrame, 0);
    EXPECT_EQ(rig.body->Phase(), LevelNs::LaunchPhase::Rigid);
    EXPECT_LT(rig.rigidBody->Velocity().x, 0.0f) << "壁で跳ね返っていない";
    for (int i = 0; i < 60; ++i)
    {
        StepBody(scene);
    }
    EXPECT_EQ(DebrisBodies(scene).size(), debrisBefore);
    EXPECT_TRUE(rig.box->IsActiveSelf());
    EXPECT_LT(rig.object->Root().Position().x, static_cast<float>(k_WallX));
}

TEST(LaunchedBody, LandingOnFloorDoesNotShatter)
{
    SceneNs::Scene scene;
    BodyRig rig = BuildBody(scene, {.floorLastX = k_LongFloorLastX});
    ASSERT_NE(rig.body, nullptr);
    const std::size_t debrisBefore = DebrisBodies(scene).size();

    rig.body->Launch(k_TestArc);
    const int steps = RunUntilRest(scene, *rig.body, k_RestStepLimit);

    EXPECT_LT(steps, k_RestStepLimit);
    EXPECT_EQ(DebrisBodies(scene).size(), debrisBefore);
    EXPECT_NEAR(rig.object->Root().Position().y, k_BodyRestY, 0.05f);
}

// 飛ぶ形は当たり箱から作る。当たりの無い物は飛ばず、物理の body も増えない
TEST(LaunchedBody, DoesNotFlyWithoutCollider)
{
    NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);
    SceneNs::Scene scene;
    nlohmann::json data = SceneNs::MakeSceneJson();
    nlohmann::json bare = SceneNs::MakeObjectJson();
    SceneNs::ObjectJsonComponents(bare).push_back(SceneNs::MakeComponentEntry("LaunchedBody"));
    SceneNs::SceneJsonObjects(data).push_back(bare);
    scene.LoadJson(std::move(data));

    LevelNs::LaunchedBody* body = nullptr;
    scene.Objects().ForEachComponent<LevelNs::LaunchedBody>([&body](LevelNs::LaunchedBody& found) { body = &found; });
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->Owner()->FindComponent<SceneNs::Collider>(), nullptr);
    const JPH::uint bodiesBefore = scene.Physics().BodyCount();

    body->Launch(k_TestArc);

    EXPECT_FALSE(body->IsFlying());
    EXPECT_EQ(scene.Physics().BodyCount(), bodiesBefore);
}

// 飛ばされた物の重力は自機の上昇重力と同じ値
TEST(LaunchedBody, FallsAtThePlayersUpwardGravity)
{
    NS::Phys::PhysicsScene physics;
    NS::Core::Sphere ball;
    ball.center = Vector3{0.0f, 50.0f, 0.0f};
    ball.radius = 0.5f;
    const JPH::BodyID body = physics.AddDynamicSphere(ball, NS::Phys::DynamicBodyDesc{});
    physics.OptimizeBroadPhase();

    physics.Update(k_FixedDt);
    const float fallenSpeed = physics.BodyVelocity(body).y;

    NS::Obj::GameObject probe;
    NS::Game::Player::PlayerComponent& player = *probe.AddComponent<NS::Game::Player::PlayerComponent>();
    EXPECT_NEAR(fallenSpeed / k_FixedDt, NsTest::ReadTuningField(player, "上昇重力"), 1.0f);
}

namespace
{
    // 的を 3 列先に置き、玉の縁から的の面まで 2.1 m 空ける。前出しで当たりが早まる間を取る
    // 横ずれ 0.45 m は試験の的の範囲で惜しい
    constexpr SlamCourse k_LeadCourse{.start = 0.0f, .lateral = 0.45f, .targetCell = 3};

    // 前出しを入れて突進し、当たりが出たフレームまで進める
    struct LeadRun
    {
        int steps = 0;
        LevelNs::HitTier tier = LevelNs::HitTier::Wide;
    };

    LeadRun RunWithLead(float lead)
    {
        SceneNs::Scene scene;
        Rig rig = BuildSlam(scene, k_LeadCourse);
        SetInstantImpact(rig);
        SetFloatField(*rig.impact, "前出し", lead);
        BeginSlam(scene, rig, k_RunSpeed, 0.0f);
        LeadRun run;
        run.steps = StepUntilImpact(scene, rig, 60);
        run.tier = rig.impact->LastImpact().tier;
        return run;
    }
} // namespace

// 前出し 0 は今の当たりのまま。前出しを入れると体が触れる前のフレームで当たりが出て、段は変わらない
TEST(CollisionImpact, LeadMakesTheHitComeEarlierWithTheSameTier)
{
    const LeadRun plain = RunWithLead(0.0f);
    const LeadRun lead = RunWithLead(0.3f);
    ASSERT_LT(plain.steps, 60);
    EXPECT_LT(lead.steps, plain.steps);
    EXPECT_EQ(plain.tier, LevelNs::HitTier::Near);
    EXPECT_EQ(lead.tier, plain.tier);
}

// 止めの頭で、自機を相手に接する所まで寄せる。寄せた距離は前出しを超えない。前出し 0 では寄せない
TEST(CollisionImpact, FreezeSnapsThePlayerUpToTheTarget)
{
    for (const float leadLength : {0.0f, 0.3f})
    {
        SCOPED_TRACE(leadLength);
        SceneNs::Scene scene;
        Rig rig = BuildSlam(scene, k_LeadCourse);
        SetFloatField(*rig.impact, "前出し", leadLength);
        BeginSlam(scene, rig, k_RunSpeed, 0.0f);
        ASSERT_LT(StepUntilImpact(scene, rig, 60), 60);
        // 当たりのフレームの移動は本番では裁定の後に走る。StepUntilImpact は走らせずに返すので、ここで走らせる
        rig.movement->OnUpdate();
        // 当たりの次のフレームが止めの頭
        StepWorld(scene);
        ASSERT_TRUE(rig.impact->FreezeBeganThisStep());

        const float snap = rig.impact->LastImpact().snapDistance;
        const float ballFront = rig.movement->Owner()->Root().Position().x + rig.movement->CapsuleRadius();
        // 食い込ませる前の的の面。止めの頭で的は突進の向きへ食い込むので、控えた元の位置から測る
        const float face = rig.impact->LastImpact().targetPos.x - 0.5f;
        // 前出し 0 の自機は的の 2 cm 手前で止まる (自機の足が壁の手前に空ける隙間、2026-09-29 に測った)
        // 寄せた後は触れる所の 1 mm の幅の内
        EXPECT_NEAR(ballFront, face, 0.03f);
        EXPECT_LE(snap, leadLength + 1.0e-4f);
        if (leadLength == 0.0f)
        {
            EXPECT_FLOAT_EQ(snap, 0.0f);
        }
        else
        {
            EXPECT_GT(snap, 0.0f);
        }
    }
}

// 予測は突進距離 + 前出しまで相手を拾う
TEST(CollisionImpact, SlamLineTargetReachesTheRushDistancePlusTheLead)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_LeadCourse);
    SettleOnFloor(scene, rig);
    const Vector3 direction{1.0f, 0.0f, 0.0f};
    LevelNs::SlamLineTarget found{};
    ASSERT_TRUE(rig.impact->FindSlamLineTarget(direction, 10.0f, found));
    const float shortOfContact = found.contact - 0.2f;

    LevelNs::SlamLineTarget missed{};
    EXPECT_FALSE(rig.impact->FindSlamLineTarget(direction, shortOfContact, missed));
    SetFloatField(*rig.impact, "前出し", 0.3f);
    LevelNs::SlamLineTarget reached{};
    EXPECT_TRUE(rig.impact->FindSlamLineTarget(direction, shortOfContact, reached));
}

// 突進していない間は、前出しの範囲に相手がいても当たりにならない
TEST(CollisionImpact, LeadDoesNothingWhileNotRushing)
{
    SceneNs::Scene scene;
    Rig rig = BuildSlam(scene, k_NearCourse);
    SetInstantImpact(rig);
    SetFloatField(*rig.impact, "前出し", 0.5f);
    SettleOnFloor(scene, rig);
    for (int i = 0; i < 10; ++i)
    {
        Step(scene, rig);
        EXPECT_FALSE(rig.impact->DidRebound()) << i;
    }
}
