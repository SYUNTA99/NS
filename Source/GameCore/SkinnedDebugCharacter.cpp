#include "GameCore/SkinnedDebugCharacter.h"

#include "Framework/App/Application.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Scene/AssetManager.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/SkeletalAnimationComponent.h"
#include "Framework/Scene/Transform.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace
{
    constexpr NS::Math::Vector3 kFootAnchor{2.5f, 0.0f, 0.0f};
    constexpr float kTargetHeight = 1.8f;
} // namespace

std::unique_ptr<SkinnedDebugCharacter> SkinnedDebugCharacter::Create(NS::Scene::AssetManager& assets,
                                                                     NS::Scene::SceneBase* scene,
                                                                     const std::filesystem::path& modelDir,
                                                                     const std::filesystem::path& materialPath)
{
    // Xbot は人型 profile 骨名と一致するため優先。 CesiumMan は骨名が違うので外部アニメ流用不可
    std::filesystem::path modelPath = modelDir / "CesiumMan.glb";
    for (const char* name : {"Xbot.glb", "CesiumMan.glb"})
    {
        const auto candidate = modelDir / name;
        if (NS::Core::FileSystem::Exists(candidate))
        {
            modelPath = candidate;
            break;
        }
    }

    NS::Scene::LoadedSkinnedModel model = assets.GetOrLoadSkinnedModel(modelPath);
    if (!model.valid)
    {
        NS_LOG_WARN(::NS::Core::LogCat::Game,
                    "SkinnedDebugCharacter: skinned glTF 取得失敗、 表示を skip: {}",
                    modelPath.string());
        return nullptr;
    }

    const NS::Scene::LoadedMaterial material = assets.LoadMaterial(materialPath);
    if (material.material == nullptr)
    {
        NS_LOG_WARN(::NS::Core::LogCat::Game,
                    "SkinnedDebugCharacter: material 取得失敗、 表示を skip: {}",
                    materialPath.string());
        return nullptr;
    }

    // bind ポーズ境界から目標身長に合わせた一様スケールを出し、 足元を接地点へ寄せる
    const float modelHeight = std::max(model.boundsMax.y - model.boundsMin.y, 1e-3f);
    const float fitScale = kTargetHeight / modelHeight;
    const NS::Math::Vector3 fitPosition{kFootAnchor.x - (model.boundsMin.x + model.boundsMax.x) * 0.5f * fitScale,
                                        kFootAnchor.y - model.boundsMin.y * fitScale,
                                        kFootAnchor.z - (model.boundsMin.z + model.boundsMax.z) * 0.5f * fitScale};

    std::unique_ptr<SkinnedDebugCharacter> character(new SkinnedDebugCharacter());
    character->AttachScene(scene);
    character->Root().SetPosition(fitPosition);
    character->Root().SetScale({fitScale, fitScale, fitScale});
    auto* meshRenderer = character->AddComponent<NS::Scene::MeshRendererComponent>(model.mesh, material.material);
    meshRenderer->SetBaseColor(material.baseColor);

    const std::size_t boneCount = model.skeleton.BoneCount();
    const std::size_t clipCount = model.clips.size();
    character->m_anim = character->AddComponent<NS::Scene::SkeletalAnimationComponent>(
        model.mesh, std::move(model.skeleton), std::move(model.clips));
    character->m_anim->SetSpeed(character->m_speed);
    character->OnStart();
    character->Root().Snapshot();

    NS_LOG_INFO(::NS::Core::LogCat::Game,
                "SkinnedDebugCharacter: 読込 ({} bones, {} clips): {}",
                boneCount,
                clipCount,
                modelPath.string());
    return character;
}

void SkinnedDebugCharacter::HandleDebugInput()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr || m_anim == nullptr || app->Input().UiWantsKeyboard())
        return;

    auto& kb = app->Input().Keyboard();
    if (kb.IsPressed(NS::Platform::Key::F1))
    {
        if (m_anim->IsPlaying())
            m_anim->Pause();
        else
            m_anim->Play();
    }
    if (kb.IsPressed(NS::Platform::Key::F2) && m_anim->ClipCount() > 0)
    {
        const std::size_t next = (m_anim->CurrentClip() + 1) % m_anim->ClipCount();
        m_anim->SelectClip(next);
        m_anim->Play();
    }
    if (kb.IsPressed(NS::Platform::Key::F3))
    {
        m_speed = std::max(0.0f, m_speed - 0.25f);
        m_anim->SetSpeed(m_speed);
    }
    if (kb.IsPressed(NS::Platform::Key::F4))
    {
        m_speed += 0.25f;
        m_anim->SetSpeed(m_speed);
    }
}
