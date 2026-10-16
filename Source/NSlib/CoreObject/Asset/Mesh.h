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

    //! @brief 描く形と当たりの形の組。道から読むメッシュ・組み込みの形・骨付きのモデルのメッシュを同じ型で持つ
    //! @details どちらも無い事がある。描く形は装置が無い時と GPU で作れなかった時に空、当たりは骨付きと読めなかった時に空
    //! 道は欄に保存する名前で、組み込みの形は形の名前。同じファイルを別の綴りで引いても、最初に引いた綴りを持つ
    class Mesh : public Object
    {
    public:
        Mesh(std::string path, std::unique_ptr<Gfx::D3DMesh> mesh, std::unique_ptr<Phys::MeshShape> collision) noexcept;
        //! 骨付きのモデルのメッシュ。当たりは持たない
        Mesh(std::string path, std::unique_ptr<Gfx::D3DSkeletalMesh> mesh) noexcept;
        ~Mesh() noexcept override;

        [[nodiscard]] const std::string& Path() const noexcept;

        //! 描く形。自分の描く形が無ければ代わりのメッシュの描く形を返す。どちらも無ければ nullptr
        [[nodiscard]] Gfx::D3DMesh* D3D() const noexcept;
        //! 骨付きで作った時だけ描く形を返し、それ以外は nullptr
        [[nodiscard]] Gfx::D3DSkeletalMesh* D3DSkeletal() const noexcept;
        [[nodiscard]] Phys::MeshShape* Collision() const noexcept;
        //! D3D の描く形の局所の境界。描く形が無ければ nullptr
        [[nodiscard]] const AABB* LocalBounds() const noexcept;
        //! 自分の描く形が無く、代わりのメッシュで描く場合 true
        [[nodiscard]] bool UsesStandIn() const noexcept;

        //! @brief 描く形を後から入れる。組み込みの形は当たりを装置の前でも作り、描く形を装置の後で足す
        //! @details 骨付きで作ったメッシュには使わない
        void SetD3D(std::unique_ptr<Gfx::D3DMesh> mesh) noexcept;
        //! 自分の描く形が無い間に描くメッシュ。standIn はこのメッシュより長く生きること
        void SetStandIn(const Mesh* standIn) noexcept;

        NS_REFLECT_NONE(Mesh, Object)

    private:
        std::string m_path;
        const Mesh* m_standIn = nullptr;
        std::unique_ptr<Gfx::D3DMesh> m_mesh;
        Gfx::D3DSkeletalMesh* m_skeletal = nullptr; // m_mesh と同じ物。骨付きで作った時だけ持つ
        std::unique_ptr<Phys::MeshShape> m_collision;
    };
} // namespace NS
