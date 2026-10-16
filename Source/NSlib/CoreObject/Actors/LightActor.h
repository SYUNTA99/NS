#pragma once

#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/SubObjects/DirectionalLight.h"

namespace NS
{
    //! @brief シーンを照らす平行光。DirectionalLight を 1 つ持つ
    class LightActor : public SceneActor
    {
    public:
        //! 保存形式と TypeRegistry の登録名。読込はこの名前で SceneActor の型を選ぶ
        NS_REFLECT_BEGIN(LightActor, SceneActor)
        NS_REFLECT_SUBOBJ(m_light, "DirectionalLight")
        NS_REFLECT_END()

        void OnEndPlay() override;
        [[nodiscard]] DirectionalLight& Light() noexcept { return *m_light; }
        [[nodiscard]] const DirectionalLight& Light() const noexcept { return *m_light; }

    protected:
        void OnInit() override;
        void OnStart() override;

    private:
        std::unique_ptr<DirectionalLight> m_light;
    };
} // namespace NS
