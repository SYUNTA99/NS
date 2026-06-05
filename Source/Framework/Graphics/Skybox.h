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
/// GPU バインドは Render(Renderer&) / LoadCubemap(Renderer&) に渡す Renderer 経由で行い、 DeviceContext は保持しない

#include <filesystem>
#include <memory>

#include <Framework/Math/Math.h>

struct ID3D11ShaderResourceView;
struct D3D11_DEPTH_STENCIL_DESC;
struct D3D11_RASTERIZER_DESC;

namespace NS::Graphics
{

    class Renderer;
    class Skybox;

    namespace detail
    {
        /// Skybox 内部の cubemap SRV を取得 (テストおよび ThemeRegistry 連携で使用)
        /// 未ロード状態でも fallback SRV (1x1 マゼンタ cubemap) が返るため非 null 保証
        [[nodiscard]] ID3D11ShaderResourceView* GetCubemapSrv(Skybox& skybox) noexcept;

        /// Skybox が構築時に作った DepthStencilState の Desc を取り出す
        /// 状態オブジェクトそのものは内部に閉じ、 公開は Desc 値のみで D3D11 を漏らさない設計
        void GetDepthStateDesc(Skybox& skybox, D3D11_DEPTH_STENCIL_DESC& out) noexcept;

        /// Skybox が構築時に作った RasterizerState の Desc を取り出す (同上、 テスト用途主体)
        void GetRasterStateDesc(Skybox& skybox, D3D11_RASTERIZER_DESC& out) noexcept;
    } // namespace detail

    /// Cubemap ベースのスカイボックス描画ファサード
    /// 単位 cube mesh + cubemap SRV + xyww shader + LESS_EQUAL depth + 前面カリングで描画する
    /// LoadCubemap は .dds (DirectXTK DDSTextureLoader) と 6-face PNG ディレクトリ
    /// の両方を受け付け、 拡張子で auto-detect する
    /// 失敗時は 1x1 マゼンタ cubemap fallback に切替わり IsUsingFallback() が true
    /// 依存: Renderer の DeviceContext を内部で保持するため Renderer より先に破棄すること
    class Skybox
    {
    public:
        struct Impl;

        explicit Skybox(Renderer& renderer);
        ~Skybox();

        Skybox(const Skybox&) = delete;
        Skybox& operator=(const Skybox&) = delete;
        Skybox(Skybox&&) = delete;
        Skybox& operator=(Skybox&&) = delete;

        /// 6-face PNG ディレクトリまたは .dds cubemap をロードする
        /// path がディレクトリならば内部で kurt レイアウトの `space_rt/lf/up/dn/ft/bk.png` を
        /// 探索し 6-face Texture2D + MISC_TEXTURECUBE flag で組み立てる
        /// path のファイル拡張子が .dds ならば DirectXTK CreateDDSTextureFromFileEx で TEXTURECUBE
        /// として読込む
        /// 失敗時は内部 SRV を 1x1 マゼンタ fallback に維持し false を返す。 成功時 true
        /// 6-face PNG の取込は renderer の DeviceContext で CopySubresourceRegion する
        [[nodiscard]] bool LoadCubemap(Renderer& renderer, const std::filesystem::path& path);

        /// 与えられた viewProj (camera の translation 成分を除去済) で skybox を 1 drawcall 描画する
        /// シーン不透明描画の後、 ImGui overlay の前で呼ぶこと (Z=1 重複対策)
        /// fallback 状態でもクラッシュせずマゼンタ cubemap を描く
        void Render(Renderer& renderer, const NS::Math::Matrix& viewProjNoTranslate) noexcept;

        /// 構築完了 (cube mesh / shader / states / fallback SRV が揃っている) なら true
        /// LoadCubemap 未呼出でも fallback により true。 致命的な Device 不在のみ false
        [[nodiscard]] bool IsValid() const noexcept;

        /// 現在 fallback (1x1 マゼンタ cubemap) を使用中か。 ロード失敗 / 未呼出で true
        [[nodiscard]] bool IsUsingFallback() const noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend ID3D11ShaderResourceView* detail::GetCubemapSrv(Skybox& skybox) noexcept;
        friend void detail::GetDepthStateDesc(Skybox& skybox, D3D11_DEPTH_STENCIL_DESC& out) noexcept;
        friend void detail::GetRasterStateDesc(Skybox& skybox, D3D11_RASTERIZER_DESC& out) noexcept;
    };

} // namespace NS::Graphics
