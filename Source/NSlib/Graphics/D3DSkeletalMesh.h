#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Core/Geometry/Vertex.h"
#include "NSlib/Graphics/D3DMesh.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        //! スキンメッシュの初期化パラメータ
        struct D3DSkeletalMeshDesc
        {
            const SkinnedVertex* vertices = nullptr;
            std::size_t vertexCount = 0;
            const uint32* indices = nullptr;
            std::size_t indexCount = 0;
            std::size_t boneCount = 0;
        };

        //! シェーダへ転送するボーンパレット
        struct BonePaletteCB
        {
            Matrix bones[k_MaxBones];
        };
        static_assert((sizeof(BonePaletteCB) % 16) == 0, "定数バッファのサイズは16バイトの倍数である必要があります。");

        //! ボーンデータをシェーダへ転送する際のスロット番号
        inline constexpr uint k_BonePaletteSlot = 1;

        //! バインド空間の影響頂点を包む球。姿勢に追従する境界の計算に使う
        struct BoneSphere
        {
            Vector3 center{0.0f, 0.0f, 0.0f}; // バインド空間での影響頂点の中心
            float radius = 0.0f;                  // 中心から最遠の影響頂点までの距離、負なら影響頂点なし
        };

        //! 重みが正の頂点から、ボーンごとの影響球を求める。影響のないボーンは負の半径
        [[nodiscard]] std::vector<BoneSphere> ComputeBoneSpheres(const SkinnedVertex* vertices,
                                                                 std::size_t vertexCount,
                                                                 std::size_t boneCount);

        //! 姿勢に追従する球で境界を保守的に包む。影響球がなければ fallback
        [[nodiscard]] AABB MergeSkinnedBounds(const std::vector<BoneSphere>& spheres,
                                                  const Matrix* palette,
                                                  std::size_t paletteCount,
                                                  const AABB& fallback);

        //! スキン頂点のメッシュ。複数個体で共有するため、姿勢は描画側が持つ
        class D3DSkeletalMesh : public D3DMesh
        {
        public:
            //! スキンメッシュを生成する
            [[nodiscard]] static std::unique_ptr<D3DSkeletalMesh> Create(const D3DSkeletalMeshDesc& desc);

            ~D3DSkeletalMesh() override;

            //! スキンメッシュ用の頂点入力レイアウトを返す
            [[nodiscard]] static std::vector<InputElement> SkinnedInputLayout();

            //! 実際に構築されたボーン数 (k_MaxBones で頭打ち)
            [[nodiscard]] std::size_t BoneCount() const noexcept;

            //! ボーンごとの影響球。実行時に現在ポーズの境界を組むのに使う。要素数は BoneCount
            [[nodiscard]] const std::vector<BoneSphere>& BoneSpheres() const noexcept;

        private:
            explicit D3DSkeletalMesh(const D3DSkeletalMeshDesc& desc);

            std::size_t m_boneCount = 0;
            std::vector<BoneSphere> m_boneSpheres; // ボーンごとの影響球、Create 時に頂点から求める
        };
    } // namespace Gfx
} // namespace NS
