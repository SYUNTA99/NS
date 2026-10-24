#include "Editor/LevelEditorController.h"
#include "Editor/Undo/ActorSnapshotApplier.h"
#include "Game/Level/FollowCameraActor.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ClassObjs/ThirdPersonFollow.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <gtest/gtest.h>

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
            camera->Update();
            return camera->Vcam().EvaluatePose(1.0f).target;
        }
    };
} // namespace

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
