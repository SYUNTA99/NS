#pragma once

#include "NSlib/Core/Animation/Skeleton.h"
#include "NSlib/CoreObject/Object.h"

#include <memory>
#include <string>

namespace NS
{
    class Mesh;

    //! 1 つの glTF から読んだ骨付きのメッシュと骨格の組
    //! 読めない名前の時は両方が空で、名前だけを持つ。読んだ後は書き換えないので、何体のアクターでも同じ物を使う
    //! 道は欄に保存する名前。同じファイルを別の綴りで引いても、最初に引いた綴りを持つ
    class SkinnedModel : public Object
    {
    public:
        //! 中身の空のモデル
        explicit SkinnedModel(std::string path) noexcept;
        SkinnedModel(std::string path, std::unique_ptr<Mesh> mesh, Anim::Skeleton skeleton) noexcept;
        ~SkinnedModel() noexcept override;

        [[nodiscard]] const std::string& Path() const noexcept;
        //! 骨付きのメッシュ。中身の空のモデルは nullptr
        [[nodiscard]] Mesh* GetMesh() const noexcept;
        //! メッシュと同じファイルの骨格。中身の空のモデルは nullptr
        [[nodiscard]] const Anim::Skeleton* GetSkeleton() const noexcept;

        NS_REFLECT_NONE(SkinnedModel, Object)

    private:
        std::string m_path;
        std::unique_ptr<Mesh> m_mesh;
        Anim::Skeleton m_skeleton;
    };
} // namespace NS
