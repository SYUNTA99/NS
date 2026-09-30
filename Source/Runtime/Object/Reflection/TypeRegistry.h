#pragma once

#include "Runtime/Object/Actor.h"
#include "Runtime/Object/ObjectJson.h"

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace NS::Obj
{
    //! Actor 派生を生成する関数
    using ActorCreateFn = std::unique_ptr<Actor> (*)();

    //! Component を生成して obj へ attach する関数。戻り値は attach した Component
    using ComponentAttachFn = Component* (*)(Actor&);

    //! @brief クラス名から型を引く自己登録の集約先。Actor 派生と Component を 1 表で持つ
    //! @details 各型は自身の .cpp で NS_CLASS を書くと、クラス名をキーに生成関数が静的初期化時に積まれる
    //! Actor 側か Component 側かは NS_CLASS が継承で見分ける。中央の手書き列挙は持たない
    //! 保存は配置物の JSON の class にクラス名を書き、読込は CreateRegisteredObject / CreateComponent がここから引く
    //! 登録マクロを書いた型しか生成できないので、信頼できない型名でも不正な生成はできない
    //! editor 専用コンポと抽象基底は登録しない
    //! StaticLib では自己登録の翻訳単位がリンカに除去され得るため、実行体側で除去対策を要する
    class TypeRegistry
    {
    public:
        [[nodiscard]] static TypeRegistry& Get() noexcept;

        struct Entry
        {
            const char* className;    // 保存形式に書くクラス名
            ActorCreateFn create;     // Actor 側の生成関数。Component 側の登録は nullptr
            ComponentAttachFn attach; // Component 側の生成関数。Actor 側の登録は nullptr
            const char* label;        // エディタで置ける Actor の表示名。置けない型は nullptr
        };

        //! クラス名と生成関数を登録する。create / attach はどちらか片方だけ渡す
        //! label はエディタで置ける Actor にだけ渡す。同名の二重登録は先勝ちで拒否し debug では assert で落とす
        void Register(const char* className, ActorCreateFn create, ComponentAttachFn attach, const char* label = nullptr);

        [[nodiscard]] const std::vector<Entry>& Entries() const noexcept;

        //! className 一致の登録を返す。無ければ nullptr
        [[nodiscard]] const Entry* Find(std::string_view className) const noexcept;

    private:
        TypeRegistry() = default;
        std::vector<Entry> m_entries; // 登録順のまま持つ。型の種類は少数なので線形照合で足りる
    };

    //! object の Actor を作る。className 一致の登録があればその生成関数、該当しなければ素の Actor を返す
    [[nodiscard]] std::unique_ptr<Actor> CreateRegisteredObject(const nlohmann::json& object);

    //! 型名から登録済みコンポを生成し obj へ attach する。未登録型は何もせず nullptr を返す
    [[nodiscard]] Component* CreateComponent(std::string_view typeName, Actor& obj);

    //! @brief エディタで置ける Actor の登録の一覧。表示名の順で安定
    //! @details ヒエラルキーの追加メニューとパレットが、置ける種類の列挙に使う
    [[nodiscard]] const std::vector<const TypeRegistry::Entry*>& PlaceableEntries();

    //! NS_CLASS / NS_PLACEABLE の実体。T の継承で Actor 側か Component 側かを見分けて登録する
    //! label を渡すのはエディタで置ける Actor だけ
    template <class T> void RegisterClass(const char* className, const char* label = nullptr)
    {
        if constexpr (std::is_base_of_v<Component, T>)
        {
            TypeRegistry::Get().Register(className, nullptr, +[](Actor& o) -> Component* { return o.AddComponent<T>(); });
        }
        else
        {
            static_assert(std::is_base_of_v<Actor, T>, "NS_CLASS は Actor か Component の派生に書く");
            TypeRegistry::Get().Register(
                className, +[]() -> std::unique_ptr<Actor> { return std::make_unique<T>(); }, nullptr, label);
        }
    }
} // namespace NS::Obj

//! 型をクラス名で自己登録する。その型の .cpp で 1 度だけ書く。#Type が保存形式と検索のキーになる
//! Component は既定コンストラクタで生成されるので、値はリフレクション field と ResolveAssets で後から入れる
#define NS_CLASS(Type)                                                                                                 \
    namespace                                                                                                          \
    {                                                                                                                  \
        const bool k_classRegistered_##Type = [] {                                                                     \
            ::NS::Obj::RegisterClass<Type>(#Type);                                                                     \
            return true;                                                                                               \
        }();                                                                                                           \
    }

//! エディタで置ける Actor をクラス名と表示名で自己登録する。NS_CLASS の代わりに、その型の .cpp で 1 度だけ書く
//! 表示名はヒエラルキーの追加メニューとパレットに出る
#define NS_PLACEABLE(Type, Label)                                                                                      \
    namespace                                                                                                          \
    {                                                                                                                  \
        const bool k_classRegistered_##Type = [] {                                                                     \
            ::NS::Obj::RegisterClass<Type>(#Type, Label);                                                              \
            return true;                                                                                               \
        }();                                                                                                           \
    }
