#pragma once

#include "Runtime/Object/Actor.h"

namespace NS::Obj
{
    //! @brief シーンを照らす平行光。DirectionalLight を 1 つ持つ
    class Light : public Actor
    {
    public:
        Light() noexcept;

        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        [[nodiscard]] const char* ClassName() const noexcept override { return "Light"; }
    };
} // namespace NS::Obj
