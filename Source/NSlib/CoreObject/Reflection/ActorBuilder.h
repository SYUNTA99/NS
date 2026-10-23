#pragma once

// 配置物が自分を JSON へ書き出し、JSON から組み上がる経路
// 値はコードの既定値 < .actor の値 < 配置物の印の付いた欄の 3 段で重なる。.actor の読み書きは ActorFile.h

#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <memory>

namespace NS
{
    class Scene;
    class Object;

    // 既存の部品へ値を適用する。クラスに無い部品名は警告して読み飛ばす
    void ApplyActorClassObjs(SceneActor& obj, const nlohmann::json& actorJson);

    //! 配置物の指す .actor のクラスで scene に置くアクターを作り、.actor と配置の上書きを書いて素材を読む。
    //! id と名前は設定しない。.actor が見つからなければ ERROR を出して nullptr
    [[nodiscard]] std::unique_ptr<SceneActor> ActorFromJson(const nlohmann::json& actorJson, Scene& scene);

    //! @brief obj が自分を JSON へ書き出す。欄は配置物が持てる欄のうち、obj の .actor の値と違う物だけを書く
    [[nodiscard]] nlohmann::json ActorToJson(const SceneActor& obj);

    //! obj の .actor の名前・名前・出現を、配置物の頭の欄だけを持つ JSON に書く。保存とエディタのひな形が通す
    [[nodiscard]] nlohmann::json MakeActorHeaderJson(const SceneActor& obj, uint32 id);
} // namespace NS
