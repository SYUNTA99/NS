#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Gfx/D3dCommon.h"

#include <memory>

namespace DirectX
{
    inline namespace DX11
    {
        class CommonStates;
    }
} // namespace DirectX

namespace NS::Gfx
{
        class D3DDevice;

        //! 所有は D3DDevice。DirectXTK の CommonStates のサンプラーだけを包む。
        //! 混ぜ方・深度・面の捨て方は D3DPipeline が作る
        class D3DCommonStates : public NonCopyable
        {
        public:
            ~D3DCommonStates();

            [[nodiscard]] bool IsValid() const noexcept;

            [[nodiscard]] ID3D11SamplerState* LinearWrap() const noexcept;  //!< 線形補間＋UV繰り返し
            [[nodiscard]] ID3D11SamplerState* LinearClamp() const noexcept; //!< 線形補間＋端で固定
            [[nodiscard]] ID3D11SamplerState* PointWrap() const noexcept;   //!< 点サンプリング＋UV繰り返し
            [[nodiscard]] ID3D11SamplerState* PointClamp() const noexcept;  //!< 点サンプリング＋端で固定

        private:
            std::unique_ptr<DirectX::CommonStates> m_states;

            friend class D3DDevice;
            explicit D3DCommonStates(ID3D11Device* device) noexcept;
        };
} // namespace NS::Gfx
