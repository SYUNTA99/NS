#pragma once

#include "NSlib/Core/Geometry/Vertex.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/D3DShader.h"

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
        };

        //! シェーダへ転送するボーンパレット。差す場所は k_BonePaletteSlot
        struct BonePaletteCB
        {
            Matrix bones[k_MaxBones];
        };
        static_assert((sizeof(BonePaletteCB) % 16) == 0, "定数バッファのサイズは16バイトの倍数である必要があります。");

        //! スキン頂点のメッシュ。複数個体で共有するため、姿勢は描画側が持つ
        class D3DSkeletalMesh : public D3DMesh
        {
        public:
            //! スキンメッシュを生成する。D3DDevice の無い場では ERROR を出さずに nullptr を返す
            [[nodiscard]] static std::unique_ptr<D3DSkeletalMesh> Create(const D3DSkeletalMeshDesc& desc);

            ~D3DSkeletalMesh() override;

            //! スキンメッシュ用の頂点入力レイアウトを返す
            [[nodiscard]] static std::vector<InputElement> SkinnedInputLayout();

        private:
            explicit D3DSkeletalMesh(const D3DSkeletalMeshDesc& desc);
        };
    } // namespace Gfx
} // namespace NS
