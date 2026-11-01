#include "NSlib/CoreObject/ChildActorObj.h"
#include "NSlib/Core/Assert.h"

namespace NS
{
    const Model* ChildActorObj::ModelSource() const noexcept
    {
        if (m_usesParentModel)
        {
            return ParentActor()->ModelSource();
        }
        return SceneActor::ModelSource();
    }

    void ChildActorObj::UseParentModel()
    {
        const SceneActor* parent = ParentActor();
        NS_ASSERT(Scene, parent != nullptr && ModelClassObj() == nullptr, "親のModelを使う子に自前のModelは持たせない");
        NS_ASSERT(Scene, parent != nullptr && parent->ModelSource() != nullptr, "親に参照するModelが無い");
        if (m_usesParentModel || parent == nullptr || ModelClassObj() != nullptr || parent->ModelSource() == nullptr)
        {
            return;
        }
        m_usesParentModel = true;
        InitDraw(this, this, &Root());
        if (!IsAppeared())
        {
            HideModel(this, this);
        }
    }

    void ChildActorObj::SetModelDraws(std::span<const ModelDraw> draws)
    {
        NS_ASSERT(Scene, m_usesParentModel, "親のModelを使う設定より先に描画指定を書いた");
        if (draws.empty())
        {
            m_modelDraws.clear();
            return;
        }
        m_modelDraws.assign(draws.begin(), draws.end());
    }

    void ChildActorObj::Collect(const Gfx::RenderContext& context,
                              const RenderSource& source,
                              std::vector<Gfx::DrawItem>& out)
    {
        if (m_usesParentModel)
        {
            ModelSource()->CollectDraws(context, source, m_modelDraws, out);
        }
    }

    AABB ChildActorObj::WorldBounds(const RenderSource& source) const noexcept
    {
        if (m_usesParentModel)
        {
            return ModelSource()->DrawsWorldBounds(source, m_modelDraws);
        }
        return {};
    }

    Vector3 ChildActorObj::SortCenter(const RenderSource& source) const noexcept
    {
        if (m_usesParentModel)
        {
            return ModelSource()->SortCenter(source);
        }
        return source.root.Translation();
    }

    void ChildActorObj::OnAppear()
    {
        SceneActor::OnAppear();
        if (m_usesParentModel && IsAppeared())
        {
            ShowModel(this, this);
        }
    }

    void ChildActorObj::OnDisappear() noexcept
    {
        SceneActor::OnDisappear();
        if (m_usesParentModel)
        {
            HideModel(this, this);
        }
    }

    void ChildActorObj::OnEndPlay()
    {
        SceneActor::OnEndPlay();
        m_modelDraws.clear();
        if (m_usesParentModel)
        {
            EndDraw(this, this);
        }
    }
}
