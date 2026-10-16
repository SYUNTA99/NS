#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Core/Geometry/Vertex.h"
#include "NSlib/Graphics/D3DMesh.h"

#include <memory>

namespace NS
{
    namespace Gfx
    {
        //! 静的メッシュの初期化パラメータ
        struct D3DStaticMeshDesc
        {
            const StaticVertex* vertices = nullptr;
            std::size_t vertexCount = 0;
            const uint32* indices = nullptr;
            std::size_t indexCount = 0;
            const AABB* precomputedBounds = nullptr; // あれば頂点走査を省いて局所境界に使う
        };

        //! @brief アニメーションを持たない静的な 3D モデル
        //! @details 位置・UV 座標・法線の頂点情報を持ち、描画へ渡す
        //! @note 構築に失敗するとキューブのフォールバックへ差し替わる
        //! @warning Renderer より先に破棄すること
        class D3DStaticMesh : public D3DMesh
        {
        public:
            //! @brief パラメータから静的メッシュを生成する
            [[nodiscard]] static std::unique_ptr<D3DStaticMesh> Create(const D3DStaticMeshDesc& desc);

            ~D3DStaticMesh() override = default;

            //! 静的メッシュ用の頂点入力レイアウトを返す
            [[nodiscard]] static std::vector<InputElement> StandardInputLayout();

        private:
            explicit D3DStaticMesh(const D3DStaticMeshDesc& desc);
        };
    } // namespace Gfx
} // namespace NS
