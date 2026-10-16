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

    using SubObjectDefaultFn = std::unique_ptr<SubObject> (*)();

    //! NS_CLASS で登録した SceneActor と部品をクラス名から生成する
    //! 静的ライブラリでは自己登録の翻訳単位がリンク時に除去されないようにする
    class TypeRegistry
    {
    public:
        [[nodiscard]] static TypeRegistry& Get() noexcept;

        struct Entry
        {
            const char* className;            // 保存形式に書くクラス名
            ActorCreateFn create;             // SceneActor 側の生成関数。SubObject 側の登録は nullptr
            SubObjectDefaultFn createDefault; // SubObject 側の生成関数。SceneActor 側の登録は nullptr
            const char* label;                // エディタで置ける SceneActor の表示名。置けない型は nullptr
        };

        //! label はエディタで置ける SceneActor にだけ渡す。同名の二重登録は先勝ちで拒否し debug では assert で落とす
        void Register(const char* className,
                      ActorCreateFn create,
                      SubObjectDefaultFn attach,
                      const char* label = nullptr);

        [[nodiscard]] const std::vector<Entry>& Entries() const noexcept;

        //! className 一致の登録を返す。無ければ nullptr
        [[nodiscard]] const Entry* Find(std::string_view className) const noexcept;

    private:
        TypeRegistry() = default;
        std::vector<Entry> m_entries; // 登録順のまま持つ。型の種類は少数なので線形照合で足りる
    };

    //! クラス名から SceneActor をコードの既定値で作り、Init を済ませて返す。未登録のクラスと空の名前は素の SceneActor
    [[nodiscard]] std::unique_ptr<SceneActor> CreateActorOfClass(std::string_view className);

    //! クラス名から SceneActor を作り、info で種類の既定・配置の上書き・素材まで済ませて返す
    [[nodiscard]] std::unique_ptr<SceneActor> CreateActorOfClass(std::string_view className, const ActorInitInfo& info);

    //! object の SceneActor を作る。className 一致の登録があればその生成関数、該当しなければ素の SceneActor を返す
    [[nodiscard]] std::unique_ptr<SceneActor> CreateRegisteredObject(const nlohmann::json& object);

    //! object を配置として scene の中に置くアクターを作り、欄と素材まで済ませて返す
    [[nodiscard]] std::unique_ptr<SceneActor> CreateRegisteredObject(const nlohmann::json& object, Scene& scene);

    //! 型名から部品をコードの既定値で作る。持ち主と名前は付かない。未登録の型名と SceneActor の型名は nullptr
    [[nodiscard]] std::unique_ptr<SubObject> CreateSubObjOfType(std::string_view typeName);

    //! @brief エディタで置ける SceneActor の登録の一覧。表示名の順で安定
    //! @details ヒエラルキーの追加メニューとパレットが、置ける種類の列挙に使う
    [[nodiscard]] const std::vector<const TypeRegistry::Entry*>& PlaceableEntries();

    //! NS_CLASS / NS_PLACEABLE の実体。T の継承で SceneActor 側か SubObject 側かを見分けて登録する
    //! label を渡すのはエディタで置ける SceneActor だけ
    template <class T> void RegisterClass(const char* className, const char* label = nullptr)
    {
        if constexpr (std::is_base_of_v<SubObject, T>)
        {
            TypeRegistry::Get().Register(
                className, nullptr, +[]() -> std::unique_ptr<SubObject> { return std::make_unique<T>(); });
        }
        else
        {
            static_assert(std::is_base_of_v<SceneActor, T>, "NS_CLASS は SceneActor か SubObject の派生に書く");
            TypeRegistry::Get().Register(
                className, +[]() -> std::unique_ptr<SceneActor> { return std::make_unique<T>(); }, nullptr, label);
        }
    }
} // namespace NS

//! 型をクラス名で自己登録する。その型の .cpp で 1 度だけ書く。#Type が保存形式と検索のキーになる
//! SubObject は既定コンストラクタで生成されるので、値はリフレクション field と参照の欄で後から入れる
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
