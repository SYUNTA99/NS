#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

#include <concepts>

namespace NS
{
    //! 型情報とリフレクションを持つ全ての基底。RTTI を使わずに IsA / Cast を提供する
    class Object : public NonCopyable
    {
    public:
        Object() noexcept = default;
        virtual ~Object() noexcept = default;

        [[nodiscard]] static const ReflectionInfo* StaticReflection() noexcept
        {
            static const ReflectionInfo info{"Object", nullptr, 0, nullptr};
            return &info;
        }
        [[nodiscard]] virtual const ReflectionInfo* GetReflection() const noexcept { return StaticReflection(); }
        [[nodiscard]] virtual const char* ClassName() const noexcept
        {
            const ReflectionInfo* info = GetReflection();
            if (info != nullptr)
            {
                return info->typeName;
            }
            return "";
        }
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
} // namespace NS
