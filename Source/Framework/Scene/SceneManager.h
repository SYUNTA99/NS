#pragma once

/// @file SceneManager.h
/// @brief NS::Scene::SceneManager — SceneBase の lifecycle を管理する単一 scene ホルダ
///
/// @details 「ロード中の scene」 1 つを保持し、 OnStart / OnUpdate / OnRender / OnShutdown
/// を Application から呼ばれた時にフォワードする。 intent API は LoadScene のみ — 内部 data
/// structure は現在は単一 unique_ptr で外から見えない設計
///
/// 将来 pause menu 等の modal scene が確定要件化したら OpenModalScene / CloseModalScene の
/// 専用 API を追加して内部を stack 化する。 今は YAGNI で単一保持に閉じる

#include <memory>

namespace NS::Scene
{

    class SceneBase;
    class ISubsystemProvider;

    /// SceneBase の生存期間を管理する単一 scene ホルダ。LoadScene で切り替える
    class SceneManager
    {
    public:
        SceneManager();
        ~SceneManager();

        SceneManager(const SceneManager&) = delete;
        SceneManager& operator=(const SceneManager&) = delete;
        SceneManager(SceneManager&&) = delete;
        SceneManager& operator=(SceneManager&&) = delete;

        /// app tier service の解決口を登録する。以降 LoadScene する scene へ引き継がれる
        void SetSubsystemProvider(ISubsystemProvider* provider) noexcept;

        /// 現 scene を OnShutdown 後に新 scene を OnStart。nullptr で scene 無し状態へ
        /// 新 scene は OnStart 直前に scene tier service を生成し、旧 scene は OnShutdown 後に破棄する
        void LoadScene(std::unique_ptr<SceneBase> scene);

        /// 現在 active な scene。 未ロードなら nullptr
        [[nodiscard]] SceneBase* Current() noexcept;
        [[nodiscard]] const SceneBase* Current() const noexcept;

        /// 現在 active な scene が存在するか
        [[nodiscard]] bool HasScene() const noexcept;

        /// 現 scene の OnUpdate にフォワード。 未ロードなら何もしない
        void Update();
        /// 現 scene の OnRender にフォワード。 未ロードなら何もしない
        void Render();

    private:
        std::unique_ptr<SceneBase> m_current;
        /// app tier service の解決口で非所有。LoadScene 時に新 scene へ渡す
        ISubsystemProvider* m_provider = nullptr;
    };

} // namespace NS::Scene
