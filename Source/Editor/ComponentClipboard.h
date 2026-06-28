#pragma once

/// @file ComponentClipboard.h
/// @brief runtime Component の現在値を保存形式 ComponentData へ写し取る橋渡し

#include "Game/Level/LevelData.h"

namespace NS::Scene
{
    class Component;
    class GameObject;
} // namespace NS::Scene

namespace NS::Editor
{
    /// comp の反射フィールドを今の値ごと ComponentData へ写す
    /// @details Inspector でライブ編集した値もそのまま拾う。 反射を持たない Component は型名も fields も空のまま返る
    [[nodiscard]] NS::Game::Level::ComponentData CaptureComponentData(const NS::Scene::Component& comp);

    /// runtime の collider component の現在値を object.components の同型データへ書き戻す
    /// @details Inspector でライブ編集した collider の half-extents / offset / 回転を保存と rebuild に乗せる
    /// 書き戻し先は scalar collider フィールドではなく components 側にする。 BuildFromComponents が読むのは
    /// components のためで、 ここを更新しないと編集が次の rebuild で失われる。 collider 以外の component は変えない
    void WriteBackColliderEdits(NS::Scene::GameObject& runtime, NS::Game::Level::ObjectInstance& object);
} // namespace NS::Editor
