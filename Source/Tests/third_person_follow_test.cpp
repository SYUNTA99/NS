#include "Editor/LevelEditorController.h"
#include "Editor/Undo/ActorSnapshotApplier.h"
#include "Game/Level/FollowCameraActor.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IUse/IUseCamera.h"
#include "NSlib/CoreObject/IUse/IUseCameraTarget.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/System/SceneSystem.h"
#include "NSlib/CoreObject/UpdatePhase.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

// 追従カメラは追う相手を取得して持つ。エディタで「追従対象」を書き換えた後は、書き換えた先の相手を追う

namespace
{
    // 追われるだけの動かない物
    class FollowedProbeActor final : public NS::SceneActor
    {
    };

    struct FollowScene
    {
        NS::Scene scene;
        FollowedProbeActor* first = nullptr;
        FollowedProbeActor* second = nullptr;
        GL::Level::FollowCameraActor* camera = nullptr;

        FollowScene()
        {
            first = scene.SpawnActor<FollowedProbeActor>("first");
            second = scene.SpawnActor<FollowedProbeActor>("second");
            second->Root().SetPosition({10.0f, 0.0f, 0.0f});
            nlohmann::json json = NS::MakeActorJson();
            NS::SetActorJsonId(json, 50);
            NS::SetActorJsonFile(json, "FollowCameraActor");
            NS::ActorJsonClassObjs(json)["Vcam"] = nlohmann::json{{"追従対象", nlohmann::json{{"ref", first->Id()}}}};
            camera = static_cast<GL::Level::FollowCameraActor*>(scene.SpawnFromJson(json));
        }

        // 固定フレームを進めてから、補間を終えた姿勢の注視点を返す
        NS::Vector3 LookAfterUpdate()
        {
            scene.ExecutePhase(NS::UpdatePhase::Camera);
            return camera->Vcam().EvaluatePose(1.0f).target;
        }
    };

    // 更新のたびに +x へ 1 m 進み、追従カメラに状態を読まれた時の自分の位置を控える物
    class WalkingTargetActor final : public NS::SceneActor, public NS::IUseCameraTarget
    {
    public:
        explicit WalkingTargetActor(std::vector<std::string>& log) : m_log(log) {}
        void WriteFollowState(NS::ThirdPersonFollow* vcam) const override
        {
            NS_ASSERT(Game, vcam != nullptr, "WriteFollowState: 渡したアクターか部品が無い");
            vcam->SetFollowMotion(true, NS::Vector3{});
            m_log.push_back("camera");
            seenPositions.push_back(Root().Position());
        }
        mutable std::vector<NS::Vector3> seenPositions; // 読まれた時の位置。書くのは WriteFollowState だけ

    protected:
        void DecideStep() override
        {
            m_log.push_back("target");
            Root().SetPosition(Root().Position() + NS::Vector3{1.0f, 0.0f, 0.0f});
        }

    private:
        std::vector<std::string>& m_log;
    };

    // コースの段で、追われる物を +z へ 5 m 置き直す
    class CourseMoverSystem final : public NS::SceneSystem
    {
    public:
        CourseMoverSystem(NS::SceneActor* target, std::vector<std::string>& log) : m_target(target), m_log(log)
        {
            NS_ASSERT(Game, target != nullptr, "CourseMoverSystem: 置き直す物が無い");
        }
        void OnTick() override
        {
            m_log.push_back("course");
            m_target->Root().SetPosition(m_target->Root().Position() + NS::Vector3{0.0f, 0.0f, 5.0f});
        }

    private:
        NS::SceneActor* m_target = nullptr;
        std::vector<std::string>& m_log;
    };

    // 追従カメラを置くだけのアクター。追う相手と、相手が状態を渡す口を試しが渡す
    class VcamOwnerActor final : public NS::SceneActor
    {
    public:
        NS_REFLECT_BEGIN(VcamOwnerActor, NS::SceneActor)
        NS_CLASSOBJ(m_vcam, "Vcam")
        NS_REFLECT_END()

        void Follow(const WalkingTargetActor* target) { m_vcam->SetTarget(target, target); }
        void OnEndPlay() override
        {
            NS::SceneActor::OnEndPlay();
            NS::EndVirtualCamera(this, m_vcam.get());
        }

    protected:
        void OnInit() override
        {
            CreateClassObj(m_vcam);
            NS::InitVirtualCamera(this, m_vcam.get());
        }

