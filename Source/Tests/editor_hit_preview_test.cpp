#include "Editor/ActionTimelineEdit.h"
#include "Editor/EditorActors.h"
#include "Editor/HitPreview.h"
#include "Editor/TimelinePreview.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/LevelMessages.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Action/ActorAction.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/PlayerInput.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Core/Windows/FileSystem.h"
#include "NSlib/Core/Windows/Input.h"
#include "Tests/TestActorFile.h"

#include <gtest/gtest.h>

#include "NSlib/Core/Types.h"
#include "Tests/TestSceneSetup.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// エディタの下見が、選んだ相手の居ない場面を断る事を縛る。体当たりが無いので、下見は相手に当たらない

namespace
{
    using namespace GL::Level;

    constexpr NS::uint32 k_RockId = 2;

    // 床 (id 100)・自機 (id 1)・その前の置物 (id 2) の場面の文書
    // 置物は半径 1.5 m の球で、床に乗った自機の玉が面の真ん中より下まで届く大きさにする
    nlohmann::json MakePreviewSceneJson()
    {
        nlohmann::json doc = NS::MakeSceneJson();
        AddActorJson(doc, "MapPartsActor", 100, NS::Vector3{0.0f, -0.5f, 0.0f}, NS::Vector3{60.0f, 1.0f, 60.0f});
        AddActorJson(doc, "PlayerActor", 1, NS::Vector3{0.0f, 1.0f, 0.0f});
        AddActorJson(doc, "PropActor", k_RockId, NS::Vector3{0.0f, 1.5f, 7.0f}, NS::Vector3{3.0f, 3.0f, 3.0f});
        return doc;
    }

    NS::Editor::HitPreviewDesc MakeDesc(float faceU, float faceV)
    {
        NS::Editor::HitPreviewDesc desc;
        desc.targetId = k_RockId;
        desc.faceU = faceU;
        desc.faceV = faceV;
        desc.charge01 = 1.0f;
        return desc;
    }
} // namespace

// 選んだ相手が場面に居ない時は、当てずにエラーを返す
TEST(EditorHitPreview, MissingTargetIsAnError)
{
    NS::Editor::HitPreviewDesc desc = MakeDesc(0.0f, 0.0f);
    desc.targetId = 999;
    const NS::Editor::HitPreviewResult result = NS::Editor::RunHitPreview(MakePreviewSceneJson(), desc);
    EXPECT_FALSE(result.hit);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.frames.empty());
}
