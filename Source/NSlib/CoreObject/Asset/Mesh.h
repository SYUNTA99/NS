#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Animation/Skeleton.h"
#include "NSlib/Core/Geometry/SkinnedBounds.h"
#include "NSlib/CoreObject/Object.h"

#include <memory>
#include <string>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class D3DMesh;
        class D3DSkeletalMesh;
    } // namespace Gfx
    namespace Phys
    {
        struct MeshShape;
    }

    class Material;

    //! マテリアルの欄が空の時に描くマテリアル。メッシュを作る時に AssetManager が決める
    struct MeshUnsetMaterial
    {
        Material* material = nullptr;
        bool reportWhenUsed = false; // 欄が空のまま描くのが誤りの時 true。初めて使う時に ERROR を出す
    };

    //! 描く形と当たりの形の組。道から読むメッシュ・組み込みの形・スキンのある glb の骨付きのメッシュを同じ型で持つ
    //! どちらも無い事がある。描く形は装置が無い時と GPU で作れなかった時に空、当たりは骨付きと読めなかった時に空
    //! 骨付きのメッシュは、同じファイルの骨格と骨ごとの影響の球も持つ。骨の無いメッシュは両方が空
    //! 道は欄に保存する名前を正規化した物で、組み込みの形は形の名前
    class Mesh : public Object
    {
    public:
        Mesh(std::string path,
             std::unique_ptr<Gfx::D3DMesh> mesh,
             std::unique_ptr<Phys::MeshShape> collision,
             MeshUnsetMaterial unset) noexcept;
        //! 骨付きのメッシュ。当たりは持たない。影響の球は骨格の骨と同じ並びで、要素数は骨の数
        Mesh(std::string path,
             std::unique_ptr<Gfx::D3DSkeletalMesh> mesh,
             Anim::Skeleton skeleton,
             std::vector<BoneSphere> boneSpheres,
             MeshUnsetMaterial unset) noexcept;
        ~Mesh() noexcept override;

        [[nodiscard]] const std::string& Path() const noexcept;

        //! 描く形。無ければ nullptr で、何も描かない
        [[nodiscard]] Gfx::D3DMesh* D3D() const noexcept;
        [[nodiscard]] Phys::MeshShape* Collision() const noexcept;
        //! 同じファイルの骨格。骨の無いメッシュは nullptr
        [[nodiscard]] const Anim::Skeleton* Skeleton() const noexcept;
        //! 骨ごとの影響の球。今の姿勢の境界を組むのに使う。骨の無いメッシュは空
        [[nodiscard]] const std::vector<BoneSphere>& BoneSpheres() const noexcept;
        //! D3D の描く形の局所の境界。描く形が無ければ nullptr
        [[nodiscard]] const AABB* LocalBounds() const noexcept;
        //! 描く形も当たりも骨格も無い場合 true。空の名前と、読めなかった名前のメッシュ
        [[nodiscard]] bool IsEmpty() const noexcept;

        //! マテリアルの欄が空の時に描くマテリアルを返す
        //! 欄が空のまま描くのが誤りのメッシュは、初めて呼んだ時だけ ERROR を出す
        [[nodiscard]] Material* UseUnsetMaterial();

        NS_REFLECT_NONE(Mesh, Object)

    private:
        std::string m_path;
        std::unique_ptr<Gfx::D3DMesh> m_mesh;
        std::unique_ptr<Phys::MeshShape> m_collision;
        Anim::Skeleton m_skeleton;             // 骨の無いメッシュは骨が 0 本
        std::vector<BoneSphere> m_boneSpheres; // 骨の無いメッシュは空
        MeshUnsetMaterial m_unset;
    };
} // namespace NS
