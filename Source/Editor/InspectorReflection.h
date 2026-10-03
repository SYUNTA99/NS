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
    struct Curve;
} // namespace NS::Obj

namespace NS::Editor
{
    //! @brief ActorRef の欄で参照可能なオブジェクトの選択肢
    struct ObjectRefOption
    {
        std::uint32_t id = 0; // 参照先の永続ID
        std::string label;    // UI表示用のラベル
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
    //! @return 値の編集有無と、編集の開始・確定フレームを集約した結果
    [[nodiscard]] ComponentEditResult DrawReflectedComponent(NS::Obj::Component& comp,
                                                             std::span<const ObjectRefOption> refOptions = {},
                                                             const NS::Obj::Component* defaults = nullptr) noexcept;

    //! @brief 部品でない値型の 1 フレームの編集で起きた事
    struct ValueEditResult
    {
        bool changed = false;   //!< いずれかの値が編集された
        bool activated = false; //!< いずれかのウィジェットで編集が始まった (ドラッグ開始フレーム)
        bool committed = false; //!< いずれかのウィジェットが非活性化した (編集の有無は問わない)
        //! 値が編集された欄。編集が起きたフレームだけ入る
        const NS::Obj::FieldDesc* changedField = nullptr;
    };

    //! @brief 部品でない値型 (タイムラインの事象など) の欄を、部品と同じウィジェットで描く
    //! @details 欄の並べ方とウィジェットは DrawReflectedComponent と同じ。既定と比べる印と戻すボタンは出さない
    //! (値型には持ち主の種類の既定が無い)
    //! @param[in,out] value 編集する値の実体。info が表す型の物
    //! @param[in] info 値型のリフレクション (T::StaticReflection())
    //! @param[in] refOptions 参照先候補のリスト。指定しない場合は数値入力となる
    //! @return 値の編集有無と、編集の開始・確定フレーム
    [[nodiscard]] ValueEditResult DrawReflectedValue(void* value,
                                                     const NS::Obj::ReflectionInfo& info,
                                                     std::span<const ObjectRefOption> refOptions = {}) noexcept;

    //! @brief 曲線のグラフの横軸の範囲を決める
    //! @details 点の x が 0〜1 に収まる曲線は 0〜1 のまま (部品の欄の曲線は割合を横軸にする)。外へ出る曲線
    //! (横軸がフレーム数のタイムラインの事象の曲線) は点を全部含め、右へ 1 割の余白を足して点を右へ引き伸ばせるようにする
    //! @param[in] curve 描く曲線
    //! @param[out] outMin 左端の x
    //! @param[out] outMax 右端の x。outMin より大きい
    void CurveGraphXRange(const NS::Obj::Curve& curve, float& outMin, float& outMax) noexcept;

    //! @brief DrawReflectedValue を型から呼ぶ
    //! @param[in,out] value 編集する値。T は NS_REFLECT_END_VALUE で欄を宣言した型
    //! @return 値の編集有無と、編集の開始・確定フレーム
    template <class T> [[nodiscard]] ValueEditResult DrawReflectedValue(T& value) noexcept
    {
        return DrawReflectedValue(&value, *T::StaticReflection());
    }

} // namespace NS::Editor
