#include "Runtime/Graphics/EffectScene.h"

#include "Runtime/Platform/Filesystem.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/StringUtils.h"
#include "Runtime/Graphics/Camera.h"
#include "Runtime/Graphics/GraphicObject.h"

#include <EffekseerRendererDX11.h>

namespace NS::Graphics
{
    namespace
    {
        // Effekseer の Update は 1 を 1/60 秒として数える
        constexpr float k_EffekseerFramesPerSecond = 60.0f;

        // TODO: 仮の値で、Effekseer のサンプルと同じ 8000。衝突のエフェクトを作ったら同時に出る最大数を測って決める
        // 超えた分のインスタンスは作られず、スプライトは描かれない
        constexpr std::int32_t k_MaxSprites = 8000;
        constexpr std::int32_t k_MaxInstances = 8000;

        // 平行移動を 4 行目に置く並びが DirectXMath と Effekseer で同じなので、転置しない
        Effekseer::Matrix44 ToEffekseer(const NS::Core::Matrix& matrix) noexcept
        {
            Effekseer::Matrix44 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int column = 0; column < 4; ++column)
                {
                    result.Values[row][column] = matrix.m[row][column];
                }
            }
            return result;
        }

        std::string ToUtf8(const char16_t* path)
        {
            if (path == nullptr)
            {
                return std::string{};
            }
            return NS::Core::StringUtils::Utf8FromWide(reinterpret_cast<const wchar_t*>(path));
        }

        template <typename GetResource, typename GetPath>
        int CountMissing(std::int32_t count, GetResource getResource, GetPath getPath, std::string_view kind)
        {
            int missing = 0;
            for (std::int32_t i = 0; i < count; ++i)
            {
                if (getResource(i) == nullptr)
                {
                    NS_LOG_ERROR(Graphics, "EffectScene::Preload: {}を読めなかった ({})", kind, ToUtf8(getPath(i)));
                    ++missing;
                }
            }
            return missing;
        }

        // Effect::Create は依存を読み損ねても成功を返し、色テクスチャが無ければ白で塗る。ここで数えて失敗にする
        int CountMissingResources(const Effekseer::EffectRef& effect)
        {
            int missing = 0;
            missing += CountMissing(
                effect->GetColorImageCount(),
                [&](std::int32_t i) { return effect->GetColorImage(i); },
                [&](std::int32_t i) { return effect->GetColorImagePath(i); },
                "テクスチャ");
            missing += CountMissing(
                effect->GetNormalImageCount(),
                [&](std::int32_t i) { return effect->GetNormalImage(i); },
                [&](std::int32_t i) { return effect->GetNormalImagePath(i); },
                "法線テクスチャ");
            missing += CountMissing(
                effect->GetDistortionImageCount(),
                [&](std::int32_t i) { return effect->GetDistortionImage(i); },
                [&](std::int32_t i) { return effect->GetDistortionImagePath(i); },
                "歪みテクスチャ");
            missing += CountMissing(
                effect->GetModelCount(),
                [&](std::int32_t i) { return effect->GetModel(i); },
                [&](std::int32_t i) { return effect->GetModelPath(i); },
                "モデル");
            missing += CountMissing(
                effect->GetMaterialCount(),
                [&](std::int32_t i) { return effect->GetMaterial(i); },
                [&](std::int32_t i) { return effect->GetMaterialPath(i); },
                "マテリアル");
            missing += CountMissing(
                effect->GetCurveCount(),
                [&](std::int32_t i) { return effect->GetCurve(i); },
                [&](std::int32_t i) { return effect->GetCurvePath(i); },
                "カーブ");
            return missing;
        }
    } // namespace

    EffectScene::EffectScene(const EffectSceneDesc& desc) noexcept : m_effectRoot(desc.effectRoot)
    {
        const GraphicObject& gpu = Gpu();
        if (gpu.device == nullptr || gpu.context == nullptr)
        {
            NS_LOG_WARN(Graphics, "EffectScene: Renderer が未構築のため無効のまま作る");
            return;
        }

        const Effekseer::Backend::GraphicsDeviceRef graphicsDevice =
            EffekseerRendererDX11::CreateGraphicsDevice(gpu.device, gpu.context);
        if (graphicsDevice == nullptr)
        {
            NS_LOG_ERROR(Graphics, "EffectScene: Effekseer の GraphicsDevice を作れなかった");
            return;
        }

        const EffekseerRenderer::RendererRef renderer =
            EffekseerRendererDX11::Renderer::Create(graphicsDevice, k_MaxSprites);
        if (renderer == nullptr)
        {
            NS_LOG_ERROR(Graphics, "EffectScene: Effekseer の Renderer を作れなかった (maxSprites={})", k_MaxSprites);
            return;
        }

        const Effekseer::ManagerRef manager = Effekseer::Manager::Create(k_MaxInstances);
        if (manager == nullptr)
        {
            NS_LOG_ERROR(
                Graphics, "EffectScene: Effekseer の Manager を作れなかった (maxInstances={})", k_MaxInstances);
            return;
        }

        // 読み込み時に座標系に合わせて値を反転するため、Effect::Create より先に決める
        manager->SetCoordinateSystem(Effekseer::CoordinateSystem::LH);

        manager->SetSpriteRenderer(renderer->CreateSpriteRenderer());
        manager->SetRibbonRenderer(renderer->CreateRibbonRenderer());
        manager->SetRingRenderer(renderer->CreateRingRenderer());
        manager->SetTrackRenderer(renderer->CreateTrackRenderer());
        manager->SetModelRenderer(renderer->CreateModelRenderer());

        manager->SetTextureLoader(renderer->CreateTextureLoader());
        manager->SetModelLoader(renderer->CreateModelLoader());
        manager->SetMaterialLoader(renderer->CreateMaterialLoader());
        manager->SetCurveLoader(Effekseer::MakeRefPtr<Effekseer::CurveLoader>());

        m_renderer = renderer;
        m_manager = manager;
    }

    EffectScene::~EffectScene()
    {
        // Sprite・Ribbon・Ring・Track の描画は Renderer を生ポインタで持つ
        // ModelRenderer の参照で Renderer が残るのに頼らず、Manager を先に放す
        m_effects.clear();
        m_manager.Reset();
        m_renderer.Reset();
    }

    bool EffectScene::IsValid() const noexcept
    {
        return m_manager != nullptr && m_renderer != nullptr;
    }

    bool EffectScene::Preload(std::string_view name) noexcept
    {
        if (!IsValid())
        {
            return false;
        }

        std::string key(name);
        if (m_effects.contains(key))
        {
            return true;
        }

        const std::string path = NS::Platform::FileSystem::Combine(m_effectRoot, std::string(name) + ".efkefc");
        const std::wstring pathWide = NS::Core::StringUtils::WideFromUtf8(path);
        const std::u16string pathU16(pathWide.begin(), pathWide.end());

        Effekseer::EffectRef effect = Effekseer::Effect::Create(m_manager, pathU16.c_str());
        if (effect == nullptr)
        {
            NS_LOG_ERROR(Graphics,
                         "EffectScene::Preload: エフェクトを読めなかった ({})",
                         NS::Core::StringUtils::Utf8FromWide(pathWide));
            return false;
        }
        if (CountMissingResources(effect) > 0)
        {
            return false;
        }

        m_effects.emplace(std::move(key), std::move(effect));
        return true;
    }

    EffectHandle EffectScene::Play(const EffectPlayDesc& desc) noexcept
    {
        if (!IsValid())
        {
            return EffectHandle{};
        }

        const auto found = m_effects.find(desc.name);
        if (found == m_effects.end())
        {
            // 続けて呼ばれてもログを埋めないよう、警告は名前ごとに 1 回
            if (m_warnedMissing.emplace(desc.name).second)
            {
                NS_LOG_WARN(Graphics, "EffectScene::Play: Preload していないエフェクト ({})", desc.name);
            }
            return EffectHandle{};
        }

        const Effekseer::Handle handle =
            m_manager->Play(found->second, desc.position.x, desc.position.y, desc.position.z);
        return EffectHandle{handle};
    }

    void EffectScene::Stop(EffectHandle handle) noexcept
    {
        if (!IsValid() || !handle.IsValid())
        {
            return;
        }
        m_manager->StopEffect(handle.value);
    }

    void EffectScene::StopAll() noexcept
    {
        if (!IsValid())
        {
            return;
        }
        m_manager->StopAllEffects();
    }

    bool EffectScene::Exists(EffectHandle handle) const noexcept
    {
        if (!IsValid() || !handle.IsValid())
        {
            return false;
        }
        return m_manager->Exists(handle.value);
    }

    void EffectScene::Update(float deltaSeconds) noexcept
    {
        if (!IsValid() || deltaSeconds < 0.0f)
        {
            return;
        }
        m_elapsedSeconds += deltaSeconds;
        // float の Update は UpdateInterval を 0 にするので、1/60 秒刻みに丸めずに進む
        m_manager->Update(deltaSeconds * k_EffekseerFramesPerSecond);
    }

    void EffectScene::Draw(const Camera& camera) noexcept
    {
        if (!IsValid())
        {
            return;
        }

        const NS::Core::Vector3& eye = camera.Position();
        Effekseer::Manager::LayerParameter layer;
        layer.ViewerPosition = Effekseer::Vector3D(eye.x, eye.y, eye.z);
        m_manager->SetLayerParameter(0, layer);

        m_renderer->SetTime(m_elapsedSeconds);
        m_renderer->SetProjectionMatrix(ToEffekseer(camera.Projection()));
        m_renderer->SetCameraMatrix(ToEffekseer(camera.View()));

        m_renderer->BeginRendering();
        Effekseer::Manager::DrawParameter drawParameter;
        // 射影後の手前と奥の深度。既定の 0 と 0 では視錐台カリングが働かない
        drawParameter.ZNear = 0.0f;
        drawParameter.ZFar = 1.0f;
        drawParameter.ViewProjectionMatrix = m_renderer->GetCameraProjectionMatrix();
        // SetCameraMatrix の後に取る。深度クリップはこの位置と向きからの距離で決まり、既定の 0 では働かない
        drawParameter.CameraPosition = m_renderer->GetCameraPosition();
        drawParameter.CameraFrontDirection = m_renderer->GetCameraFrontDirection();
        m_manager->Draw(drawParameter);
        m_renderer->EndRendering();
    }
} // namespace NS::Graphics
