#pragma once

/// @file InspectorReflection.h
/// @brief Component の反射情報から ImGui 編集 UI を自動生成する Inspector ヘルパ (Debug / Development 限定)

namespace NS::Scene
{
    class Component;
}

namespace NS::Editor
{
    /// comp の反射フィールドを ImGui widget として描き、1 つでも編集されたら true を返す
    /// 反射情報を持たない Component は何も描かず false (GetReflection() == nullptr)
    [[nodiscard]] bool DrawReflectedComponent(NS::Scene::Component& comp) noexcept;
} // namespace NS::Editor
