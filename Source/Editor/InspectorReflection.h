#pragma once

//! @brief アクターの持ち物のリフレクション情報から、ImGui編集用UIを自動生成するインスペクタヘルパー

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
        struct EditorAppearance;

        //! @brief ActorRef の欄で参照可能なオブジェクトの選択肢
        struct ActorRefOption
        {
            uint32 id = 0;
            std::string label;
        };

        //! @brief 1フレームのアクターの持ち物の編集で起きた相互作用の集約
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

            // 値が編集された対象と欄。編集が起きたフレームだけ入る
            // 凍結スナップショットへの写しが欄単位で要るので、changed の集約とは別に持つ
            Object* changedTarget = nullptr;
            const FieldDesc* changedField = nullptr;
        };

        //! @brief 欄の名前に上書きの色を付け、戻すを出すか
        //! @details シーンへ書く欄で、actor のシーンの上書きの一覧にある時だけ真。値は比べない。
        //! actor は comp の持ち主、partKey は comp の部品のキー。actor か partKey が nullptr なら偽
        [[nodiscard]] bool ShowsOverride(const Object* comp,
                                         const char* partKey,
                                         const FieldDesc& field,
                                         const SceneActor* actor);

        //! リフレクション欄を編集し、Undo に必要な編集開始・終了と変更内容を返す
        //! refOptions が空なら参照は数値入力。partKey と actor があれば上書きの表示と既定に戻すを使える
        //! filter は欄の見せる名前の部分一致。空か nullptr なら絞らない
        //! actor は comp の持ち主。actor と appearance があれば、行の地を書く先の色で塗る
        [[nodiscard]] ClassObjEditResult DrawReflectedClassObj(Object* comp,
                                                               std::span<const ActorRefOption> refOptions = {},
                                                               const char* partKey = nullptr,
                                                               const char* filter = nullptr,
                                                               const SceneActor* actor = nullptr,
                                                               const EditorAppearance* appearance = nullptr) noexcept;

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
                                                         std::span<const ActorRefOption> refOptions = {}) noexcept;

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
