#include "NSlib/Gfx/D3DEffect.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Windows/FileSystem.h"
#include "NSlib/Core/Windows/StringUtils.h"
#include "NSlib/Gfx/D3DDevice.h"

#pragma warning(push, 0)
#include <EffekseerRendererCommon/TextureLoader.h>
#include <EffekseerRendererDX11.h>
#pragma warning(pop)

#include <string>

namespace NS
{
    namespace Gfx
    {
        namespace
        {
            std::string ToUtf8(const char16_t* path)
            {
                if (path == nullptr)
                {
                    return std::string{};
                }
                return OS::StringUtils::Utf8FromWide(reinterpret_cast<const wchar_t*>(path));
            }

            template <typename GetResource, typename GetPath>
            int CountMissing(int32 count, GetResource getResource, GetPath getPath, std::string_view kind)
            {
                int missing = 0;
                for (int32 i = 0; i < count; ++i)
                {
                    if (getResource(i) == nullptr)
                    {
                        NS_LOG_ERROR(Graphics, "D3DEffect::LoadFromFile: {}を読めなかった ({})", kind, ToUtf8(getPath(i)));
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
                    [&](int32 i) { return effect->GetColorImage(i); },
                    [&](int32 i) { return effect->GetColorImagePath(i); },
                    "テクスチャ");
                missing += CountMissing(
                    effect->GetNormalImageCount(),
                    [&](int32 i) { return effect->GetNormalImage(i); },
                    [&](int32 i) { return effect->GetNormalImagePath(i); },
                    "法線テクスチャ");
                missing += CountMissing(
                    effect->GetDistortionImageCount(),
                    [&](int32 i) { return effect->GetDistortionImage(i); },
                    [&](int32 i) { return effect->GetDistortionImagePath(i); },
                    "歪みテクスチャ");
                missing += CountMissing(
                    effect->GetModelCount(),
                    [&](int32 i) { return effect->GetModel(i); },
                    [&](int32 i) { return effect->GetModelPath(i); },
                    "モデル");
                missing += CountMissing(
                    effect->GetMaterialCount(),
                    [&](int32 i) { return effect->GetMaterial(i); },
                    [&](int32 i) { return effect->GetMaterialPath(i); },
                    "マテリアル");
                missing += CountMissing(
                    effect->GetCurveCount(),
                    [&](int32 i) { return effect->GetCurve(i); },
                    [&](int32 i) { return effect->GetCurvePath(i); },
                    "カーブ");
                return missing;
            }

            Effekseer::SettingRef CreateLoaderSetting(const D3DDevice* gpu) noexcept
            {
                const Effekseer::Backend::GraphicsDeviceRef graphicsDevice =
                    EffekseerRendererDX11::CreateGraphicsDevice(gpu->Native(), gpu->NativeContext());
                if (graphicsDevice == nullptr)
                {
                    NS_LOG_ERROR(Graphics, "D3DEffect: Effekseer の GraphicsDevice を作れなかった");
                    return nullptr;
                }

                Effekseer::SettingRef setting = Effekseer::Setting::Create();
                if (setting == nullptr)
                {
                    NS_LOG_ERROR(Graphics, "D3DEffect: Effekseer の Setting を作れなかった");
                    return nullptr;
                }

                // 読み込み時に座標系に合わせて値を反転するため、Effect::Create より先に決める
                setting->SetCoordinateSystem(Effekseer::CoordinateSystem::LH);
                setting->SetEffectLoader(Effekseer::Effect::CreateEffectLoader());
                setting->SetTextureLoader(
                    EffekseerRenderer::CreateTextureLoader(graphicsDevice, nullptr, Effekseer::ColorSpaceType::Gamma));
                setting->SetModelLoader(EffekseerRenderer::CreateModelLoader(graphicsDevice));
                setting->SetMaterialLoader(EffekseerRendererDX11::CreateMaterialLoader(graphicsDevice));
                setting->SetCurveLoader(Effekseer::MakeRefPtr<Effekseer::CurveLoader>());
                return setting;
            }
        } // namespace

