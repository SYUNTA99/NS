#pragma once

// 配置物が自分を JSON へ書き出し、JSON から組み上がる経路
// 値はコードの既定値 < .actor の値 < 配置物の印の付いた欄の 3 段で重なる。.actor の読み書きは ActorFile.h

#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <memory>
#include <type_traits>

namespace NS
{
    class Scene;
    class Object;

    // 既存の部品へ値を適用する。クラスに無い部品名は警告して読み飛ばす
    void ApplyObjectClassObjs(SceneActor& obj, const nlohmann::json& object);

    //! 配置物の指す .actor のクラスで scene に置くアクターを作り、.actor と配置の上書きを書いて素材を読む。
    //! id と名前は設定しない。.actor が見つからなければ ERROR を出して nullptr
    [[nodiscard]] std::unique_ptr<SceneActor> ObjectFromJson(const nlohmann::json& object, Scene& scene);

    //! @brief obj が自分を JSON へ書き出す。欄は配置物が持てる欄のうち、obj の .actor の値と違う物だけを書く
    [[nodiscard]] nlohmann::json ObjectToJson(const SceneActor& obj);

    //! @details transform 以外の値は写さずコード既定に任せる疎な写し。id は 0 で、置く時に振る
    [[nodiscard]] nlohmann::json MakePrototypeJson(const SceneActor& obj);

    //! @brief SceneActor 派生 T の OnInit が積む構成からひな形の JSON を作る
    //! @details 派生クラスの構成を手書きレシピへ並記せず、クラス自身を構成と既定値の唯一の出所にする
    template <class T> [[nodiscard]] nlohmann::json MakePrototypeJson()
    {
        static_assert(std::is_base_of_v<SceneActor, T>, "T は SceneActor 派生でなければならない");
        const std::unique_ptr<T> prototype = CreateActor<T>();
        return MakePrototypeJson(*prototype);
    }
} // namespace NS
