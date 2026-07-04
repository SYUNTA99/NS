#pragma once

/// @file ComponentRegistry.h
/// @brief 型名 → コンポーネント factory の自己登録レジストリと登録マクロ
///
/// @details 各 Component は自身の .cpp で NS_REGISTER_COMPONENT を書くと、型名をキーに
/// 生成関数がこのテーブルへ静的初期化時に積まれる。中央の手書き列挙は持たない
/// JSON の type 文字列や Add Component パレットから Component を生成する唯一の経路で、
/// 登録マクロを書いた型しか生成できない。player / editor 専用コンポは登録しないので、
/// 信頼できない type 名でも不正なコンポを生成できない
/// StaticLib では自己登録 TU がリンカに除去され得るため、実行体側で除去対策を要する
/// 依存: NS::Scene::GameObject / Component、 前方宣言のみで各 Component の重いヘッダは露出しない

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NS::Scene
{
    class Component;
    class GameObject;

    /// Component を生成して obj へ attach する関数。戻り値は attach 済みの実体
    using ComponentAttachFn = Component* (*)(GameObject&);

    /// 自己登録の集約先。全 TU の登録が同一実体へ積まれる
    class ComponentRegistry
    {
    public:
        [[nodiscard]] static ComponentRegistry& Get() noexcept;

        /// 型名と生成関数を登録する。同名の二重登録は先勝ちで拒否し debug では assert で落とす
        void Register(const char* name, ComponentAttachFn attach);

        [[nodiscard]] const std::unordered_map<std::string_view, ComponentAttachFn>& Entries() const noexcept;

    private:
        ComponentRegistry() = default;
        std::unordered_map<std::string_view, ComponentAttachFn> m_entries;
    };

    /// 型名から登録済みコンポを生成し obj へ attach する。未登録型は何もせず nullptr を返す
    [[nodiscard]] Component* CreateComponent(std::string_view typeName, GameObject& obj);

    /// 型名が登録済みか
    [[nodiscard]] bool IsRegistered(std::string_view typeName) noexcept;

    /// 登録済み型名の一覧。名前順で安定。Add Component パレットが選択肢の列挙に使う
    [[nodiscard]] const std::vector<std::string>& RegisteredNames();
} // namespace NS::Scene

/// Component 型を自己登録する。その型の .cpp の namespace NS::Scene 内で 1 度だけ書く
/// 可変引数は AddComponent へ渡す構築引数。#Type が検索キーになるので反射 typeName と一致する
/// 展開先が GameObject.h を include している必要がある
#define NS_REGISTER_COMPONENT(Type, ...)                                                                               \
    namespace                                                                                                          \
    {                                                                                                                  \
        const bool k_componentRegistered_##Type = [] {                                                                 \
            ::NS::Scene::ComponentRegistry::Get().Register(                                                            \
                #Type, +[](::NS::Scene::GameObject& o) -> ::NS::Scene::Component* {                                    \
                    return o.AddComponent<Type>(__VA_ARGS__);                                                          \
                });                                                                                                    \
            return true;                                                                                               \
        }();                                                                                                           \
    }