    private:
        std::unique_ptr<NS::ThirdPersonFollow> m_vcam;
    };

    // 一覧の先頭に追従カメラ、後ろに進む物を置く。カメラが一覧の順で追うと、進む前の位置を読む
    struct WalkingFollowScene
    {
        NS::Scene scene;
        std::vector<std::string> log;
        VcamOwnerActor* camera = nullptr;
        WalkingTargetActor* target = nullptr;

        WalkingFollowScene()
        {
            camera = scene.SpawnActor<VcamOwnerActor>("VcamOwner");
            target = scene.SpawnActor<WalkingTargetActor>("target", log);
            if (camera != nullptr && target != nullptr)
            {
                camera->Follow(target);
            }
        }
    };
} // namespace

// カメラはアクター全部・物理・判定・コースの後に追う。そのフレームに動いた後の相手を読む
TEST(FollowCamera, ReadsTheTargetAfterTheActorsAndTheCourseMovedItInThatFrame)
{
    WalkingFollowScene world;
    ASSERT_NE(world.camera, nullptr);
    ASSERT_NE(world.target, nullptr);
    CourseMoverSystem mover(world.target, world.log);
    world.scene.AddSystemTick(mover, NS::UpdatePhase::Course);

    world.scene.OnUpdate();

    EXPECT_EQ(world.log, (std::vector<std::string>{"target", "course", "camera"}));
    ASSERT_EQ(world.target->seenPositions.size(), 1u);
    EXPECT_NEAR(world.target->seenPositions[0].x, 1.0f, 1.0e-4f);
    EXPECT_NEAR(world.target->seenPositions[0].z, 5.0f, 1.0e-4f);
    world.scene.RemoveSystemTick(mover);
}

// 止めている間もカメラは止め始めたアクターを追う。止めると、進み続ける自機が画面の中で動いて見える
TEST(FollowCamera, KeepsFollowingTheHolderWhileOthersAreHeld)
{
    WalkingFollowScene world;
    ASSERT_NE(world.camera, nullptr);
    ASSERT_NE(world.target, nullptr);
    world.scene.HoldOthers(world.target, 10);

    for (int frame = 0; frame < 3; ++frame)
    {
        world.scene.OnUpdate();
    }

    ASSERT_TRUE(world.scene.IsHoldingOthers());
    ASSERT_EQ(world.target->seenPositions.size(), 3u);
    EXPECT_NEAR(world.target->seenPositions[2].x, 3.0f, 1.0e-4f);
}

// インスペクタで欄を書いた後に通る道で取得し直す
TEST(ThirdPersonFollow, FollowsTheNewTargetAfterTheInspectorRewritesIt)
{
    FollowScene world;
    ASSERT_NE(world.camera, nullptr);
    ASSERT_TRUE(world.camera->IsAppeared());
    EXPECT_NEAR(world.LookAfterUpdate().x, 0.0f, 1.0e-4f);

    LevelEditorController editor(&world.scene);
    ASSERT_EQ(NS::ApplyJsonFields(&world.camera->Vcam(),
                                  nlohmann::json{{"追従対象", nlohmann::json{{"ref", world.second->Id()}}}}),
              0u);
    editor.FinishFieldWrite(world.camera, NS::FieldType::ActorRef);
    EXPECT_NEAR(world.LookAfterUpdate().x, 10.0f, 1.0e-4f);
}

// 取り消しとやり直しが控えを当てた後も、控えの相手を追う
TEST(ThirdPersonFollow, FollowsTheNewTargetAfterASnapshotRewritesIt)
{
    FollowScene world;
    ASSERT_NE(world.camera, nullptr);
    EXPECT_NEAR(world.LookAfterUpdate().x, 0.0f, 1.0e-4f);

    NS::Editor::ActorSnapshotApplier applier{&world.scene};
    std::optional<nlohmann::json> snapshot = applier.CaptureActor(world.camera->Id());
    ASSERT_TRUE(snapshot.has_value());
    NS::ActorJsonClassObjs(*snapshot)["Vcam"]["追従対象"] = nlohmann::json{{"ref", world.second->Id()}};
    const std::unique_ptr<NS::SceneActor> replaced = applier.ApplyActorSnapshot(world.camera->Id(), *snapshot);
    EXPECT_EQ(replaced, nullptr);
    EXPECT_NEAR(world.LookAfterUpdate().x, 10.0f, 1.0e-4f);
}