        uint32 D3DEffect::DefaultSeed(std::string_view path) noexcept
        {
            const std::string stem = OS::FileSystem::Stem(path);
            uint32 hash = 2166136261u;
            for (const char ch : stem)
            {
                hash ^= static_cast<uint8>(ch);
                hash *= 16777619u;
            }
            if (hash == 0u)
            {
                return 1u;
            }
            return hash;
        }

        std::unique_ptr<D3DEffectLoader> D3DEffectLoader::Create() noexcept
        {
            // D3DDevice の無い場 (GPU の無い試し) では、ERROR を出さずに作らない
            // ゲームでは D3DDevice を先に作るので、ここで返らない
            const D3DDevice* gpu = D3DDevice::Current();
            if (gpu == nullptr || gpu->Native() == nullptr || gpu->NativeContext() == nullptr)
            {
                return nullptr;
            }
            Effekseer::SettingRef setting = CreateLoaderSetting(gpu);
            if (setting == nullptr)
            {
                return nullptr;
            }
            return std::unique_ptr<D3DEffectLoader>(new D3DEffectLoader(std::move(setting), gpu->Native()));
        }

        D3DEffectLoader::D3DEffectLoader(Effekseer::SettingRef setting, const void* nativeDevice) noexcept
            : m_setting(std::move(setting)), m_nativeDevice(nativeDevice)
        {
        }

        D3DEffectLoader::~D3DEffectLoader() = default;

        bool D3DEffectLoader::IsForCurrentDevice() const noexcept
        {
            const D3DDevice* gpu = D3DDevice::Current();
            return gpu != nullptr && gpu->Native() == m_nativeDevice;
        }

        std::unique_ptr<D3DEffect> D3DEffect::LoadFromFile(std::string_view path,
                                                           std::optional<uint32> seed,
                                                           const D3DEffectLoader* loader) noexcept
        {
            std::unique_ptr<D3DEffectLoader> ownLoader;
            if (loader == nullptr)
            {
                ownLoader = D3DEffectLoader::Create();
                loader = ownLoader.get();
            }
            if (loader == nullptr)
            {
                return nullptr;
            }
            if (!loader->IsForCurrentDevice())
            {
                NS_LOG_ERROR(Graphics, "D3DEffect::LoadFromFile: 今の D3DDevice で作っていない D3DEffectLoader が渡された");
                return nullptr;
            }
            if (path.empty())
            {
                NS_LOG_ERROR(Graphics, "D3DEffect::LoadFromFile: 空のパスが渡された");
                return nullptr;
            }
            const Effekseer::SettingRef& setting = loader->Native();

            const std::wstring pathWide = OS::StringUtils::WideFromUtf8(path);
            const std::u16string pathU16(pathWide.begin(), pathWide.end());

            Effekseer::EffectRef effect = Effekseer::Effect::Create(setting, pathU16.c_str());
            if (effect == nullptr)
            {
                NS_LOG_ERROR(Graphics,
                             "D3DEffect::LoadFromFile: エフェクトを読めなかった ({})",
                             OS::StringUtils::Utf8FromWide(pathWide));
                return nullptr;
            }
            if (CountMissingResources(effect) > 0)
            {
                return nullptr;
            }

            uint32 chosenSeed = seed.value_or(DefaultSeed(path));
            if (chosenSeed == 0u)
            {
                chosenSeed = 1u;
            }
            return std::unique_ptr<D3DEffect>(new D3DEffect(std::move(effect), chosenSeed, std::string{path}));
        }

        D3DEffect::D3DEffect(Effekseer::EffectRef effect, uint32 seed, std::string path) noexcept
            : m_effect(std::move(effect)), m_seed(seed), m_path(std::move(path))
        {
        }

        D3DEffect::~D3DEffect() = default;
    } // namespace Gfx
} // namespace NS
