#pragma once

#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <string_view>

//! テストのアクターを、ゲームと同じく .actor を通してシーンに入れる
//! 新しい永続 id を振り、actorFile の .actor の値に classObjs のシーンの上書きを重ねて作り、開始する
//! .actor が見つからなければ、シーンの読み込みと同じく ERROR を出して nullptr を返す
//! 反映 (NS_REFLECT) の無い型 T は、基底の型までしか見分けない。作った型が T と違えば nullptr
//! @param[in] actorFile 敷く .actor の名前。テストの型の .actor はテストの準備がクラスと同じ名前で書く
//! @param[in] name アクターの名前。空なら .actor の名前から振る
//! @param[in] classObjs 部品名ごとの欄の object。シーンの上書きとして書く
template <class T>
T* SpawnFromActorFile(NS::Scene& scene,
                      std::string_view actorFile,
                      std::string_view name = {},
                      const nlohmann::json& classObjs = nlohmann::json::object())
{
    nlohmann::json actorJson = NS::MakeActorJson();
    NS::SetActorJsonId(actorJson, scene.Actors().AllocateActorId());
    NS::SetActorJsonFile(actorJson, actorFile);
    NS::SetActorJsonName(actorJson, name);
    if (!classObjs.empty())
    {
        NS::ActorJsonClassObjs(actorJson) = classObjs;
    }
    return NS::Cast<T>(scene.SpawnFromJson(actorJson));
}
