#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Gfx/D3dCommon.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        // 定数バッファの場所はここだけに書く。HLSL の register(bN) は C++ から読めないので、
        // shader_constants_test がシェーダーを反射で読んで場所と大きさを照らす

        /// 描く 1 回ごとの定数 (物の FrameCB、描く段の定数) のスロット
        inline constexpr uint k_DrawConstantsSlot = 0;
        /// 骨の行列の並び BonePalette のスロット
        inline constexpr uint k_BonePaletteSlot = 1;
        /// 材質の値を書く定数バッファ MaterialParams のスロット
        inline constexpr uint k_MaterialParamsSlot = 2;

        /// MaterialParams の項目の型。配列・構造体・行列などは Unsupported で、値を持たない
        enum class ShaderParamType
        {
            Float,
            Float2,
            Float3,
            Float4,
            Int,
            Uint,
            Bool,
            Unsupported,
        };

        /// MaterialParams の項目 1 つ。位置と大きさはバッファの中のバイトで、HLSL の格納規則で決まる
        struct ShaderParam
        {
            std::string name;
            ShaderParamType type = ShaderParamType::Unsupported;
            uint offset = 0;
            uint size = 0;
            /// HLSL に書いた初期値。成分ごとに 32 ビットで、書いていなければ 0
            std::array<uint32, 4> initialBits{};
            /// シェーダーが読んでいるか。読まない項目は変えても見た目が変わらない
            bool used = false;
        };

        /// MaterialParams の並び。シェーダーに無ければ bufferSize は 0 で項目も空
        struct ShaderParamLayout
        {
            uint bufferSize = 0;
            std::vector<ShaderParam> params;
        };

        /// コンパイルしたバイトコードから MaterialParams の並びを取り出す
        /// @return MaterialParams が b2 以外にある時と、取り出しに失敗した時は nullopt。理由はログに出す
        [[nodiscard]] std::optional<ShaderParamLayout> ReflectMaterialParams(std::span<const std::byte> bytecode);

        /// 頂点と画素の MaterialParams を 1 つにする。2 つのステージは同じバッファを読む
        /// @return 片方が空ならもう片方。両方にあって大きさか項目 (名前・型・位置) が違えば nullopt
        [[nodiscard]] std::optional<ShaderParamLayout> MergeMaterialParams(const ShaderParamLayout& vertex,
                                                                           const ShaderParamLayout& pixel);

        /// シェーダのパイプラインステージ種別
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

        /// ファイル名からステージを判定して実行時にコンパイルするシェーダ
        /// 頂点・ピクセルシェーダの読込失敗はピンクの代替表示で示す
        class D3DShader : public NonCopyable
        {
        public:
            /// HLSLファイルからシェーダを生成する
            [[nodiscard]] static std::unique_ptr<D3DShader> Create(std::string_view hlslPath);

            [[nodiscard]] bool IsValid() const noexcept;

            /// 読み込みに失敗してピンクのフォールバックへ差し替わっているか
            [[nodiscard]] bool IsUsingFallback() const noexcept;

            [[nodiscard]] ShaderType Type() const noexcept;

            [[nodiscard]] ID3D11DeviceChild* Native() const noexcept;

            /// 頂点レイアウト生成用のバイナリコードを取得する
            [[nodiscard]] std::span<const std::byte> VertexShaderBytecode() const noexcept;

            /// 今の中身の番号。中身を持たない間は 0
            /// バイトコードの入力シグネチャから求める。入力が変われば番号も変わり、入力が同じなら別のシェーダーでも同じ番号。
            /// 入力レイアウトのようにバイトコードから作る物は、作った時の番号と比べて作り直すかを決める
            [[nodiscard]] uint64 BytecodeId() const noexcept;

            /// 今の中身の MaterialParams の並び。代わりのピンクと、取り出せなかった時は空
            [[nodiscard]] const ShaderParamLayout& MaterialParams() const noexcept;

            /// hlslPath のファイルを読み直し、シェーダを更新する
            /// 道は持たないので、作った時の道を呼び手が渡す。ファイル名のステージが作った時と違えば読まない
            /// @return 成功した場合 true、それ以外の場合は false。失敗しても前の中身と並びを保つ
            [[nodiscard]] bool Reload(std::string_view hlslPath);

        private:
            explicit D3DShader(std::string_view hlslPath);

            [[nodiscard]] bool Compile(std::string_view hlslPath,
                                       ComPtr<ID3D11DeviceChild>& outShader,
                                       ComPtr<ID3DBlob>& outBytecode) const noexcept;
            /// コンパイルした中身の MaterialParams の並びを持ち直し、頂点シェーダーならバイトコードも持つ
            void TakeBytecode(ComPtr<ID3DBlob> bytecode, std::string_view hlslPath);

            ShaderType m_type = ShaderType::Unknown;
            ComPtr<ID3D11DeviceChild> m_shader;
            ComPtr<ID3DBlob> m_vsBytecode;
            ShaderParamLayout m_materialParams;
            uint64 m_bytecodeId = 0;
            bool m_fallback = false;
        };
    } // namespace Gfx
} // namespace NS
