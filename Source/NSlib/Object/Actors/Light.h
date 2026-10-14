#pragma once

#include "NSlib/Object/Actor.h"
#include "NSlib/Object/SubObjects/DirectionalLight.h"

namespace NS::Obj
{
    //! @brief シーンを照らす平行光。DirectionalLight を 1 つ持つ
    class Light : public Actor
    {
    public:
        //! 保存形式と TypeRegistry の登録名。読込はこの名前で Actor の型を選ぶ
        NS_REFLECT_BEGIN(Light, NS::Obj::Actor)
        NS_REFLECT_SUBOBJ(m_light, "DirectionalLight")
        NS_REFLECT_END()

    protected:
        void OnInit() override;

    private:
        std::unique_ptr<DirectionalLight> m_light;
    };
} // namespace NS::Obj
