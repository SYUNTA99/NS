#pragma once

#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Core/Math.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace NS::Graphics
{
    class Buffer;

    //! スキンメッシュアニメーションで使用できるボーン（骨）の最大数
    inline constexpr std::size_t k_MaxBones = 128;

    //! @brief スキンメッシュ用の頂点データ構造。
    //! @details 頂点の基本情報に加え、最大4つのボーンからインデックスとウェイトを保持する
    struct SkinnedVertex
    {
        NS::Core::Vector3 position;
        NS::Core::Vector2 uv;
        NS::Core::Vector3 normal;
        std::uint32_t joints[4];
        float weights[4];
    };
    static_assert(sizeof(SkinnedVertex) == 64, "頂点レイアウトの制約上、SkinnedVertexは64バイトである必要があります。");
    static_assert(std::is_standard_layout_v<SkinnedVertex>,
                  "GPUへ正しく転送するため、SkinnedVertexは標準レイアウトである必要があります。");

    //! スキンメッシュの初期化パラメータ
    struct SkinnedMeshDesc
    {
        const SkinnedVertex* vertices = nullptr;
        std::size_t vertexCount = 0;
        const std::uint32_t* indices = nullptr;
        std::size_t indexCount = 0;
        std::size_t boneCount = 0;
    };

    //! シェーダへ転送するボーンの姿勢データ（ボーンパレット）
    struct BonePaletteCB
    {
        NS::Core::Matrix bones[k_MaxBones];
    };
    static_assert((sizeof(BonePaletteCB) % 16) == 0, "定数バッファのサイズは16バイトの倍数である必要があります。");

    //! ボーンデータをシェーダへ転送する際のスロット番号
    inline constexpr unsigned k_BonePaletteSlot = 1;

    //! @brief ボーン1本が動かす頂点群の広がりを表す球
    //! @details 中心はバインド空間、半径は中心から最遠の影響頂点まで。実行時に現在ポーズのパレットで
    //! 中心だけ動かし半径そのままの箱にすると、回転しても半径が変わらずポーズ追従の境界になる
    struct BoneSphere
    {
        NS::Core::Vector3 center{0.0f, 0.0f, 0.0f}; // バインド空間での影響頂点の中心
        float radius = 0.0f;                        // 中心から最遠の影響頂点までの距離、負なら影響頂点なし
    };

    //! @brief スキン頂点からボーンごとの影響球を求める
    //! @details 頂点を weight>0 の全ボーンへ算入し、ボーンごとに中心=影響頂点の軸並行中心、
    //! 半径=中心からの最遠影響頂点距離を返す。要素数は boneCount。影響の無いボーンは半径を負にする
    [[nodiscard]] std::vector<BoneSphere> ComputeBoneSpheres(const SkinnedVertex* vertices,
                                                             std::size_t vertexCount,
                                                             std::size_t boneCount);

    //! @brief ボーン球とボーンパレットから現在ポーズのモデル空間境界を組む
    //! @details 各球の中心を palette[i] で動かし半径そのままの箱にして全ボーン分を包む。
    //! 影響球が1つも無ければ fallback を返す。手足が箱を割る過小を作らない保守的な包み方
    [[nodiscard]] NS::Core::AABB MergeSkinnedBounds(const std::vector<BoneSphere>& spheres,
                                                    const NS::Core::Matrix* palette,
                                                    std::size_t paletteCount,
                                                    const NS::Core::AABB& fallback);

    //! @brief スキンメッシュの形状データを管理するクラス。
    //! @details アニメーションに必要な特殊な頂点データを保持し、描画処理へ渡す。
    //! @note
    //! 現在の姿勢（ボーンパレット）は描画する各キャラクター自身が管理する状態であるため、このメッシュクラス自体は姿勢データを保持しない。
    class SkeletalMesh : public Mesh
    {
    public:
        //! スキンメッシュを生成する
        [[nodiscard]] static std::unique_ptr<SkeletalMesh> Create(const SkinnedMeshDesc& desc);

        ~SkeletalMesh() override;

        //! スキンメッシュ用の頂点入力レイアウト（シェーダへのデータ構造の渡し方）を取得する
        [[nodiscard]] static std::vector<InputElement> SkinnedInputLayout();

        //! 実際に構築された有効なボーン数（最大数で制限済み）
        [[nodiscard]] std::size_t BoneCount() const noexcept;

        //! ボーンごとの影響球。実行時に現在ポーズの境界を組むのに使う。要素数は BoneCount
        [[nodiscard]] const std::vector<BoneSphere>& BoneSpheres() const noexcept;

    private:
        explicit SkeletalMesh(const SkinnedMeshDesc& desc);

        std::size_t m_boneCount = 0;
        std::vector<BoneSphere> m_boneSpheres; // ボーンごとの影響球、Create 時に頂点から求める
    };

} // namespace NS::Graphics