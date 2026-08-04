#pragma once

#include "Runtime/Object/GameObject.h"

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace NS::Object
{
    struct ObjectData;

    /// GameObject 派生の器を生成する関数
    using GameObjectCreateFn = std::unique_ptr<GameObject> (*)();

    /// Component を生成して obj へ attach する関数。戻り値は attach した Component
    using ComponentAttachFn = Component* (*)(GameObject&);

    /// @brief クラス名から型を引く自己登録の集約先。器 (GameObject 派生) と中身 (Component) を 1 表で持つ
    /// @details 各型は自身の .cpp で NS_CLASS を書くと、クラス名をキーに生成関数が静的初期化時に積まれる
    /// 器か中身かは NS_CLASS が継承で見分ける。中央の手書き列挙は持たない
    /// 保存は ObjectData.className にクラス名を書き、読込は CreateRegisteredObject / CreateComponent がここから引く
    /// 登録マクロを書いた型しか生成できないので、信頼できない型名でも不正な生成はできない
    /// editor 専用コンポと抽象基底は登録しない
    /// StaticLib では自己登録の翻訳単位がリンカに除去され得るため、実行体側で除去対策を要する
    class TypeRegistry
    {
    public:
        [[nodiscard]] static TypeRegistry& Get() noexcept;

        struct Entry
        {
            const char* className;     // 保存形式に書くクラス名
            GameObjectCreateFn create; // 器の生成関数。中身の登録は nullptr
            ComponentAttachFn attach;  // 中身の生成関数。器の登録は nullptr
        };

        /// クラス名と生成関数を登録する。create / attach はどちらか片方だけ渡す
        /// 同名の二重登録は先勝ちで拒否し debug では assert で落とす
        void Register(const char* className, GameObjectCreateFn create, ComponentAttachFn attach);

        [[nodiscard]] const std::vector<Entry>& Entries() const noexcept;

        /// className 一致の登録を返す。無ければ nullptr
        [[nodiscard]] const Entry* Find(std::string_view className) const noexcept;

    private:
        TypeRegistry() = default;
        std::vector<Entry> m_entries; // 登録順のまま持つ。型の種類は少数なので線形照合で足りる
    };

    /// object の器を作る。className 一致の器登録があればその工場、該当しなければ素の GameObject を返す
    [[nodiscard]] std::unique_ptr<GameObject> CreateRegisteredObject(const ObjectData& object);

    /// 型名から登録済みコンポを生成し obj へ attach する。未登録型は何もせず nullptr を返す
    [[nodiscard]] Component* CreateComponent(std::string_view typeName, GameObject& obj);

    /// 型名が Component として登録済みか
    [[nodiscard]] bool IsRegistered(std::string_view typeName) noexcept;

    /// 登録済み Component 型名の一覧。名前順で安定。Add Component パレットが選択肢の列挙に使う
    [[nodiscard]] const std::vector<std::string>& RegisteredNames();

    /// NS_CLASS の実体。T の継承で器か中身かを見分けて登録する
    template <class T> void RegisterClass(const char* className)
    {
        if constexpr (std::is_base_of_v<Component, T>)
        {
            TypeRegistry::Get().Register(
                className, nullptr, +[](GameObject& o) -> Component* { return o.AddComponent<T>(); });
        }
        else
        {
            static_assert(std::is_base_of_v<GameObject, T>, "NS_CLASS は GameObject か Component の派生に書く");
            TypeRegistry::Get().Register(
                className, +[]() -> std::unique_ptr<GameObject> { return std::make_unique<T>(); }, nullptr);
        }
    }
} // namespace NS::Object

/// 型をクラス名で自己登録する。その型の .cpp で 1 度だけ書く。#Type が保存形式と検索のキーになる
/// Component は既定コンストラクタで生成されるので、値は反射 field と ResolveAssets で後から入れる
#define NS_CLASS(Type)                                                                                                 \
    namespace                                                                                                          \
    {                                                                                                                  \
        const bool k_classRegistered_##Type = [] {                                                                     \
            ::NS::Object::RegisterClass<Type>(#Type);                                                                 \
            return true;                                                                                               \
        }();                                                                                                           \
    }
