#pragma once

#include <Runtime/Object/Reflection/ObjectRef.h>
#include <Runtime/Object/Reflection/Reflection.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace NsTest
{
    //! @brief 欄名でリフレクションの float を読む
    //! @details ASSERT_NE は void を返す関数でしか使えないので、欄の不在は EXPECT_NE で報告する
    //! @param[in] comp GetReflection() を持つ component
    //! @param[in] label 欄の表示名
    //! @return 欄の値。欄が無い場合は非数
    template <class T> [[nodiscard]] inline float ReadTuningField(const T& comp, const char* label)
    {
        const NS::Obj::FieldDesc* field = NS::Obj::FindField(comp.GetReflection(), label);
        EXPECT_NE(field, nullptr) << label;
        if (field == nullptr)
        {
            return std::numeric_limits<float>::quiet_NaN();
        }

        float value = 0.0f;
        field->get(&comp, &value);
        return value;
    }

    //! @brief 欄名でリフレクションの float を書く
    //! @details 欄が無ければ ASSERT_NE でその場の試しを止める
    //! @param[in,out] comp GetReflection() を持つ component
    //! @param[in] label 欄の表示名
    //! @param[in] value 書き込む値
    template <class T> inline void WriteTuningField(T& comp, const char* label, float value)
    {
        const NS::Obj::FieldDesc* field = NS::Obj::FindField(comp.GetReflection(), label);
        ASSERT_NE(field, nullptr) << label;
        field->set(&comp, &value);
    }

    //! @brief 欄名でリフレクションの ObjectRef を書く。データからの構築と同じ set を通す
    //! @details 欄が無ければ ASSERT_NE でその場の試しを止める
    //! @param[in,out] comp GetReflection() を持つ component
    //! @param[in] label 欄の表示名
    //! @param[in] id 参照先の永続 id。0 は未設定
    template <class T> inline void WriteObjectRefField(T& comp, const char* label, std::uint32_t id)
    {
        const NS::Obj::FieldDesc* field = NS::Obj::FindField(comp.GetReflection(), label);
        ASSERT_NE(field, nullptr) << label;
        const NS::Obj::ObjectRef ref{id};
        field->set(&comp, &ref);
    }
} // namespace NsTest
