#pragma once

#include "NSlib/CoreObject/ObjectJson.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace NS
{
    //! SceneActor 派生を生成する関数
    using ActorCreateFn = std::unique_ptr<SceneActor> (*)();

    //! NS_CLASS で登録した SceneActor をクラス名から生成する
    //! 静的ライブラリでは自己登録の翻訳単位がリンク時に除去されないようにする
    class TypeRegistry
    {
    public:
        [[nodiscard]] static TypeRegistry& Get() noexcept;

        struct Entry
        {
            const char* className; // 保存形式に書くクラス名
            ActorCreateFn create;
            const char* label; // エディタで置ける SceneActor の表示名。置けない型は nullptr
        };

        //! label はエディタで置ける SceneActor にだけ渡す。同名の二重登録は先勝ちで拒否し debug では assert で落とす
        void Register(const char* className, ActorCreateFn create, const char* label = nullptr);

        [[nodiscard]] const std::vector<Entry>& Entries() const noexcept;

        //! className 一致の登録を返す。無ければ nullptr
        [[nodiscard]] const Entry* Find(std::string_view className) const noexcept;

    private:
        TypeRegistry() = default;
        std::vector<Entry> m_entries; // 登録順のまま持つ。型の種類は少数なので線形照合で足りる
    };

    //! クラス名から配置の無い SceneActor を作り、クラスと同じ名前の .actor と素材まで済ませて返す。.actor が無ければ ERROR を出して nullptr
    [[nodiscard]] std::unique_ptr<SceneActor> CreateActorOfClass(std::string_view className);

    //! @brief クラス名から SceneActor を作り、info で .actor・配置の上書き・素材まで済ませて返す
    //! @details info.actorFile が空ならクラスと同じ名前の .actor を使う。.actor が読まれていなければ ERROR を出して nullptr
    [[nodiscard]] std::unique_ptr<SceneActor> CreateActorOfClass(std::string_view className, const ActorInitInfo& info);

    //! @brief クラス名から、.actor を写さずにコンストラクタと OnInit の値だけで SceneActor を作る
    //! @details .actor の全部の項目を生成する時だけ使う。シーンへ入らず、素材も読まない。未登録のクラスは nullptr
    [[nodiscard]] std::unique_ptr<SceneActor> CreateActorWithCodeValues(std::string_view className);

    //! @brief object を配置として scene の中に置くアクターを作り、欄と素材まで済ませて返す
    //! @details クラスは object の指す .actor の中の class。.actor が見つからなければ ERROR を出して nullptr
    [[nodiscard]] std::unique_ptr<SceneActor> CreateRegisteredObject(const nlohmann::json& object, Scene& scene);

    //! @brief エディタで置ける SceneActor の登録の一覧。表示名の順で安定
    //! @details ヒエラルキーの追加メニューとパレットが、置ける種類の列挙に使う
    [[nodiscard]] const std::vector<const TypeRegistry::Entry*>& PlaceableEntries();

    //! NS_CLASS / NS_PLACEABLE の実体。label を渡すのはエディタで置ける SceneActor だけ
    template <class T> void RegisterClass(const char* className, const char* label = nullptr)
    {
        static_assert(std::is_base_of_v<SceneActor, T>, "NS_CLASS は SceneActor の派生に書く");
        TypeRegistry::Get().Register(
            className, +[]() -> std::unique_ptr<SceneActor> { return std::make_unique<T>(); }, label);
    }
} // namespace NS

//! SceneActor の型をクラス名で自己登録する。その型の .cpp で 1 度だけ書く。#Type が保存形式と検索のキーになる
#define NS_CLASS(Type)                                                                                                 \
    namespace                                                                                                          \
    {                                                                                                                  \
        const bool k_classRegistered_##Type = [] {                                                                     \
            ::NS::RegisterClass<Type>(#Type);                                                                          \
            return true;                                                                                               \
        }();                                                                                                           \
    }

//! エディタで置ける SceneActor をクラス名と表示名で自己登録する。NS_CLASS の代わりに、その型の .cpp で 1 度だけ書く
//! 表示名はヒエラルキーの追加メニューとパレットに出る
#define NS_PLACEABLE(Type, Label)                                                                                      \
    namespace                                                                                                          \
    {                                                                                                                  \
        const bool k_classRegistered_##Type = [] {                                                                     \
            ::NS::RegisterClass<Type>(#Type, Label);                                                                   \
            return true;                                                                                               \
        }();                                                                                                           \
    }
