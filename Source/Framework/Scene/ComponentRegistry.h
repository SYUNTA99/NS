#pragma once

/// @file ComponentRegistry.h
/// @brief 型名 → curated コンポーネント factory の手書きレジストリ
///
/// @details JSON の type 文字列や Add Component パレットから、 curated subset の Component を
/// GameObject へ生成・attach する唯一の経路。 AddComponent<T> はコンパイル時テンプレートで
/// 文字列から型を作れないため、 curated 型ごとに lambda factory を 1 行ずつ手書き登録する
/// player / editor / camera 専用コンポは allowlist に載せないので、 信頼できない type 名でも
/// 不正なコンポを生成できない
/// 依存: NS::Scene::GameObject / Component (前方宣言のみ、 各 Component の重いヘッダは露出しない)

#include <string>
#include <string_view>
#include <vector>

namespace NS::Scene
{
    class Component;
    class GameObject;

    /// 型名から curated コンポを生成し obj へ attach する。 除外型 / 未知型は何もせず nullptr を返す
    [[nodiscard]] Component* CreateComponent(std::string_view typeName, GameObject& obj);

    /// 型名が curated 登録済みか
    [[nodiscard]] bool IsRegistered(std::string_view typeName) noexcept;

    /// 登録済み型名の一覧。 Add Component パレットが選択肢の列挙に使う
    [[nodiscard]] const std::vector<std::string>& RegisteredNames();
} // namespace NS::Scene
