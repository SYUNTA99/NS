#pragma once

// 配置物が自分を JSON へ書き出し、JSON から組み上がる経路
// 値はコードの既定値 < 種類の既定値 < 個体の上書きの 3 段で重なる。種類の既定値は Archetype.h

#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <memory>

namespace NS
{
    class Scene;
    class Object;

    // 既存の部品へ値を適用する。クラスに無い部品名は警告して読み飛ばす
    void ApplyObjectClassObjs(SceneActor& obj, const nlohmann::json& object);

    //! 型登録から scene に置くアクターを作り、種類の既定値と配置の上書きを書いて素材を読む。id と名前は設定しない
    //! class がなければ nullptr
    [[nodiscard]] std::unique_ptr<SceneActor> ObjectFromJson(const nlohmann::json& object, Scene& scene);

    //! @brief obj が自分を JSON へ書き出す。欄はクラスの種類の既定値と違う物 (個体の上書き) だけを書く
    [[nodiscard]] nlohmann::json ObjectToJson(const SceneActor& obj);

    //! obj のクラス名・名前・出現を、配置物の頭の欄だけを持つ JSON に書く。保存とエディタのひな形が通す
    [[nodiscard]] nlohmann::json MakeObjectHeaderJson(const SceneActor& obj, uint32 id);
} // namespace NS
