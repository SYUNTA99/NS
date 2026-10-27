#pragma once

#include "NSlib/CoreObject/ClassObjs/DirectionalLight.h"
#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    //! シーンを照らす平行光。DirectionalLight を 1 つ持つ
    class LightActor : public SceneActor
    {
    public:
        //! 保存形式と TypeRegistry の登録名。読込はこの名前で SceneActor の型を選ぶ
        NS_REFLECT_BEGIN(LightActor, SceneActor)
        NS_CLASSOBJ(m_light, "DirectionalLight")
        NS_REFLECT_END()

        void OnEndPlay() override;
        [[nodiscard]] DirectionalLight& Light() noexcept { return *m_light; }
        [[nodiscard]] const DirectionalLight& Light() const noexcept { return *m_light; }

    protected:
        void OnInit() override;
        //! 光は入れたまま、出ている間だけシーンを照らす
        void OnAppear() override;
        void OnDisappear() noexcept override;

    private:
        std::unique_ptr<DirectionalLight> m_light;
    };
} // namespace NS
