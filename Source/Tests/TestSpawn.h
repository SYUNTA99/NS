#pragma once

#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <string_view>

//! 部品も振る舞いも持たないアクター。止めを始める物や、IGet〜 の関数を呼ぶ元にだけ使う
//! 登録は TestActorFileEnvironment.cpp の NS_CLASS
class TestEmptyActor final : public NS::SceneActor
{
public:
    NS_REFLECT_NONE(TestEmptyActor, NS::SceneActor)
};

//! 立方体のモデルを player.mat で描くだけのアクター。描いた絵や影を見るテストに使う
//! 登録は TestActorFileEnvironment.cpp の NS_CLASS
class TestBoxActor final : public NS::SceneActor
{
public:
    NS_REFLECT_NONE(TestBoxActor, NS::SceneActor)

    //! 描くモデル。OnInit の前は nullptr
    [[nodiscard]] NS::Model* Body() const noexcept { return m_body; }

protected:
    void OnInit() override
    {
        m_body = CreateClassObj(m_model);
        m_body->SetMeshRef("cube");
        m_body->SetMaterialRef("Assets/Materials/player.mat");
    }

private:
    NS::Model* m_body = nullptr;
};

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
