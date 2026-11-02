#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/ThirdPersonFollow.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <gtest/gtest.h>

#include <memory>

// 体当たりから止めの明けまでは、カメラの距離を放した時のまま保つ。当たる瞬間に画面が引くと、
// 揺れを見せたい止めの間に二人が小さく遠ざかる。明けたら距離を普通の追い方へ戻す

namespace
{
    // 接地と保つかを試しが書く追われる物。動かない
    class FramingTargetProbeActor final : public NS::SceneActor
    {
    public:
        // 接地と保つかを仮想カメラの口へ渡す
        void Feed(GL::Level::ThirdPersonFollow* vcam) const
        {
            NS_ASSERT(Game, vcam != nullptr, "Feed: 渡したアクターか部品が無い");
            vcam->SetFollowMotion(grounded, NS::Vector3{});
            vcam->SetFollowFramingHeld(framingHeld);
        }

        bool grounded = true;
        bool framingHeld = false;
    };

    struct FramingScene
    {
        NS::Scene scene;
        FramingTargetProbeActor* target = nullptr;
        GL::Level::FollowCameraActor* camera = nullptr;

        FramingScene()
        {
            target = scene.SpawnActor<FramingTargetProbeActor>("target");
            camera = scene.SpawnActor<GL::Level::FollowCameraActor>("FollowCameraActor");
            NS::ApplyJsonFields(&camera->Vcam(), nlohmann::json{{"追従対象", nlohmann::json{{"ref", target->Id()}}}});
            // 欄を書いた後に、エディタと同じく相手を取得し直す
            camera->ResolveActorRefs();
        }

        void Run(int frames)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                target->Feed(&camera->Vcam());
                scene.GetCameraManager()->OnTick();
            }
        }
    };
} // namespace

// 浮いた突進と止めの間は距離を保ち、明けたら距離は空中の距離へ動く
TEST(FollowImpactFraming, HeldFramingKeepsDistanceUntilTheRelease)
{
    FramingScene world;
    world.Run(120);
    const GL::Level::ThirdPersonFollow& vcam = world.camera->Vcam();
    const float distance = vcam.Distance();

    world.target->grounded = false;
    world.target->framingHeld = true;
    for (int frame = 0; frame < 20; ++frame)
    {
        SCOPED_TRACE(frame);
        world.Run(1);
        EXPECT_FLOAT_EQ(vcam.Distance(), distance);
    }

    world.target->framingHeld = false;
    world.Run(11);
    EXPECT_GT(vcam.Distance(), distance);
}
