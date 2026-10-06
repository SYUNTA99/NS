#pragma once

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Components/DirectionalLight.h"

namespace NS::Obj
{
    //! @brief シーンを照らす平行光。DirectionalLight を 1 つ持つ
    class Light : public Actor
    {
    public:
        Light() noexcept;
        void ForEachPart(const PartVisitor& visitor) const override;

        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        NS_REFLECT_NONE(Light, NS::Obj::Actor)
    private:
        mutable DirectionalLight m_light;
    };
} // namespace NS::Obj
