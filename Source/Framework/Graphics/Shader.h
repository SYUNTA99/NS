#pragma once

/// @file Shader.h
/// @brief NS::Graphics::Shader — 頂点/ピクセル/ジオメトリ/ハル/ドメイン/コンピュートの単一ステージシェーダ
///
/// @details path のファイル名に含まれる `.vs.`/`.ps.`/`.gs.`/`.hs.`/`.ds.`/`.cs.` でステージを判定し、
/// ランタイム `D3DCompile` で対応プロファイル vs_5_0 等にコンパイルし、 entry はステージ名 + Main 固定とする
/// 頂点・ピクセルは読込/コンパイル失敗時に PS 出力 RGB=1,0,1 の埋込 HLSL magenta fallback へ切替わり
/// `IsUsingFallback()` が true になる。 他ステージは代替表示が無いため失敗時は IsValid()==false
/// ファイル名からステージを判定できない場合も IsValid()==false
/// 描画では頂点 + ピクセルの 2 個を作り D3D11 では別オブジェクトのため Material が両方を合成して持つ
/// 本型の責務は 1 ステージの生成まで。 Set*Shader によるバインドは CommandList が行う
/// コンピュートの Dispatch / UAV バインドは扱わない
/// 入力レイアウトは保持せず、 Mesh が `VertexShaderBytecode()` から生成・所有する
/// D3D11 型を公開する設計のため `ID3D11DeviceChild*` を `Native()` で公開する
/// 依存: 生成にグローバル Device を使い、 context は保持せず、 バインドは CommandList 経由

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>

#include "Framework/Core/NonCopyable.h"
#include "Framework/Graphics/D3dCommon.h"

namespace NS::Graphics
{
    class Shader;

    /// ファイル名から判定するシェーダのパイプラインステージ種別
    /// CommandList のリソース bind 先指定にも使い、 bind 対応は Vertex / Pixel のみ
    enum class ShaderType
    {
        Unknown,
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
    } // namespace detail

    /// 単一ステージのシェーダ。 path のファイル名 `.vs.`/`.ps.`/`.gs.`/`.hs.`/`.ds.`/`.cs.` でステージを判定する
    /// 頂点・ピクセルは読込 / コンパイル失敗時に magenta fallback へ切替わり `IsUsingFallback()` で検知する
    class Shader : public NS::Core::NonCopyable
    {
    public:
        /// HLSL ファイルから単一ステージ Shader を生成する。 失敗時も非 null を返し fallback は IsValid() で検知する
        [[nodiscard]] static std::unique_ptr<Shader> Create(const std::filesystem::path& hlslPath);

        /// シェーダが生成済みなら true。fallback でも true。 ステージ判定失敗時は false
        [[nodiscard]] bool IsValid() const noexcept;

        /// 読込・コンパイル失敗で magenta fallback に切替わっているかを問い合わせる
        [[nodiscard]] bool IsUsingFallback() const noexcept;

        /// 判定されたパイプラインステージ種別
        [[nodiscard]] ShaderType Type() const noexcept;

        /// 内部シェーダオブジェクト。継ぎ目で生 D3D を扱う Renderer / detail が type で分岐して使う
        [[nodiscard]] ID3D11DeviceChild* Native() const noexcept;

        /// InputLayout 生成用の VS バイトコード。 頂点ステージでない or 構築失敗時は空 span
        [[nodiscard]] std::span<const std::byte> VertexShaderBytecode() const noexcept;

        /// 生成元 path から再コンパイルして内部 GPU オブジェクトを差し替える
        /// 成功で true。 失敗時は旧オブジェクトを保持し false を返すので、 編集中の typo で画面を壊さない
        /// Shader* の identity は不変なので Material 等が保持する参照は無効化されない
        [[nodiscard]] bool Reload();

    private:
        explicit Shader(const std::filesystem::path& hlslPath);

        /// m_sourcePath の実ファイルから 1 ステージをコンパイルし、 成功時のみ out へ書く。 fallback はしない
        /// device 無効 / ステージ判定不可 / 読込・コンパイル・生成失敗で false となり、 失敗時 out には触れない
        [[nodiscard]] bool Compile(ComPtr<ID3D11DeviceChild>& outShader,
                                   ComPtr<ID3DBlob>& outVsBytecode) const noexcept;

        std::filesystem::path m_sourcePath; // reload のため生成元 path を保持する
        ShaderType m_type = ShaderType::Unknown;
        ComPtr<ID3D11DeviceChild> m_shader; // 全ステージ共通の保持先で取得時に static_cast する
        ComPtr<ID3DBlob> m_vsBytecode;      // 頂点ステージのみで Mesh の InputLayout に使う
        bool m_fallback = false;
    };

} // namespace NS::Graphics
