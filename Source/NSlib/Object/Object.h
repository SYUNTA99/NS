#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Object/Reflection/Reflection.h"

#include <concepts>

namespace NS::Obj
{
    //! @brief 型情報とリフレクションを持つ全ての基底。実行時型情報を切っているので IsA / Cast もここで持つ
    class Object : public NS::NonCopyable
    {
    public:
        Object() noexcept = default;
        virtual ~Object() noexcept = default;

        //! この型のリフレクション情報。派生は NS_REFLECT_* が同じ名前で隠す
        [[nodiscard]] static const ReflectionInfo* StaticReflection() noexcept
        {
            static const ReflectionInfo info{"Object", nullptr, 0, nullptr};
            return &info;
        }
        //! 実体の型のリフレクション情報
        [[nodiscard]] virtual const ReflectionInfo* GetReflection() const noexcept { return StaticReflection(); }
        //! @brief 実体の型名を返す
        //! @return GetReflection の型名。情報が無ければ空文字列
        [[nodiscard]] virtual const char* ClassName() const noexcept
        {
            const ReflectionInfo* info = GetReflection();
            if (info != nullptr)
            {
                return info->typeName;
            }
            return "";
        }
        //! 実体の型が target か、target から派生している場合 true、それ以外の場合は false。target が nullptr なら false
        [[nodiscard]] bool IsA(const ReflectionInfo* target) const noexcept
        {
            if (target == nullptr)
            {
                return false;
            }
            for (const ReflectionInfo* info = GetReflection(); info != nullptr; info = info->base)
            {
                if (info == target)
                {
                    return true;
                }
            }
            return false;
        }
    };

    //! @brief object の実体が T か T の派生なら T として返す
    //! @return 違う型か nullptr なら nullptr
    template <class T>
        requires std::derived_from<T, Object>
    [[nodiscard]] T* Cast(Object* object) noexcept
    {
        if (object != nullptr && object->IsA(T::StaticReflection()))
        {
            return static_cast<T*>(object);
        }
        return nullptr;
    }

    //! @brief object の実体が T か T の派生なら const の T として返す
    //! @return 違う型か nullptr なら nullptr
    template <class T>
        requires std::derived_from<T, Object>
    [[nodiscard]] const T* Cast(const Object* object) noexcept
    {
        if (object != nullptr && object->IsA(T::StaticReflection()))
        {
            return static_cast<const T*>(object);
        }
        return nullptr;
    }
} // namespace NS::Obj
