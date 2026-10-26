#include "Editor/EditorActors.h"
#include "Editor/LevelEditorController.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Core/Windows/Input.h"
#include "NSlib/Core/Windows/InputReader.h"
#include "NSlib/Core/Windows/Keyboard.h"

#include <gtest/gtest.h>

// エディタが世界を動かし直す時 (再開・コマ送り) に、止めている間の押しを自機へ渡さず、
// エディタの読み手の押しは残す事を縛る。ツールバーは描画の中から呼ぶので、エディタがまだ読んでいない押しがある

namespace
{
    void TapKey(NS::OS::Key key)
    {
        NS::OS::Input& input = NS::OS::Input::Get();
        input.Keyboard().OnKeyDown(key);
        input.Keyboard().OnKeyUp(key);
    }

    nlohmann::json SceneWithPlayer()
    {
        nlohmann::json doc = NS::MakeSceneJson();
        NS::SceneJsonActors(doc).push_back(NS::Editor::MakePrototypeJson<PlayerActor>());
        NS::EnsureUniqueActorIds(doc);
        return doc;
    }

    // 本番の DecideStep の頭と同じく、自機の部品に機器を読ませる
    bool PlayerReadsJump(PlayerActor* player)
    {
        player->Input().Update(player);
        const bool pressed = player->Input().JumpPressed();
        player->Input().ConsumePressed();
        return pressed;
    }
} // namespace

TEST(EditorPlayResume, ResumeDropsPausedPressesForThePlayerOnly)
{
    NS::Scene scene;
    scene.ResetFromJson(SceneWithPlayer());
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    EXPECT_FALSE(PlayerReadsJump(player));

    editor.TogglePlayPause();
    ASSERT_TRUE(editor.PlayPaused());
    NS::OS::InputReader editorSide;
    TapKey(NS::OS::Key::Space);
    editor.TogglePlayPause();
    ASSERT_FALSE(editor.PlayPaused());

    EXPECT_FALSE(PlayerReadsJump(player));
    EXPECT_TRUE(editorSide.Read().WasPressed(NS::OS::Key::Space));

    // 動かし直した後の押しは自機へ届く
    TapKey(NS::OS::Key::Space);
    EXPECT_TRUE(PlayerReadsJump(player));
    editor.EnterEdit();
}

TEST(EditorPlayResume, StepFrameDropsPausedPressesForThePlayerOnly)
{
    NS::Scene scene;
    scene.ResetFromJson(SceneWithPlayer());
    LevelEditorController editor(&scene);
    editor.EnterPlay();
    PlayerActor* player = FindPlayer(scene.Actors());
    ASSERT_NE(player, nullptr);
    EXPECT_FALSE(PlayerReadsJump(player));

    editor.TogglePlayPause();
    NS::OS::InputReader editorSide;
    TapKey(NS::OS::Key::Space);
    editor.RequestStepFrame();

    EXPECT_FALSE(PlayerReadsJump(player));
    EXPECT_TRUE(editorSide.Read().WasPressed(NS::OS::Key::Space));
    editor.EnterEdit();
}
