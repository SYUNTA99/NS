#include "GameCore/Level/ClearFadeComponent.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/ScreenFade.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/RenderContext.h"
#include "Framework/Scene/SceneBase.h"
#include "GameCore/Level/PlayFlowComponent.h"
#include "GameCore/LevelPlayScene.h"
#include "GameCore/Player.h"

#include <algorithm>

namespace NS::GameCore::Level
{
    ClearFadeComponent::ClearFadeComponent() noexcept = default;
    ClearFadeComponent::~ClearFadeComponent() noexcept = default;

    void ClearFadeComponent::OnStart()
    {
        auto* owner = Owner();
        if (owner == nullptr)
            return;
        auto* scene = owner->OwningScene();
        if (scene == nullptr)
            return;

        // ゴール到達 / 死亡からレベル再開へ繋ぐ暗転 / 明転に使う。 構築失敗時は演出なしで続行する
        m_screenFade = NS::Graphics::ScreenFade::Create();
        if (!m_screenFade->IsValid())
            NS_LOG_WARN(::NS::Core::LogCat::Game, "ClearFadeComponent: ScreenFade 構築失敗、 暗転演出なしで続行");

        scene->RegisterRenderable(this);
    }

    void ClearFadeComponent::OnEndPlay()
    {
        if (auto* owner = Owner())
        {
            if (auto* scene = owner->OwningScene())
                scene->UnregisterRenderable(this);
        }
        // ScreenFade は Renderer の資源を握るため、 Renderer より先のここで手放す
        m_screenFade.reset();
    }

    void ClearFadeComponent::Begin() noexcept
    {
        if (m_stage != Stage::None)
            return;
        m_stage = Stage::Out;
        m_timer = 0.0f;
        m_alpha = 0.0f;
    }

    void ClearFadeComponent::Advance(float dt) noexcept
    {
        if (m_stage == Stage::None)
            return;

        m_timer += dt;
        if (m_stage == Stage::Out)
        {
            m_alpha = std::clamp(m_timer / kFadeOutSeconds, 0.0f, 1.0f);
            if (m_timer >= kFadeOutSeconds)
            {
                // 全黒の裏でレベルを頭から組み直し、 spawn へ戻してから明転へ移る
                auto* owner = Owner();
                PlayFlowComponent* flow = nullptr;
                if (owner != nullptr)
                    flow = owner->FindComponent<PlayFlowComponent>();
                if (flow != nullptr)
                    flow->RestartLevel();
                LevelPlayScene* scene = nullptr;
                if (owner != nullptr)
                    scene = dynamic_cast<LevelPlayScene*>(owner->OwningScene());
                if (scene != nullptr)
                {
                    if (auto* player = scene->PlayerRef())
                        player->Root().Snapshot();
                }
                m_stage = Stage::In;
                m_timer = 0.0f;
                m_alpha = 1.0f;
            }
        }
        else
        {
            m_alpha = 1.0f - std::clamp(m_timer / kFadeInSeconds, 0.0f, 1.0f);
            if (m_timer >= kFadeInSeconds)
            {
                m_stage = Stage::None;
                m_alpha = 0.0f;
            }
        }
    }

    void ClearFadeComponent::Draw(const NS::Scene::RenderContext& context)
    {
        // クリア / 死亡の暗転は全描画の最後に最前面で重ねる。 不透明度 0 のフレームは描かない
        if (m_screenFade == nullptr || !m_screenFade->IsValid() || m_alpha <= 0.0f || context.renderer == nullptr)
            return;
        m_screenFade->Render(*context.renderer, NS::Math::Color{0.0f, 0.0f, 0.0f, m_alpha});
    }

} // namespace NS::GameCore::Level
