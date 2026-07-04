#include "Framework/Scene/EnvironmentSubsystem.h"

#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/Camera.h"
#include "Framework/Graphics/Skybox.h"
#include "Framework/Scene/SceneBase.h"
#include "Framework/Scene/SubsystemRegistry.h"

#include <utility>

namespace NS::Scene
{
    EnvironmentSubsystem::EnvironmentSubsystem() = default;
    EnvironmentSubsystem::~EnvironmentSubsystem() = default;

    void EnvironmentSubsystem::Initialize(SceneBase&) noexcept
    {
        // 装置だけ先に作る。cubemap のパスは設定が持ち、DrawSky の差分再読込が初回から入れる
        auto skybox = NS::Graphics::Skybox::Create();
        if (skybox && skybox->IsValid())
        {
            m_skybox = std::move(skybox);
        }
        else
        {
            NS_LOG_WARN(::NS::Core::LogCat::Graphics, "EnvironmentSubsystem: Skybox 構築失敗のため空を描かない");
        }
    }

    void EnvironmentSubsystem::Deinitialize() noexcept
    {
        m_skybox.reset();
        m_loadedSkyboxPath.clear();
    }

    void EnvironmentSubsystem::SetSettings(const EnvironmentSettings& settings)
    {
        m_settings = settings;
        m_hasSettings = true;
    }

    NS::Graphics::RenderSettingsOverride EnvironmentSubsystem::BuildOverride() const noexcept
    {
        NS::Graphics::RenderSettingsOverride over{};
        if (!m_hasSettings)
            return over;

        if (m_settings.lightDirection.LengthSquared() > 1e-6f)
        {
            over.lightDir = m_settings.lightDirection;
        }
        else if (!m_warnedZeroLightDirection)
        {
            // zero ベクトルは normalize で拡散光が無言で消えるため上書きせず既定 lightDir に落とす
            NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                        "EnvironmentSubsystem: lightDirection が zero のため既定 lightDir で描画する");
            m_warnedZeroLightDirection = true;
        }
        over.lightColor = m_settings.lightColor;
        over.ambientColor = m_settings.ambientColor;
        return over;
    }

    void EnvironmentSubsystem::DrawSky(NS::Graphics::Renderer& renderer, const NS::Graphics::Camera& camera) noexcept
    {
        if (!m_skybox || !m_skybox->IsValid())
            return;

        // 毎フレーム LoadCubemap すると I/O が常時走るため、前回パスと差分があるときだけ再ロードする
        if (!m_settings.skyboxCubemapPath.empty() && m_settings.skyboxCubemapPath != m_loadedSkyboxPath)
        {
            // ユーザー編集ファイル由来のパスを ContentRoot 配下へ閉じ込める。外を指す値は読み込まない
            const auto absPath = ::NS::Core::FileSystem::ResolveUnder(::NS::Core::FileSystem::ContentRoot(),
                                                                      m_settings.skyboxCubemapPath);
            if (!absPath.has_value())
            {
                NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                            "EnvironmentSubsystem: cubemap パス '{}' は ContentRoot 配下でないため読み込まない",
                            m_settings.skyboxCubemapPath.string());
                // 拒否はパスを直すまで変わらないので、覚えて警告の連打を止める
                m_loadedSkyboxPath = m_settings.skyboxCubemapPath;
            }
            else if (m_skybox->LoadCubemap(*absPath))
            {
                m_loadedSkyboxPath = m_settings.skyboxCubemapPath;
            }
            else
            {
                NS_LOG_WARN(::NS::Core::LogCat::Graphics,
                            "EnvironmentSubsystem: cubemap 読込失敗 ({}), 既存を維持",
                            absPath->string());
                // 失敗時は前回パスを更新しないので次フレームで再試行できる
            }
        }

        // view の平行移動成分を 0 化して camera 中心に空を固定する
        NS::Math::Matrix viewNoTranslate = camera.View();
        viewNoTranslate._41 = 0.0f;
        viewNoTranslate._42 = 0.0f;
        viewNoTranslate._43 = 0.0f;
        m_skybox->Render(renderer, viewNoTranslate * camera.Projection());
    }

    // 環境はどのシーンにも 1 つあり、設定が書かれるまで描画既定値に影響しない
    NS_REGISTER_SUBSYSTEM(EnvironmentSubsystem, SubsystemTier::Scene, [](const SceneBase&) { return true; })
} // namespace NS::Scene
