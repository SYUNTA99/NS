#pragma once

/// @file SkeletalMesh.h
/// @brief NS::Graphics::SkeletalMesh — GPU スキニング対象 mesh (skinned 頂点 + ボーンパレット)
///
/// @details 基底 Mesh の派生。 SkinnedVertex (64 byte) を VB、 uint32 を IB に持ち、
/// ボーンパレット定数バッファ (b1, VS) を所有する。 Draw は palette CB を bind してから
/// 基底 Mesh::Draw を呼ぶ。 SkinnedMeshDesc 不正 / Buffer 失敗時は geometry 未設定のまま
/// IsValid()==false に落ちる (skinned 用 fallback geometry は持たない)
/// ボーンパレットは SetBonePalette で外部 (Skeleton 等) から与える。 未設定時は恒等 (bind pose)
/// @pre Renderer の DeviceContext を内部保持するため Renderer より先に破棄すること

#include "Framework/Graphics/Mesh.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Math/Math.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace NS::Graphics
{
    class Renderer;

    /// ボーンパレット (skinning 行列群) の最大数
    /// 128 * 64byte = 8KB で D3D11 定数バッファ上限 (64KB) 内、 単一キャラに十分
    inline constexpr std::size_t kMaxBones = 128;

    /// スキン付き頂点フォーマット (64 byte 固定)
    /// position/uv/normal に加え 1 頂点あたり最大 4 ボーンの影響 (joints=ボーン index, weights=重み) を持つ
    /// joints/weights は GPU 入力レイアウト (BLENDINDICES=uint4 / BLENDWEIGHT=float4) と byte 単位で
    /// 一致させるため固定長配列で保持する
    struct SkinnedVertex
    {
        NS::Math::Vector3 position; // offset 0  POSITION
        NS::Math::Vector2 uv;       // offset 12 TEXCOORD
        NS::Math::Vector3 normal;   // offset 20 NORMAL
        std::uint32_t joints[4];    // offset 32 BLENDINDICES
        float weights[4];           // offset 48 BLENDWEIGHT
    };
    static_assert(sizeof(SkinnedVertex) == 64, "SkinnedVertex は 64 byte 固定");
    static_assert(std::is_standard_layout_v<SkinnedVertex>,
                  "SkinnedVertex は offsetof 使用のため標準レイアウト必須 (SkinnedInputLayout)");

    /// SkeletalMesh 構築パラメータ。 Static Buffer 前提で initialData はコンストラクタ内でコピーされる
    /// Index は uint32_t、 boneCount はボーンパレットの有効要素数 (上限 kMaxBones)
    struct SkinnedMeshDesc
    {
        const SkinnedVertex* vertices = nullptr;
        std::size_t vertexCount = 0;
        const std::uint32_t* indices = nullptr;
        std::size_t indexCount = 0;
        std::size_t boneCount = 0;
    };

    /// GPU スキニング対象 mesh。 SkinnedVertex を VB に持ち、 ボーンパレット CB を b1(VS) に bind して
    /// skinned 頂点シェーダの LBS で変形描画する。 desc 不正 / Buffer 失敗時は IsValid()==false (fallback 無し)
    class SkeletalMesh : public Mesh
    {
    public:
        SkeletalMesh(Renderer& renderer, const SkinnedMeshDesc& desc);
        ~SkeletalMesh() override;

        SkeletalMesh(const SkeletalMesh&) = delete;
        SkeletalMesh& operator=(const SkeletalMesh&) = delete;
        SkeletalMesh(SkeletalMesh&&) = delete;
        SkeletalMesh& operator=(SkeletalMesh&&) = delete;

        /// ボーンパレット (model 空間 skinning 行列群) を次の描画に反映する
        /// 上限 (内部 kMaxBones) を超える分は無視し、 不足分は恒等のまま残す
        void SetBonePalette(std::span<const NS::Math::Matrix> palette) noexcept;

        /// palette CB を b1(VS) に bind してから Mesh::Draw() を呼ぶ。 IsValid()==false なら no-op
        void Draw() noexcept override;

        /// SkinnedVertex に対応する POSITION/TEXCOORD/NORMAL/BLENDINDICES/BLENDWEIGHT の InputElement 配列を返す
        /// 戻り値は ShaderDesc::inputLayout にそのまま流用できる
        [[nodiscard]] static std::vector<InputElement> SkinnedInputLayout();

        /// desc.boneCount を内部上限でクランプした有効ボーン数
        [[nodiscard]] std::size_t BoneCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_pImpl;
    };

} // namespace NS::Graphics
