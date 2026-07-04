#pragma once

/// @file EnvironmentSubsystem.h
/// @brief NS::Scene::EnvironmentSubsystem — シーン環境の設定値と消費機構を束ねるシーン service
///
/// @details 設定が主で装置は従。テーマ等の出所が毎フレーム SetSettings で設定を書き、
/// 描画側は BuildOverride と DrawSky だけを使う。skybox 装置は Initialize で生成し、
/// device 不在なら装置を持たないまま各操作が何もしない
/// 依存: NS::Scene::SceneSubsystem, NS::Graphics::RenderSettings, NS::Graphics::Skybox

#include "Framework/Graphics/RenderSettings.h"
#include "Framework/Math/Math.h"
#include "Framework/Scene/SceneSubsystem.h"

#include <filesystem>
#include <memory>

namespace NS::Graphics
{
    class Camera;
    class Renderer;
    class Skybox;
} // namespace NS::Graphics

namespace NS::Scene
{
    /// シーン環境の設定値。テーマや将来のレベル個別上書きがここへ書き、描画側はここだけを読む
    struct EnvironmentSettings
    {
        /// 平行光の向き。正規化前で良く、zero は上書きなし扱いで既定へ落ちる
        NS::Math::Vector3 lightDirection{-0.3f, -1.0f, -0.2f};
        NS::Math::Vector3 lightColor{1.0f, 1.0f, 1.0f};
        NS::Math::Vector3 ambientColor{0.2f, 0.2f, 0.2f};
        /// ContentRoot 配下の相対パス。空文字なら cubemap の読込を行わない
        std::filesystem::path skyboxCubemapPath{};
    };

    /// 環境設定と skybox 装置を所有するシーン service。設定が書かれるまで描画既定値に影響しない
    class EnvironmentSubsystem final : public SceneSubsystem
    {
    public:
        EnvironmentSubsystem();
        ~EnvironmentSubsystem() override;

        /// skybox 装置を生成する。device 不在なら装置を持たず DrawSky は何もしない
        void Initialize(SceneBase& scene) noexcept override;

        /// skybox 装置と読込済みパスの控えを畳む
        void Deinitialize() noexcept override;

        /// 環境の出所が毎フレーム書く。以降の BuildOverride は設定値を宣言する
        void SetSettings(const EnvironmentSettings& settings);
        [[nodiscard]] const EnvironmentSettings& Settings() const noexcept { return m_settings; }

        /// 設定から scene 上書きを作る。zero の lightDirection は上書きせず既定へ落とし一度だけ警告する
        /// 設定が一度も書かれていなければ空を返し、project 既定値がそのまま残る
        [[nodiscard]] NS::Graphics::RenderSettingsOverride BuildOverride() const noexcept;

        /// 設定のパスが前回と違えば cubemap を読み直し、camera 中心固定で空を描く
        /// パスは ContentRoot 配下の相対のみ許可。装置未初期化と読込失敗は既存表示を維持する
        void DrawSky(NS::Graphics::Renderer& renderer, const NS::Graphics::Camera& camera) noexcept;

        /// 直近の scene 段解決値。SceneBase が解決時に格納し editor の由来表示が読む
        [[nodiscard]] const NS::Graphics::RenderSettings& LastResolved() const noexcept { return m_lastResolved; }
        /// scene 段解決値の控えを更新する。SceneBase::ResolveSceneSettings が解決の度に呼ぶ
        void SetLastResolved(const NS::Graphics::RenderSettings& resolved) noexcept { m_lastResolved = resolved; }

    private:
        EnvironmentSettings m_settings{};
        /// SetSettings が一度でも呼ばれたか。書かれるまで BuildOverride は空を返す
        bool m_hasSettings = false;
        NS::Graphics::RenderSettings m_lastResolved{};
        std::unique_ptr<NS::Graphics::Skybox> m_skybox;
        /// 差分フレームのみ cubemap を再ロードするため前回パスを保持する
        std::filesystem::path m_loadedSkyboxPath{};
        /// 警告の 1 回制御。const の BuildOverride から書くため mutable
        mutable bool m_warnedZeroLightDirection = false;
    };
} // namespace NS::Scene
