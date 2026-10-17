#pragma once

//! @brief サブオブジェクトのリフレクション情報から、ImGui編集用UIを自動生成するインスペクタヘルパー

#include "NSlib/CoreObject/Reflection/Reflection.h"

#include "NSlib/Core/Types.h"
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    class Object;
    class SceneActor;
    struct Curve;
} // namespace NS

namespace NS
{
    namespace Editor
    {
        //! @brief ActorRef の欄で参照可能なオブジェクトの選択肢
        struct ObjectRefOption
        {
            uint32 id = 0;
            std::string label;
        };

        //! @brief 1フレームのサブオブジェクト編集で起きた相互作用の集約
        //! @details activated/committed はドラッグを1つのundo単位へまとめるための開始・終了フレームの印
        struct ClassObjEditResult
        {
            bool changed = false;   // いずれかの値が編集された
            bool activated = false; // いずれかのウィジェットで編集が始まった (ドラッグ開始フレーム)
            bool committed = false; // いずれかのウィジェットが非活性化した (編集の有無は問わない)

            // 既定へ戻す要求。ボタンが押されたフレームだけ対象と欄が入る
            // 値をこの場で書くと 取り消し の控えを取る前に実体が動くので、適用は呼び出し側へ預ける
            Object* revertTarget = nullptr;
            const FieldDesc* revertField = nullptr;

            // 種類の既定にする要求。選ばれたフレームだけ対象と欄が入る
            // 同じ種類の他の個体とファイルも書き換えるので、適用は呼び出し側へ預ける
            Object* promoteTarget = nullptr;
            const FieldDesc* promoteField = nullptr;

            // 値が編集された対象と欄。編集が起きたフレームだけ入る
            // 凍結スナップショットへの写しが欄単位で要るので、changed の集約とは別に持つ
            Object* changedTarget = nullptr;
            const FieldDesc* changedField = nullptr;
        };

        //! @brief リフレクション欄 1 つの値が既定と違うか
        //! @param[in] defaults 既定インスタンス。nullptr なら常に false
        [[nodiscard]] bool FieldDiffersFromDefault(const Object& comp,
                                                   const Object* defaults,
                                                   const FieldDesc& field) noexcept;

        //! @brief リフレクション欄 1 つを既定値へ戻す
        void RevertFieldToDefault(Object& comp,
                                  const Object& defaults,
                                  const FieldDesc& field) noexcept;

        //! リフレクション欄を編集し、Undo に必要な編集開始・終了と変更内容を返す
        //! refOptions が空なら参照は数値入力、defaults があれば上書きの表示と復元を使える
        //! filter は欄の見せる名前の部分一致。空か nullptr なら絞らない
        //! actor と row は comp の持ち主とその表の行。どちらかが nullptr なら種類の既定にする操作を出さない
        [[nodiscard]] ClassObjEditResult DrawReflectedClassObj(Object& comp,
                                                               std::span<const ObjectRefOption> refOptions = {},
                                                               const Object* defaults = nullptr,
                                                               const char* filter = nullptr,
                                                               const SceneActor* actor = nullptr,
                                                               const ClassObjInfo* row = nullptr) noexcept;

        //! @brief 部品でない値型の 1 フレームの編集で起きた事
        struct ValueEditResult
        {
            bool changed = false;   //!< いずれかの値が編集された
            bool activated = false; //!< いずれかのウィジェットで編集が始まった (ドラッグ開始フレーム)
            bool committed = false; //!< いずれかのウィジェットが非活性化した (編集の有無は問わない)
            //! 値が編集された欄。編集が起きたフレームだけ入る
            const FieldDesc* changedField = nullptr;
        };

        //! info の型として value を編集する。部品と同じ UI を使い、既定値との比較は行わない
        //! refOptions が空なら参照は数値入力
        [[nodiscard]] ValueEditResult DrawReflectedValue(void* value,
                                                         const ReflectionInfo& info,
                                                         std::span<const ObjectRefOption> refOptions = {}) noexcept;

        //! 点の x が 0〜1 なら表示も 0〜1。範囲外の点があれば全点と右側の 1 割の余白を含める
        //! outMax は outMin より大きい
        void CurveGraphXRange(const Curve& curve, float& outMin, float& outMax) noexcept;

        //! T は NS_REFLECT_END_VALUE で欄を宣言した型
        template <class T> [[nodiscard]] ValueEditResult DrawReflectedValue(T& value) noexcept
        {
            return DrawReflectedValue(&value, *T::StaticReflection());
        }
    } // namespace Editor
} // namespace NS
