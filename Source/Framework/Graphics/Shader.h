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
/// 本型の責務は 1 ステージの生成とバインド (Set*Shader)
/// コンピュートの Dispatch / UAV バインドは扱わない
/// 入力レイアウトは保持しない (Mesh が頂点 Shader の VS バイトコードから生成・所有する)
/// 依存: Renderer の DeviceContext を内部で保持するため Renderer より先に破棄すること

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>

namespace NS::Graphics
{
    class Renderer;
    class Shader;

    namespace detail
    {
        /// Mesh が InputLayout を生成するための VS バイトコード。 頂点 Shader でない or 構築失敗で空 span
        [[nodiscard]] std::span<const std::byte> GetVertexShaderBytecode(const Shader& shader) noexcept;
    } // namespace detail

    /// 単一ステージのシェーダ。 path のファイル名 (`.vs.`/`.ps.`/`.gs.`/`.hs.`/`.ds.`/`.cs.`) でステージを判定する
    /// 頂点・ピクセルは読込/コンパイル失敗時に magenta fallback へ切替わる (`IsUsingFallback()` で検知)
    /// 依存: Renderer の DeviceContext を内部で保持するため Renderer より先に破棄すること
    class Shader
    {
    public:
        struct Impl;

        Shader(Renderer& renderer, const std::filesystem::path& hlslPath);
        ~Shader();

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;
        Shader(Shader&&) = delete;
        Shader& operator=(Shader&&) = delete;

        /// シェーダが生成済みなら true。fallback でも true。 ステージ判定失敗時は false
        [[nodiscard]] bool IsValid() const noexcept;

        /// 読込・コンパイル失敗で magenta fallback に切替わっているかを問い合わせる
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        /// 自分のステージに応じた *SSetShader を発行する
        void Bind() noexcept;

    private:
        std::unique_ptr<Impl> m_pImpl;

        friend std::span<const std::byte> detail::GetVertexShaderBytecode(const Shader& shader) noexcept;
    };

} // namespace NS::Graphics
