#pragma once

#include "NSlib/CoreObject/Object.h"

#include <memory>
#include <string>

namespace NS
{
    namespace Gfx
    {
        class D3DTexture;
    }

    //! @brief 画像ファイル 1 つのテクスチャ。中に D3DTexture を持つ
    //! @details 読めない時は中の D3DTexture が代わりのピンクの画像になる
    class Texture : public Object
    {
    public:
        explicit Texture(const std::string& path);
        ~Texture() noexcept override;

        [[nodiscard]] Gfx::D3DTexture* D3D() const noexcept;

        NS_REFLECT_NONE(Texture, Object)

    private:
        std::unique_ptr<Gfx::D3DTexture> m_texture;
    };
} // namespace NS
