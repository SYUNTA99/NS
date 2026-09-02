#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/Mesh.h"

#include <memory>
#include <type_traits>

namespace NS::Graphics
{
    //! アニメーションを持たない静的モデル用の頂点データ構造
    struct StaticVertex
    {
        NS::Core::Vector3 position;
        NS::Core::Vector2 uv;
        NS::Core::Vector3 normal;
    };
    static_assert(sizeof(StaticVertex) == 32, "頂点レイアウトの制約上、StaticVertexは32バイトである必要があります。");
    static_assert(std::is_standard_layout_v<StaticVertex>,
                  "GPUへ正しく転送するため、StaticVertexは標準レイアウトである必要があります。");

    //! 静的メッシュの初期化パラメータ
    struct MeshDesc
    {
        const StaticVertex* vertices = nullptr;
        std::size_t vertexCount = 0;
        const std::uint32_t* indices = nullptr;
        std::size_t indexCount = 0;
        const NS::Core::AABB* precomputedBounds = nullptr; // あれば頂点走査を省いて局所境界に使う
    };

    //! @brief アニメーションを持たない静的な 3D モデル
    //! @details 位置・UV 座標・法線の頂点情報を持ち、描画へ渡す
    //! @note 構築に失敗するとキューブのフォールバックへ差し替わる
    //! @warning Renderer より先に破棄すること
    class StaticMesh : public Mesh
    {
    public:
        //! @brief パラメータから静的メッシュを生成する
        [[nodiscard]] static std::unique_ptr<StaticMesh> Create(const MeshDesc& desc);

        ~StaticMesh() override = default;

        //! 静的メッシュ用の頂点入力レイアウトを返す
        [[nodiscard]] static std::vector<InputElement> StandardInputLayout();

    private:
        explicit StaticMesh(const MeshDesc& desc);
    };

} // namespace NS::Graphics