#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/Gfx/D3DPipeline.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class D3DMaterial;
    }
    class Shader;
    class Texture;

    //! Inspector のマテリアルの欄が持つテクスチャの枠の数。これより多い .mat は書かない
    inline constexpr std::size_t k_MaterialTextureSlots = 4;

    //! .mat の値。道は ContentRoot からの相対で、ファイルに書いてある綴りのまま持つ
    struct MaterialFileDesc
    {
        std::string vertexShader;
        std::string pixelShader;
        std::vector<std::string> textures; // 並びの番号がテクスチャの枠の番号
        Vector3 baseColor{1.0f, 1.0f, 1.0f};
        Gfx::BlendMode blend = Gfx::BlendMode::Opaque;
    };

    //! .mat の混ぜ方の綴り (Opaque・Alpha・Additive) を読む。綴りが合わなければ nullopt
    [[nodiscard]] std::optional<Gfx::BlendMode> ParseBlend(std::string_view name) noexcept;
    //! ParseBlend が読む綴りを返す
    [[nodiscard]] std::string_view BlendName(Gfx::BlendMode blend) noexcept;

    //! @brief マテリアル 1 つ。.mat の値と、その値で作った D3DMaterial を持つ
    //! @details シェーダーとテクスチャは AssetManager が持ち、ここは指すだけ。欄の値を変えても D3DMaterial は作り直さず、
    //! 作り直すのは Rebuild だけ
    //! 基本の色はアクターに付けた時に写す色で、描く時には使わない
    //! 道は欄に保存する名前で、共有のマテリアルは共有の名前
    class Material : public Object
    {
    public:
        //! @brief file の値と、AssetManager が file の道から引いたシェーダーとテクスチャで D3DMaterial を作る
        //! @details textures の番号がテクスチャの枠の番号。シェーダーとテクスチャはこのマテリアルより長く生きること
        Material(std::string path,
                 MaterialFileDesc file,
                 Shader* vertexShader,
                 Shader* pixelShader,
                 std::span<Texture* const> textures);
        //! 読めなかった名前のマテリアル。値も D3DMaterial も持たず、道だけを持つ
        explicit Material(std::string path);
        ~Material() noexcept override;

        [[nodiscard]] const std::string& Path() const noexcept;
        //! 読めなかった名前のマテリアルの場合 true
        [[nodiscard]] bool IsEmpty() const noexcept;

        //! 描く D3DMaterial。中身が空なら代わりのマテリアルの物を返す。どちらも無ければ nullptr
        [[nodiscard]] Gfx::D3DMaterial* D3D() const noexcept;
        //! 中身が空の間に描くマテリアル。standIn はこのマテリアルより長く生きること
        void SetStandIn(const Material* standIn) noexcept;
        [[nodiscard]] Shader* VertexShader() const noexcept;
        [[nodiscard]] Shader* PixelShader() const noexcept;
        [[nodiscard]] const MaterialFileDesc& File() const noexcept;

        //! @brief 今の値と、AssetManager が今の値の道から引いたシェーダーとテクスチャで D3DMaterial を作り直す
        //! @details 作れなかった時は false を返し、前の D3DMaterial とシェーダーを残す
        //! シェーダーとテクスチャはこのマテリアルより長く生きること
        [[nodiscard]] bool Rebuild(Shader* vertexShader, Shader* pixelShader, std::span<Texture* const> textures);

        [[nodiscard]] Color BaseColor() const noexcept;
        //! 透明度は .mat に無いので捨てる
        void SetBaseColor(const Color& color) noexcept;
        [[nodiscard]] std::string BlendText() const;
        //! ParseBlend が読めない綴りは受けず、前の値を残す
        void SetBlendText(const std::string& text);
        [[nodiscard]] const std::string& VertexShaderPath() const noexcept;
        void SetVertexShaderPath(const std::string& path);
        [[nodiscard]] const std::string& PixelShaderPath() const noexcept;
        void SetPixelShaderPath(const std::string& path);
        //! 枠に道が無ければ空
        [[nodiscard]] std::string TexturePath(std::size_t slot) const;
        //! 欄の設定の口は引数を 1 つしか取らないので、枠の番号を型の引数で受ける
        template <std::size_t Slot> void SetTexturePath(const std::string& path)
        {
            SetTexturePathAt(Slot, path);
        }

        NS_REFLECT_BEGIN(Material, Object)
        NS_ACCESSOR(Color, "基本の色", BaseColor(), SetBaseColor)
        NS_ACCESSOR(std::string, "混ぜ方", BlendText(), SetBlendText)
        NS_ACCESSOR(std::string, "頂点シェーダー", VertexShaderPath(), SetVertexShaderPath)
        NS_ACCESSOR(std::string, "ピクセルシェーダー", PixelShaderPath(), SetPixelShaderPath)
        NS_ACCESSOR(std::string, "テクスチャ 1", TexturePath(0), SetTexturePath<0>)
        NS_ACCESSOR(std::string, "テクスチャ 2", TexturePath(1), SetTexturePath<1>)
        NS_ACCESSOR(std::string, "テクスチャ 3", TexturePath(2), SetTexturePath<2>)
        NS_ACCESSOR(std::string, "テクスチャ 4", TexturePath(3), SetTexturePath<3>)
        NS_REFLECT_END()

    private:
        void SetTexturePathAt(std::size_t slot, const std::string& path);

        std::string m_path;
        const Material* m_standIn = nullptr;
        MaterialFileDesc m_file;
        Shader* m_vertexShader = nullptr;
        Shader* m_pixelShader = nullptr;
        std::unique_ptr<Gfx::D3DMaterial> m_material;
    };
} // namespace NS
