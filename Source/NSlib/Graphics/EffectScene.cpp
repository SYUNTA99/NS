#include "NSlib/Graphics/EffectScene.h"

#include "NSlib/Core/CameraData.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Graphics/GraphicObject.h"
#include "NSlib/Graphics/Texture.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/StringUtils.h"

#include <EffekseerRendererDX11.h>

#include <cmath>
#include <format>

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
        Effekseer::Matrix44 ToEffekseer(const NS::Matrix& matrix) noexcept
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
        Effekseer::Matrix43 ToEffekseerTransform(const NS::Vector3& position,
                                                 const NS::Quaternion& rotation,
                                                 const NS::Vector3& scale) noexcept
        {
            const NS::Matrix world = NS::Matrix::CreateScale(scale) *
                                           NS::Matrix::CreateFromQuaternion(rotation) *
                                           NS::Matrix::CreateTranslation(position);
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

        // 名前と、その名前を何回目に出したかから乱数の種を作る。名前を FNV-1a で 32 ビットに畳み、回数を混ぜる
        // Effekseer は既定で std::rand から種を引くので、他の絵や std::rand の使い手が 1 つ増えるだけで
        // 全部の絵の乱数がずれる
        std::int32_t SeedOf(std::string_view name, std::uint32_t playIndex) noexcept
        {
            std::uint32_t hash = 2166136261u;
            for (const char c : name)
            {
                hash ^= static_cast<std::uint8_t>(c);
                hash *= 16777619u;
            }
            hash ^= playIndex * 2654435761u;
            return static_cast<std::int32_t>(hash & 0x7FFFFFFFu);
        }

        std::string ToUtf8(const char16_t* path)
        {
            if (path == nullptr)
            {
                return std::string{};
            }
            return NS::OS::StringUtils::Utf8FromWide(reinterpret_cast<const wchar_t*>(path));
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
        if (m_renderer != nullptr)
        {
            m_renderer->SetDepth(nullptr, EffekseerRenderer::DepthReconstructionParameter{});
        }
        for (DepthCopy& copy : m_depthCopies)
        {
            copy = DepthCopy{};
        }
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

        const std::string path = NS::OS::FileSystem::Combine(m_effectRoot, std::string(name) + ".efkefc");
        const std::wstring pathWide = NS::OS::StringUtils::WideFromUtf8(path);
        const std::u16string pathU16(pathWide.begin(), pathWide.end());

        Effekseer::EffectRef effect = Effekseer::Effect::Create(m_manager, pathU16.c_str());
        if (effect == nullptr)
        {
            NS_LOG_ERROR(Graphics,
                         "EffectScene::Preload: エフェクトを読めなかった ({})",
                         NS::OS::StringUtils::Utf8FromWide(pathWide));
            return false;
        }
        if (CountMissingResources(effect) > 0)
        {
            return false;
        }

        m_effects.emplace(std::move(key), std::move(effect));
        return true;
    }

    EffectHandle EffectScene::Play(std::string_view name, NS::Vector3 position) noexcept
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
        // Play が std::rand から引いた種を上書きする。節が生まれる最初の Update より前なので、この種で生まれる
        std::uint32_t& playCount = m_playCounts[key];
        m_manager->SetRandomSeed(handle, SeedOf(key, playCount));
        ++playCount;
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
                                   const NS::Vector3& position,
                                   const NS::Quaternion& rotation,
                                   const NS::Vector3& scale) noexcept
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

    void EffectScene::Draw(const NS::CameraData& camera) noexcept
    {
        if (!IsValid())
        {
            return;
        }

        const NS::Vector3& eye = camera.Position();
        Effekseer::Manager::LayerParameter layer;
        layer.ViewerPosition = Effekseer::Vector3D(eye.x, eye.y, eye.z);
        m_manager->SetLayerParameter(0, layer);

        m_renderer->SetTime(m_elapsedSeconds);
        m_renderer->SetProjectionMatrix(ToEffekseer(camera.Projection()));
        m_renderer->SetCameraMatrix(ToEffekseer(camera.View()));
        PassDepth(camera);

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

    bool EffectScene::PassesDepth() const noexcept
    {
        if (!IsValid())
        {
            return false;
        }
        Effekseer::Backend::TextureRef texture;
        EffekseerRenderer::DepthReconstructionParameter parameter;
        m_renderer->GetDepth(texture, parameter);
        return texture != nullptr;
    }

    void EffectScene::PassDepth(const NS::CameraData& camera) noexcept
    {
        ++m_drawCount;
        ID3D11DeviceContext* context = Gpu().context;
        ComPtr<ID3D11DepthStencilView> dsv;
        if (context != nullptr)
        {
            context->OMGetRenderTargets(0, nullptr, dsv.GetAddressOf());
        }
        // 奥行きを結ばない描画先は奥行き無しで描く。前の描画先の奥行きを残すと、別の絵の物で薄くなる
        if (dsv == nullptr)
        {
            m_renderer->SetDepth(nullptr, EffekseerRenderer::DepthReconstructionParameter{});
            m_depthFallback = false;
            return;
        }
        ComPtr<ID3D11Resource> resource;
        dsv->GetResource(resource.GetAddressOf());
        ComPtr<ID3D11Texture2D> source;
        if (resource == nullptr || FAILED(resource.As(&source)))
        {
            FallBackWithoutDepth("奥行きが 2D のテクスチャでない", 0, 0);
            return;
        }
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        // 写し先は R24G8 の組で作るので、写せるのは同じ組で多重サンプルでない奥行きだけ
        const bool copyable =
            (desc.Format == DXGI_FORMAT_D24_UNORM_S8_UINT || desc.Format == DXGI_FORMAT_R24G8_TYPELESS) &&
            desc.SampleDesc.Count == 1;
        if (!copyable)
        {
            FallBackWithoutDepth("写せない奥行きの書式", desc.Width, desc.Height);
            return;
        }
        DepthCopy* copy = DepthCopyFor(desc.Width, desc.Height);
        if (copy == nullptr)
        {
            FallBackWithoutDepth("奥行きの写し先を作れなかった", desc.Width, desc.Height);
            return;
        }
        // 書き込み中の奥行きを読み込み元に重ねないため、描く直前に別のテクスチャへ写して読む
        context->CopyResource(copy->texture->Native(), source.Get());
        copy->lastUsed = m_drawCount;

        // 投影行列から奥行きを視点からの距離へ戻す値。NS は左手系の透視投影を転置せずに渡し、深度は 0 が手前で 1 が奥
        const Effekseer::Matrix44 projection = ToEffekseer(camera.Projection());
        EffekseerRenderer::DepthReconstructionParameter parameter;
        parameter.DepthBufferScale = 1.0f;
        parameter.DepthBufferOffset = 0.0f;
        parameter.ProjectionMatrix33 = projection.Values[2][2];
        parameter.ProjectionMatrix43 = projection.Values[2][3];
        parameter.ProjectionMatrix34 = projection.Values[3][2];
        parameter.ProjectionMatrix44 = projection.Values[3][3];
        m_renderer->SetDepth(copy->effekseerTexture, parameter);
        m_depthFallback = false;
        m_failedDepthKey.clear();
    }

    EffectScene::DepthCopy* EffectScene::DepthCopyFor(std::uint32_t width, std::uint32_t height) noexcept
    {
        DepthCopy* oldest = &m_depthCopies[0];
        for (DepthCopy& copy : m_depthCopies)
        {
            if (copy.texture != nullptr && copy.width == width && copy.height == height)
            {
                return &copy;
            }
            if (copy.lastUsed < oldest->lastUsed)
            {
                oldest = &copy;
            }
        }
        // 空きか、一番長く使っていない方を作り直す
        for (DepthCopy& copy : m_depthCopies)
        {
            if (copy.texture == nullptr)
            {
                oldest = &copy;
                break;
            }
        }
        *oldest = DepthCopy{};
        TextureDesc desc{};
        desc.width = width;
        desc.height = height;
        desc.format = DXGI_FORMAT_R24G8_TYPELESS;
        desc.viewFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        desc.bindFlags = D3D11_BIND_SHADER_RESOURCE;
        std::unique_ptr<Texture> texture = Texture::Create(desc);
        if (texture == nullptr || texture->Srv() == nullptr)
        {
            return nullptr;
        }
        const Effekseer::Backend::TextureRef effekseerTexture =
            EffekseerRendererDX11::CreateTexture(m_renderer->GetGraphicsDevice(), texture->Srv(), nullptr, nullptr);
        if (effekseerTexture == nullptr)
        {
            return nullptr;
        }
        ++m_depthCopiesCreated;
        oldest->texture = std::move(texture);
        oldest->effekseerTexture = effekseerTexture;
        oldest->width = width;
        oldest->height = height;
        return oldest;
    }

    void EffectScene::FallBackWithoutDepth(std::string_view reason, std::uint32_t width, std::uint32_t height) noexcept
    {
        // 前の奥行きを残すと、別の描画先の物で薄くなる
        m_renderer->SetDepth(nullptr, EffekseerRenderer::DepthReconstructionParameter{});
        m_depthFallback = true;
        std::string key = std::format("{} {}x{}", reason, width, height);
        if (key == m_failedDepthKey)
        {
            return;
        }
        NS_LOG_ERROR(Graphics, "EffectScene: {} ({}x{})。奥行き無しでエフェクトを描く", reason, width, height);
        m_failedDepthKey = std::move(key);
    }
} // namespace NS::Gfx
