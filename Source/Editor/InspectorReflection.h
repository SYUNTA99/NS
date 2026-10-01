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

namespace NS::Obj
{
    class Component;
    class Actor;
} // namespace NS::Obj

namespace NS::Editor
{
    //! @brief ActorRef の欄で参照可能なオブジェクトの選択肢
    struct ObjectRefOption
    {
        std::uint32_t id = 0; // 参照先の永続ID
        std::string label;    // UI表示用のラベル
    };

    //! @brief ComponentRef フィールドで参照可能な Component の選択肢
    //! @details 描く側が欄の型で絞るので、候補には全配置物の Component を入れてよい
    struct ComponentRefOption
    {
        std::uint32_t object = 0; // 持ち主の配置物の永続 ID
        std::string partName;
        const NS::Obj::Component* target = nullptr; // 型の照合に使う実体。そのフレームの間だけ有効
        std::string label;                          // UI表示用のラベル
    };

    //! @brief 部品ごとの既定の値を引く置き場
    //! @details リフレクション欄の上書きの印と戻すボタンが、今の値と比べる相手として引く
    //! 持ち主のクラスの既定の 1 体 (コードの既定値に種類の既定値を当てた物) の同じ部品が相手になる
    class ComponentDefaults : public NS::Core::NonCopyable
    {
    public:
        ComponentDefaults() noexcept;
        ~ComponentDefaults() noexcept;

        //! @brief comp と比べる既定の部品を返す
        //! @details 種類の既定値を変えると指す先が作り直されるので、そのフレームの間だけ使う。見つからなければ nullptr
        [[nodiscard]] const NS::Obj::Component* Find(const NS::Obj::Component& comp);

    private:
        // typeName のコードの既定値の 1 個。初回だけ作って以降は使い回す。未登録の型は nullptr
        [[nodiscard]] const NS::Obj::Component* FindTypeDefault(std::string_view typeName);

        std::vector<std::pair<std::string, std::unique_ptr<NS::Obj::Component>>> m_byType;
    };

    //! @brief 1フレームのコンポーネント編集で起きた相互作用の集約
    //! @details activated/committed はドラッグを1つのundo単位へまとめるための開始・終了フレームの印
    struct ComponentEditResult
    {
        bool changed = false;   // いずれかの値が編集された
        bool activated = false; // いずれかのウィジェットで編集が始まった (ドラッグ開始フレーム)
        bool committed = false; // いずれかのウィジェットが非活性化した (編集の有無は問わない)

        // 既定へ戻す要求。ボタンが押されたフレームだけ対象と欄が入る
        // 値をこの場で書くと undo の控えを取る前に live が動くので、適用は呼び出し側へ預ける
        NS::Obj::Component* revertTarget = nullptr;
        const NS::Obj::FieldDesc* revertField = nullptr;

        // 種類の既定にする要求。選ばれたフレームだけ対象と欄が入る
        // 同じ種類の他の個体とファイルも書き換えるので、適用は呼び出し側へ預ける
        NS::Obj::Component* promoteTarget = nullptr;
        const NS::Obj::FieldDesc* promoteField = nullptr;

        // 値が編集された対象と欄。編集が起きたフレームだけ入る
        // 凍結スナップショットへの写しが欄単位で要るので、changed の集約とは別に持つ
        NS::Obj::Component* changedTarget = nullptr;
        const NS::Obj::FieldDesc* changedField = nullptr;
    };

    //! @brief リフレクション欄 1 つの値が既定と違うか
    //! @param[in] defaults 既定インスタンス。nullptr なら常に false
    [[nodiscard]] bool FieldDiffersFromDefault(const NS::Obj::Component& comp,
                                               const NS::Obj::Component* defaults,
                                               const NS::Obj::FieldDesc& field) noexcept;

    //! @brief リフレクション欄 1 つを既定値へ戻す
    void RevertFieldToDefault(NS::Obj::Component& comp,
                              const NS::Obj::Component& defaults,
                              const NS::Obj::FieldDesc& field) noexcept;

    //! @brief コンポーネントのフィールドをImGuiウィジェットとして描画する
    //! @param[in,out] comp 編集対象のコンポーネント
    //! @param[in] refOptions 参照先候補のリスト。指定しない場合は数値入力となる
    //! @param[in] defaults 既定の部品。渡すと既定と違う欄に上書きの印が付き、戻すか種類の既定にするかを選べる
    //! @param[in] componentOptions ComponentRef の参照先候補。欄の型に合う物だけを出す
    //! @return 値の編集有無と、編集の開始・確定フレームを集約した結果
    [[nodiscard]] ComponentEditResult DrawReflectedComponent(
        NS::Obj::Component& comp,
        std::span<const ObjectRefOption> refOptions = {},
        const NS::Obj::Component* defaults = nullptr,
        std::span<const ComponentRefOption> componentOptions = {}) noexcept;

} // namespace NS::Editor
