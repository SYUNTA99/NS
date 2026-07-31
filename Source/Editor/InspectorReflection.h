#pragma once

//! @brief コンポーネントのリフレクション情報から、ImGui編集用UIを自動生成するインスペクタヘルパー

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Object/Reflection/Reflection.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS::Object
{
    class Component;
    class GameObject;
} // namespace NS::Object

namespace NS::Editor
{
    //! @brief ObjectRefフィールドで参照可能なオブジェクトの選択肢
    struct ObjectRefOption
    {
        std::uint32_t id = 0; // 参照先の永続ID
        std::string label;    // UI表示用のラベル
    };

    //! @brief 型ごとの既定インスタンスを控える器
    //! @details 反射欄の「既定と違う」印と戻すボタンが、 今の値と比べる相手として引く
    //! 控えは 1 体の器へまとめて attach するだけで、 world に入らないので更新も描画も走らない
    class ComponentDefaults : public NS::Core::NonCopyable
    {
    public:
        ComponentDefaults() noexcept;
        ~ComponentDefaults() noexcept;

        //! @brief typeName の既定インスタンスを返す
        //! @details 初回だけ作って以降は使い回す。 未登録の型は nullptr
        [[nodiscard]] const NS::Object::Component* Find(std::string_view typeName);

    private:
        std::unique_ptr<NS::Object::GameObject> m_holder;                     // 既定インスタンスの持ち主
        std::vector<std::pair<std::string, NS::Object::Component*>> m_byType; // 型名から引く索引
    };

    //! @brief 1フレームのコンポーネント編集で起きた相互作用の集約
    //! @details activated/committed はドラッグを1つのundo単位へまとめるための開始・終了フレームの印
    struct ComponentEditResult
    {
        bool changed = false;   // いずれかの値が編集された
        bool activated = false; // いずれかのウィジェットで編集が始まった (ドラッグ開始フレーム)
        bool committed = false; // いずれかのウィジェットが非活性化した (編集の有無は問わない)

        // 既定へ戻す要求。 ボタンが押されたフレームだけ対象と欄が入る
        // 値をこの場で書くと undo の控えを取る前に live が動くので、 適用は呼び出し側へ預ける
        NS::Object::Component* revertTarget = nullptr;
        const NS::Object::FieldDesc* revertField = nullptr;
    };

    //! @brief 反射欄 1 つの値が既定と違うか
    //! @param defaults 既定インスタンス。 nullptr なら常に false
    [[nodiscard]] bool FieldDiffersFromDefault(const NS::Object::Component& comp,
                                               const NS::Object::Component* defaults,
                                               const NS::Object::FieldDesc& field) noexcept;

    //! @brief 反射欄 1 つを既定値へ戻す
    void RevertFieldToDefault(NS::Object::Component& comp,
                              const NS::Object::Component& defaults,
                              const NS::Object::FieldDesc& field) noexcept;

    //! @brief コンポーネントのフィールドをImGuiウィジェットとして描画する
    //! @param comp 編集対象のコンポーネント
    //! @param refOptions 参照先候補のリスト。指定しない場合は数値入力となる
    //! @param defaults 既定インスタンス。 渡すと既定と違う欄に印と戻すボタンが付く
    //! @return 値の編集有無と、編集の開始・確定フレームを集約した結果
    [[nodiscard]] ComponentEditResult DrawReflectedComponent(NS::Object::Component& comp,
                                                             std::span<const ObjectRefOption> refOptions = {},
                                                             const NS::Object::Component* defaults = nullptr) noexcept;

    //! @brief ゲームオブジェクトが持つ全コンポーネントの編集UIを描画する
    //! @param obj 編集対象のゲームオブジェクト
    //! @param refOptions 参照先候補のリスト
    //! @param defaults 既定インスタンスの控え。 渡すと既定と違う欄に印と戻すボタンが付く
    //! @return 全コンポーネント分を集約した編集結果
    [[nodiscard]] ComponentEditResult DrawObjectComponents(NS::Object::GameObject& obj,
                                                           std::span<const ObjectRefOption> refOptions = {},
                                                           ComponentDefaults* defaults = nullptr) noexcept;
} // namespace NS::Editor
