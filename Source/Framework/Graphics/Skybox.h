#pragma once

/// @file Skybox.h
/// @brief NS::Graphics::Skybox — cubemap ベースのスカイボックス描画ファサード
///
/// @details 単位 cube mesh と TextureCube SRV を保持し、 専用 skybox.vs/ps.hlsl で
/// `xyww` swizzle により depth=1 に張り付けて描画する。 深度比較は LESS_EQUAL、
/// 深度書込は OFF、 CullMode は FRONT (inside-out cube) に固定
/// LoadCubemap は拡張子で auto-detect: `.dds` ならば DirectXTK の
/// CreateDDSTextureFromFileEx で TEXTURECUBE フラグ付きで読込、 ディレクトリならば
/// kurt レイアウト (space_rt/lf/up/dn/ft/bk.png) を WICTextureLoader で読み込み
/// 6-face Texture2D を組み立てる。 失敗時は 1x1 マゼンタ cubemap fallback に切替わり
/// `IsUsingFallback()` が true、 Render() はそのまま安全に呼び出せる
/// 描画順は scene の不透明描画後 + ImGui 直前 (Z=1 同士の深度比較対策)
/// GPU バインドは Render(Renderer&) に渡す Renderer 経由で行い、 DeviceContext は保持しない

#include <filesystem>
#include <memory>

#include <Framework/Core/NonCopyable.h>
#include <Framework/Graphics/D3dCommon.h>
#include <Framework/Math/Math.h>

namespace NS::Graphics
{

    class Renderer;
    class StaticMesh;
    class Shader;
    class Buffer;

    /// cubemap + xyww shader + LESS_EQUAL depth + 前面カリングで描画するスカイボックス。Renderer より先に破棄すること
    class Skybox : public NS::Core::NonCopyable
    {
    public:
        /// Skybox を生成する。 cube mesh / shader / fallback cubemap を構築して返す
        [[nodiscard]] static std::unique_ptr<Skybox> Create();

        ~Skybox();

        /// .dds または 6-face PNG ディレクトリを cubemap としてロードする。失敗時は fallback 維持で false を返す
        [[nodiscard]] bool LoadCubemap(const std::filesystem::path& path);

        /// viewProjNoTranslate で skybox を描画する (不透明描画後・ImGui 前に呼ぶこと)
        void Render(Renderer& renderer, const NS::Math::Matrix& viewProjNoTranslate) noexcept;

        /// 構築完了 (cube mesh / shader / states / fallback SRV が揃っている) なら true
        /// LoadCubemap 未呼出でも fallback により true。 致命的な Device 不在のみ false
        [[nodiscard]] bool IsValid() const noexcept;

        /// 現在 fallback (1x1 マゼンタ cubemap) を使用中か。 ロード失敗 / 未呼出で true
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        /// cubemap SRV (非所有)。 未ロードでも fallback SRV (1x1 マゼンタ) が返るため構築成功後は非 null
        [[nodiscard]] ID3D11ShaderResourceView* Srv() const noexcept;
        /// 構築時に作った DepthStencilState の Desc 値 (LESS_EQUAL / 深度書込 OFF の検証用)
        [[nodiscard]] D3D11_DEPTH_STENCIL_DESC DepthStateDesc() const noexcept;
        /// 構築時に作った RasterizerState の Desc 値 (前面カリングの検証用)
        [[nodiscard]] D3D11_RASTERIZER_DESC RasterStateDesc() const noexcept;

    private:
        Skybox();

        ComPtr<ID3D11Device> m_device;
        std::unique_ptr<StaticMesh> m_cubeMesh;
        std::unique_ptr<Shader> m_vs;
        std::unique_ptr<Shader> m_ps;
        std::unique_ptr<Buffer> m_cb;
        ComPtr<ID3D11SamplerState> m_sampler;
        ComPtr<ID3D11DepthStencilState> m_depthState;
        ComPtr<ID3D11RasterizerState> m_rasterState;
        ComPtr<ID3D11ShaderResourceView> m_cubemapSrv;
        D3D11_DEPTH_STENCIL_DESC m_depthDesc{};
        D3D11_RASTERIZER_DESC m_rasterDesc{};
        bool m_usingFallback = true;
        bool m_valid = false;
    };

} // namespace NS::Graphics
