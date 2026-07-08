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
    /// ObjectRef フィールドの選択肢 1 件。 label は Hierarchy と同じ表示名 + 永続 id
    struct ObjectRefOption
    {
        std::uint32_t id = 0;
        std::string label;
    };

    /// comp の反射フィールドを ImGui widget として描き、1 つでも編集されたら true を返す
    /// 反射情報を持たない Component は何も描かず false。GetReflection() == nullptr で判定する
    /// refOptions は ObjectRef フィールドの参照先候補。 渡すとレベル配置物から選ぶコンボになり、
    /// 空なら永続 id の数値入力に落ちる
    [[nodiscard]] bool DrawReflectedComponent(NS::Scene::Component& comp,
                                              std::span<const ObjectRefOption> refOptions = {}) noexcept;

    /// obj の全 Component を型名見出し付きで描く。反射の無い型は見出しのみで調整値なしと表示する
    /// 反射の無い型名は RTTI から名前空間を剥がして得る。1 つでも編集されたら true を返す
    [[nodiscard]] bool DrawObjectComponents(NS::Scene::GameObject& obj,
                                            std::span<const ObjectRefOption> refOptions = {}) noexcept;
} // namespace NS::Editor
