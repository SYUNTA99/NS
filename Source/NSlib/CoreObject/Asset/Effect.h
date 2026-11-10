#pragma once

#include "NSlib/CoreObject/Object.h"

#include <memory>
#include <string>

namespace NS
{
    namespace Gfx
    {
        class D3DEffect;
    } // namespace Gfx
} // namespace NS

namespace NS
{
    //! Effekseer のエフェクト素材。ContentRoot からの相対パスと、GPU 上の D3DEffect を持つ
    class Effect : public Object
    {
    public:
        explicit Effect(std::string path) noexcept;
        Effect(std::string path, std::unique_ptr<Gfx::D3DEffect> effect) noexcept;
        ~Effect() override;

        [[nodiscard]] const std::string& Path() const noexcept { return m_path; }
        [[nodiscard]] const Gfx::D3DEffect* D3D() const noexcept { return m_effect.get(); }
        //! 再生できるエフェクトを持つか。読めなかった道筋と、D3DDevice の無い場で読んだ物は false
        [[nodiscard]] bool IsValid() const noexcept { return m_effect != nullptr; }

        NS_REFLECT_NONE(Effect, Object)

    private:
        std::string m_path;
        std::unique_ptr<Gfx::D3DEffect> m_effect;
    };
} // namespace NS
