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

    /// 画像ファイル 1 つのテクスチャ。中に D3DTexture を持つ
    /// 読めない時は中の D3DTexture が代わりのピンクの画像になる
    class Texture : public Object
    {
    public:
        explicit Texture(std::string path);
        ~Texture() noexcept override;

        [[nodiscard]] const std::string& Path() const noexcept;

        [[nodiscard]] Gfx::D3DTexture* D3D() const noexcept;

        NS_REFLECT_NONE(Texture, Object)

    private:
        std::string m_path;
        std::unique_ptr<Gfx::D3DTexture> m_texture;
    };
} // namespace NS
