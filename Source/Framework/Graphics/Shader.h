#pragma once

/// @file Shader.h
/// @brief NS::Graphics::Shader — 単一ステージのシェーダ (頂点/ピクセル/ジオメトリ/ハル/ドメイン/コンピュート)
///
/// @details path のファイル名に含まれる `.vs.`/`.ps.`/`.gs.`/`.hs.`/`.ds.`/`.cs.` でステージを判定し、
/// ランタイム `D3DCompile` で対応プロファイル (vs_5_0 等) にコンパイルする (entry はステージ名 + Main 固定)
/// 頂点・ピクセルは読込/コンパイル失敗時に埋込 HLSL の magenta fallback (PS 出力 RGB=(1,0,1)) へ切替わり
/// `IsUsingFallback()` が true になる (他ステージは代替表示が無いため失敗時は IsValid()==false)
/// ファイル名からステージを判定できない場合も IsValid()==false
/// 描画では頂点 + ピクセルの 2 個を作り Material が両方を合成して持つ (D3D11 では別オブジェクトのため)
/// 本型の責務は 1 ステージの生成まで。 バインド (Set*Shader) は Renderer::BindShader が行う
/// コンピュートの Dispatch / UAV バインドは扱わない
/// 入力レイアウトは保持しない (Mesh が `VertexShaderBytecode()` から生成・所有する)
/// Graphics は exposed-D3D lean 設計のため `ID3D11DeviceChild*` を `Native()` で公開する
/// 依存: 生成に Renderer の Device を使う。 context は保持せず、 バインドは Renderer 経由

#include <cstddef>
#include <filesystem>
#include <span>

#include <d3d11.h>
#include <wrl/client.h>

namespace NS::Graphics
{
    class Renderer;
    class Shader;

    /// シェーダのパイプラインステージ種別 (ファイル名から判定)
    enum class ShaderType
    {
        Vertex,
        Pixel,
        Geometry,
        Hull,
        Domain,
        Compute,
    };

    namespace detail
    {
        /// Mesh が InputLayout を生成するための VS バイトコード。 頂点 Shader でない or 構築失敗で空 span
        [[nodiscard]] std::span<const std::byte> GetVertexShaderBytecode(const Shader& shader) noexcept;

        /// ステージに応じた *SSetShader (VS/PS/GS/HS/DS/CS) を context に発行する
        /// 無効な Shader または context==nullptr は no-op。 Renderer::BindShader が本関数を呼ぶ
        void BindShader(ID3D11DeviceContext* context, const Shader& shader) noexcept;
    } // namespace detail

    /// 単一ステージのシェーダ。 path のファイル名 (`.vs.`/`.ps.`/`.gs.`/`.hs.`/`.ds.`/`.cs.`) でステージを判定する
    /// 頂点・ピクセルは読込/コンパイル失敗時に magenta fallback へ切替わる (`IsUsingFallback()` で検知)
    /// 依存: 生成に Renderer の Device を使う。 context は保持せず、 バインドは Renderer::BindShader 経由
    class Shader
    {
    public:
        Shader(Renderer& renderer, const std::filesystem::path& hlslPath);

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;
        Shader(Shader&&) = delete;
        Shader& operator=(Shader&&) = delete;

        /// シェーダが生成済みなら true。fallback でも true。 ステージ判定失敗時は false
        [[nodiscard]] bool IsValid() const noexcept;

        /// 読込・コンパイル失敗で magenta fallback に切替わっているかを問い合わせる
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        /// 判定されたパイプラインステージ種別
        [[nodiscard]] ShaderType Type() const noexcept;

        /// 内部シェーダオブジェクト。継ぎ目で raw D3D を扱う Renderer / detail が type で分岐して使う
        [[nodiscard]] ID3D11DeviceChild* Native() const noexcept;

        /// InputLayout 生成用の VS バイトコード。 頂点ステージでない or 構築失敗時は空 span
        [[nodiscard]] std::span<const std::byte> VertexShaderBytecode() const noexcept;

    private:
        ShaderType m_type = ShaderType::Vertex;
        Microsoft::WRL::ComPtr<ID3D11DeviceChild> m_shader; // 全ステージ共通の保持先 (取得時に static_cast)
        Microsoft::WRL::ComPtr<ID3DBlob> m_vsBytecode;      // 頂点ステージのみ (Mesh の InputLayout 用)
        bool m_fallback = false;
    };

} // namespace NS::Graphics
