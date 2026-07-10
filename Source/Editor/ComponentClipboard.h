#pragma once

/// @file ComponentClipboard.h
/// @brief 実行中の Component の現在値を保存形式 ComponentData へ写し取る

#include "Framework/Scene/SceneData.h"

namespace NS::Scene
{
    class Component;
    class GameObject;
} // namespace NS::Scene

namespace NS::Editor
{
    /// comp の反射フィールドを今の値ごと ComponentData へ写す
    /// @details インスペクタで編集中の値も拾う。 反射を持たない Component は型名も fields も空で返る
    [[nodiscard]] NS::Scene::ComponentData CaptureComponentData(const NS::Scene::Component& comp);

    /// runtime の全コンポーネントの現在値を object.components の同型データへ書き戻す
    /// @details インスペクタで編集中の反射フィールドを保存と再構築に乗せる。 書き戻し先を components 側に
    /// するのは BuildFromComponents が読むのが components だからで、 ここを更新しないと次の再構築で編集が
    /// 失われる。 同型が無いコンポーネントは据え置く
    void WriteBackComponentEdits(NS::Scene::GameObject& runtime, NS::Scene::ObjectData& object);
} // namespace NS::Editor
