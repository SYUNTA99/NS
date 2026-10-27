#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/CoreObject/Object.h"

#include <memory>
#include <string>

namespace NS
{
    namespace Gfx
    {
        class D3DMesh;
        class D3DSkeletalMesh;
    }
    namespace Phys
    {
        struct MeshShape;
    }

    class Material;

    /// マテリアルの欄が空の時に描くマテリアル。メッシュを作る時に AssetManager が決める
    struct MeshUnsetMaterial
    {
        Material* material = nullptr;
        bool reportWhenUsed = false; // 欄が空のまま描くのが誤りの時 true。初めて使う時に ERROR を出す
    };

    /// 描く形と当たりの形の組。道から読むメッシュ・組み込みの形・骨付きのモデルのメッシュを同じ型で持つ
    /// どちらも無い事がある。描く形は装置が無い時と GPU で作れなかった時に空、当たりは骨付きと読めなかった時に空
    /// 道は欄に保存する名前を正規化した物で、組み込みの形は形の名前
    class Mesh : public Object
    {
    public:
        Mesh(std::string path,
             std::unique_ptr<Gfx::D3DMesh> mesh,
             std::unique_ptr<Phys::MeshShape> collision,
             MeshUnsetMaterial unset) noexcept;
        /// 骨付きのモデルのメッシュ。当たりは持たない
        Mesh(std::string path, std::unique_ptr<Gfx::D3DSkeletalMesh> mesh, MeshUnsetMaterial unset) noexcept;
        ~Mesh() noexcept override;

        [[nodiscard]] const std::string& Path() const noexcept;

        /// 描く形。無ければ nullptr で、何も描かない
        [[nodiscard]] Gfx::D3DMesh* D3D() const noexcept;
        /// 骨付きで作った時だけ描く形を返し、それ以外は nullptr
        [[nodiscard]] Gfx::D3DSkeletalMesh* D3DSkeletal() const noexcept;
        [[nodiscard]] Phys::MeshShape* Collision() const noexcept;
        /// D3D の描く形の局所の境界。描く形が無ければ nullptr
        [[nodiscard]] const AABB* LocalBounds() const noexcept;
        /// 描く形も当たりも無い場合 true。空の名前と、読めなかった名前のメッシュ
        [[nodiscard]] bool IsEmpty() const noexcept;

        /// マテリアルの欄が空の時に描くマテリアルを返す
        /// 欄が空のまま描くのが誤りのメッシュは、初めて呼んだ時だけ ERROR を出す
        [[nodiscard]] Material* UseUnsetMaterial();

        NS_REFLECT_NONE(Mesh, Object)

    private:
        std::string m_path;
        std::unique_ptr<Gfx::D3DMesh> m_mesh;
        Gfx::D3DSkeletalMesh* m_skeletal = nullptr; // m_mesh と同じ物。骨付きで作った時だけ持つ
        std::unique_ptr<Phys::MeshShape> m_collision;
        MeshUnsetMaterial m_unset;
    };
} // namespace NS
