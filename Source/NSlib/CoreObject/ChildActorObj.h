#pragma once

#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/IRenderable.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"

#include <span>
#include <vector>

namespace NS
{
    //! 親Actorが所有する子Actorの実体。親Modelを参照した描画も子自身で扱う
    class ChildActorObj : public SceneActor, public IRenderable
    {
    public:
        NS_REFLECT_NONE(ChildActorObj, SceneActor)

        [[nodiscard]] const Model* ModelSource() const noexcept override;
        //! 親にModelがあり、自分にはModelが無い子を、自身の根で描く物として登録する
        //! 繰り返しても登録は一回。描画はSetModelDrawsで指定する
        void UseParentModel();
        //! UseParentModelの後に描画指定をコピーする。空なら描かず、非表示中は保持する
        void SetModelDraws(std::span<const ModelDraw> draws);
        //! 中心が同じ半透明の物の描画順。小さいほど先に描く
        void SetModelSortPriority(int priority) noexcept { m_modelSortPriority = priority; }

        void Collect(const Gfx::RenderContext& context,
                     const RenderSource& source,
                     std::vector<Gfx::DrawItem>& out) override;
        [[nodiscard]] AABB WorldBounds(const RenderSource& source) const noexcept override;
        [[nodiscard]] Gfx::RenderPass Bucket() const noexcept override { return Gfx::RenderPass::Transparent; }
        [[nodiscard]] Vector3 SortCenter(const RenderSource& source) const noexcept override;
        [[nodiscard]] int SortPriority() const noexcept override { return m_modelSortPriority; }
        void OnEndPlay() override;

    protected:
        void OnAppear() override;
        void OnDisappear() noexcept override;

    private:
        bool m_usesParentModel = false;
        int m_modelSortPriority = 0;
        std::vector<ModelDraw> m_modelDraws;
    };
}
