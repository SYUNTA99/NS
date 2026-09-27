#include "Runtime/Graphics/EffectScene.h"

#include "Runtime/Core/CameraData.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Graphics/GraphicObject.h"
#include "Runtime/Platform/Filesystem.h"
#include "Runtime/Platform/StringUtils.h"

#include <EffekseerRendererDX11.h>

#include <cmath>

namespace NS::Gfx
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

        // Effekseer の Manager が持つ動的入力の数
        constexpr int k_DynamicInputCount = 4;

        // 大きさ・回転・位置の順に掛けた姿勢。行の並びは DirectXMath と同じで、平行移動は 4 行目
        Effekseer::Matrix43 ToEffekseerTransform(const NS::Core::Vector3& position,
                                                 const NS::Core::Quaternion& rotation,
                                                 const NS::Core::Vector3& scale) noexcept
        {
            const NS::Core::Matrix world = NS::Core::Matrix::CreateScale(scale) *
                                           NS::Core::Matrix::CreateFromQuaternion(rotation) *
                                           NS::Core::Matrix::CreateTranslation(position);
            Effekseer::Matrix43 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int column = 0; column < 3; ++column)
                {
                    result.Value[row][column] = world.m[row][column];
                }
            }
            return result;
        }

        // 0〜1 の色の成分を 0〜255 にする。非数と 0 以下は 0、1 以上は 255
        std::uint8_t ToColorByte(float value) noexcept
        {
            if (!(value > 0.0f))
            {
                return 0;
            }
            if (value >= 1.0f)
            {
                return 255;
            }
            return static_cast<std::uint8_t>(std::lround(value * 255.0f));
        }

        std::string ToUtf8(const char16_t* path)
        {
            if (path == nullptr)
            {
                return std::string{};
            }
            return NS::Platform::StringUtils::Utf8FromWide(reinterpret_cast<const wchar_t*>(path));
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

    EffectScene::EffectScene(std::string effectRoot) noexcept : m_effectRoot(std::move(effectRoot))
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
        const std::wstring pathWide = NS::Platform::StringUtils::WideFromUtf8(path);
        const std::u16string pathU16(pathWide.begin(), pathWide.end());

        Effekseer::EffectRef effect = Effekseer::Effect::Create(m_manager, pathU16.c_str());
        if (effect == nullptr)
        {
            NS_LOG_ERROR(Graphics,
                         "EffectScene::Preload: エフェクトを読めなかった ({})",
                         NS::Platform::StringUtils::Utf8FromWide(pathWide));
            return false;
        }
        if (CountMissingResources(effect) > 0)
        {
            return false;
        }

        m_effects.emplace(std::move(key), std::move(effect));
        return true;
    }

    EffectHandle EffectScene::Play(std::string_view name, NS::Core::Vector3 position) noexcept
    {
        return Play(name, EffectPlayDesc{.position = position});
    }

    EffectHandle EffectScene::Play(std::string_view name, const EffectPlayDesc& desc) noexcept
    {
        if (!IsValid())
        {
            return EffectHandle{};
        }

        const std::string key(name);
        const std::unordered_map<std::string, Effekseer::EffectRef>::iterator found = m_effects.find(key);
        if (found == m_effects.end())
        {
            // 続けて呼ばれてもログを埋めないよう、警告は名前ごとに 1 回
            if (m_warnedMissing.emplace(key).second)
            {
                NS_LOG_WARN(Graphics, "EffectScene::Play: Preload していないエフェクト ({})", key);
            }
            return EffectHandle{};
        }

        const Effekseer::Handle handle =
            m_manager->Play(found->second, desc.position.x, desc.position.y, desc.position.z);
        // Play は節を作らず、最初の Update で作る。ここで渡した姿勢・色・入力は生まれる時に読まれる
        m_manager->SetMatrix(handle, ToEffekseerTransform(desc.position, desc.rotation, desc.scale));
        m_manager->SetAllColor(handle,
                               Effekseer::Color(ToColorByte(desc.color.R()),
                                                ToColorByte(desc.color.G()),
                                                ToColorByte(desc.color.B()),
                                                ToColorByte(desc.color.A())));
        for (std::size_t i = 0; i < desc.dynamicInputs.size(); ++i)
        {
            if (desc.dynamicInputs[i].has_value())
            {
                m_manager->SetDynamicInput(handle, static_cast<std::int32_t>(i), desc.dynamicInputs[i].value());
            }
        }
        return EffectHandle{handle};
    }

    void EffectScene::SetTransform(EffectHandle handle,
                                   const NS::Core::Vector3& position,
                                   const NS::Core::Quaternion& rotation,
                                   const NS::Core::Vector3& scale) noexcept
    {
        if (!IsValid() || !handle.IsValid())
        {
            return;
        }
        m_manager->SetMatrix(handle.value, ToEffekseerTransform(position, rotation, scale));
    }

    void EffectScene::SetDynamicInput(EffectHandle handle, int index, float value) noexcept
    {
        if (!IsValid() || !handle.IsValid())
        {
            return;
        }
        // Effekseer は範囲の外を黙って捨てる。書いたつもりの値が効かない理由をログに残す
        if (index < 0 || index >= k_DynamicInputCount)
        {
            NS_LOG_WARN(Graphics, "EffectScene::SetDynamicInput: 動的入力の番号 {} は 0〜3 の外", index);
            return;
        }
        m_manager->SetDynamicInput(handle.value, index, value);
    }

    void EffectScene::StopRoot(EffectHandle handle) noexcept
    {
        if (!IsValid() || !handle.IsValid())
        {
            return;
        }
        m_manager->StopRoot(handle.value);
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

    void EffectScene::Draw(const NS::Core::CameraData& camera) noexcept
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
} // namespace NS::Gfx
