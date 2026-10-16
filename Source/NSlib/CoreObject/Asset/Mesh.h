#pragma once

#include "NSlib/CoreObject/Object.h"

#include <memory>

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

    //! @brief 描く形と当たりの形の組。道から読むメッシュ・組み込みの形・骨付きのモデルのメッシュを同じ型で持つ
    //! @details どちらも無い事がある。描く形は装置が無い時と GPU で作れなかった時に空、当たりは骨付きと読めなかった時に空
    class Mesh : public Object
    {
    public:
        Mesh(std::unique_ptr<Gfx::D3DMesh> mesh, std::unique_ptr<Phys::MeshShape> collision) noexcept;
        //! 骨付きのモデルのメッシュ。当たりは持たない
        explicit Mesh(std::unique_ptr<Gfx::D3DSkeletalMesh> mesh) noexcept;
        ~Mesh() noexcept override;

        [[nodiscard]] Gfx::D3DMesh* D3D() const noexcept;
        //! 骨付きで作った時だけ描く形を返し、それ以外は nullptr
        [[nodiscard]] Gfx::D3DSkeletalMesh* D3DSkeletal() const noexcept;
        [[nodiscard]] Phys::MeshShape* Collision() const noexcept;

        //! @brief 描く形を後から入れる。組み込みの形は当たりを装置の前でも作り、描く形を装置の後で足す
        //! @details 骨付きで作ったメッシュには使わない
        void SetD3D(std::unique_ptr<Gfx::D3DMesh> mesh) noexcept;

        NS_REFLECT_NONE(Mesh, Object)

    private:
        std::unique_ptr<Gfx::D3DMesh> m_mesh;
        Gfx::D3DSkeletalMesh* m_skeletal = nullptr; // m_mesh と同じ物。骨付きで作った時だけ持つ
        std::unique_ptr<Phys::MeshShape> m_collision;
    };
} // namespace NS
