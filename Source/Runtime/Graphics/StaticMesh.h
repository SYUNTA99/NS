#pragma once

#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Math/Math.h"

#include <memory>
#include <type_traits>

namespace NS::Graphics
{
    //! アニメーションを持たない静的モデル用の頂点データ構造。
    struct StaticVertex
    {
        NS::Math::Vector3 position;
        NS::Math::Vector2 uv;
        NS::Math::Vector3 normal;
    };
    static_assert(sizeof(StaticVertex) == 32, "頂点レイアウトの制約上、StaticVertexは32バイトである必要があります。");
    static_assert(std::is_standard_layout_v<StaticVertex>,
                  "GPUへ正しく転送するため、StaticVertexは標準レイアウトである必要があります。");

    //! 静的メッシュの初期化パラメータ。
    struct MeshDesc
    {
        const StaticVertex* vertices = nullptr;
        std::size_t vertexCount = 0;
        const std::uint32_t* indices = nullptr;
        std::size_t indexCount = 0;
        const NS::Math::AABB* precomputedBounds = nullptr; // あれば頂点走査を省いて局所境界に使う
    };

    //! @brief アニメーションを持たない静的な3Dモデルの形状データを管理するクラス。
    //! @details 基本的な頂点情報（位置、UV座標、法線）を保持し、描画処理へ渡す。
    //! @note 構築に失敗した場合は、エラーを示す代替モデル（キューブ形状）が自動的に適用される。
    //! @warning
    //! 破棄順序のバグを防ぐため、描画システム（Renderer等）よりも先に破棄されるようライフサイクルを管理すること。
    class StaticMesh : public Mesh
    {
    public:
        //! @brief パラメータから静的メッシュを生成する
        [[nodiscard]] static std::unique_ptr<StaticMesh> Create(const MeshDesc& desc);

        ~StaticMesh() override = default;

        //! 静的メッシュ用の頂点入力レイアウト（シェーダへのデータ構造の渡し方）を取得する
        [[nodiscard]] static std::vector<InputElement> StandardInputLayout();

    private:
        explicit StaticMesh(const MeshDesc& desc);
    };

} // namespace NS::Graphics