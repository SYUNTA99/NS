#pragma once

/// @file InspectorReflection.h
/// @brief Component の反射情報から ImGui 編集 UI を自動生成する Inspector ヘルパ。Debug / Development 限定

namespace NS::Scene
{
    class Component;
    class GameObject;
} // namespace NS::Scene

namespace NS::Editor
{
    /// comp の反射フィールドを ImGui widget として描き、1 つでも編集されたら true を返す
    /// 反射情報を持たない Component は何も描かず false。GetReflection() == nullptr で判定する
    [[nodiscard]] bool DrawReflectedComponent(NS::Scene::Component& comp) noexcept;

    /// obj の全 Component を型名見出し付きで描く。反射の無い型は見出しのみで調整値なしと表示する
    /// 反射の無い型名は RTTI から名前空間を剥がして得る。1 つでも編集されたら true を返す
    [[nodiscard]] bool DrawObjectComponents(NS::Scene::GameObject& obj) noexcept;
} // namespace NS::Editor
