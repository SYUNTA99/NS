#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/Gfx/D3DPipeline.h"
#include "NSlib/Gfx/D3DShader.h"

#include <cstddef>
#include <map>
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

    //! @brief シェーダーの MaterialParams の項目 1 つに当てる値。.mat の params に書く形で持つ
    //! @details 数は 1〜4 個で、どれも有限。bool は isBool で、numbers は 0 か 1 の 1 個
    struct MaterialParamValue
    {
        bool isBool = false;
        std::vector<double> numbers;

        bool operator==(const MaterialParamValue&) const = default;
    };

    //! 項目の名前から値を引く。シェーダーから消えた名前の値も持ち続け、.mat に書き戻す
    using MaterialParamValues = std::map<std::string, MaterialParamValue>;

    //! .mat の値。道は ContentRoot からの相対で、ファイルに書いてある綴りのまま持つ
    struct MaterialFileDesc
    {
        std::string vertexShader;
        std::string pixelShader;
        std::vector<std::string> textures; // 並びの番号がテクスチャの枠の番号
        Vector3 baseColor{1.0f, 1.0f, 1.0f};
        Gfx::BlendMode blend = Gfx::BlendMode::Opaque;
        MaterialParamValues params;
    };

    //! value を param に当てられるか。成分の数と bool か数かが型に合い、整数の型には範囲内の整数だけが合う
    [[nodiscard]] bool FitsParam(const Gfx::ShaderParam& param, const MaterialParamValue& value) noexcept;
    //! HLSL に書いた param の初期値を、.mat に書く値の形で返す。対応しない型は数の無い値
    [[nodiscard]] MaterialParamValue InitialParamValue(const Gfx::ShaderParam& param);
    //! @brief layout のバッファの中身を作る
    //! @details 名前で引いた値が FitsParam で合う項目にはその値を、ほかの項目にはシェーダーの初期値を書く
    [[nodiscard]] std::vector<std::byte> PackMaterialParams(const Gfx::ShaderParamLayout& layout,
                                                           const MaterialParamValues& values);

    //! .mat の混ぜ方の綴り (Opaque・Alpha・Additive) を読む。綴りが合わなければ nullopt
    [[nodiscard]] std::optional<Gfx::BlendMode> ParseBlend(std::string_view name) noexcept;
    //! ParseBlend が読む綴りを返す
    [[nodiscard]] std::string_view BlendName(Gfx::BlendMode blend) noexcept;

    //! @brief マテリアル 1 つ。.mat の値と、その値で作った D3DMaterial を持つ
    //! @details シェーダーとテクスチャは AssetManager が持ち、ここは指すだけ。欄の値を変えても D3DMaterial は作り直さず、
    //! 作り直すのは Rebuild だけ。シェーダーの材質の値だけは SetParamValue がその場で送る
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

        //! 頂点と画素のシェーダーの MaterialParams を合わせた並び。作った時と Rebuild の時に取り直す
        [[nodiscard]] const Gfx::ShaderParamLayout& ParamLayout() const noexcept;
        //! param に当てている値。持っている値が合わなければシェーダーの初期値
        [[nodiscard]] MaterialParamValue ParamValue(const Gfx::ShaderParam& param) const;
        //! @brief param の値を value にし、D3DMaterial へ送る。作り直さずに次の描画から効く
        //! @return FitsParam で合わない値は受けずに false
        bool SetParamValue(const Gfx::ShaderParam& param, const MaterialParamValue& value);

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
        Gfx::ShaderParamLayout m_paramLayout;
        std::unique_ptr<Gfx::D3DMaterial> m_material;
    };
} // namespace NS
