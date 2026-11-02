#pragma once

#include "Editor/EditorActors.h"
#include "Game/Level/PropActor.h"
#include "Game/PlayerActor.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "Tests/TestSceneSetup.h"
#include "Tests/TestViewCamera.h"

#include <utility>

//! @brief 自機を原点の上に、置物を (rockX, 0.5, rockZ) に置き、後ろ上の固定のカメラから置物を見る。床の上面は y = 0
//! @details どちらも出荷の .actor から作る。mass が正なら、作った置物の質量へ書く。落ち着くまで回してから返す
inline PlayerActor* PlaceTackleScene(NS::Scene& scene, float rockX, float rockZ = 4.0f, float mass = 0.0f)
{
    nlohmann::json doc = NS::MakeSceneJson();
    AddActorJson(doc, "PlayerActor", 1, NS::Vector3{0.0f, 1.0f, 0.0f});
    AddActorJson(doc, "PropActor", 2, NS::Vector3{rockX, 0.5f, rockZ});
    scene.ResetFromJson(doc);
    // 質量は派生の .actor が持つ項目でシーンには書けないので、作ったアクターへ書く
    GL::Level::PropActor* placed = NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(2));
    if (mass > 0.0f && placed != nullptr)
    {
        (void)NS::ApplyJsonFields(&placed->Params(), nlohmann::json{{"質量", mass}});
    }
    AddTerrainBox(scene, NS::Vector3{0.0f, -0.5f, 0.0f}, 100.0f, 0.5f, 100.0f);
    PlaceViewCamera(scene, NS::Vector3{0.0f, 3.0f, -6.0f}, NS::Vector3{0.0f, 0.5f, rockZ});
    for (int frame = 0; frame < 30; ++frame)
    {
        scene.OnUpdate();
    }
    return NS::Cast<PlayerActor>(scene.Actors().FindByActorId(1));
}

//! PlaceTackleScene が置いた置物
inline GL::Level::PropActor* TackleRockOf(NS::Scene& scene)
{
    return NS::Cast<GL::Level::PropActor>(scene.Actors().FindByActorId(2));
}
