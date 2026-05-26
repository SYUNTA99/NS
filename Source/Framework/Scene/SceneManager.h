#pragma once

/// @file SceneManager.h
/// @brief NS::Scene::SceneManager — SceneBase の lifecycle を管理する単一 scene ホルダ。
///
/// @details 「ロード中の scene」 1 つを保持し、 OnStart / OnUpdate / OnRender / OnShutdown
/// を Application から呼ばれた時にフォワードする。 intent API は LoadScene のみ — 内部 data
/// structure (現在は単一 unique_ptr) は外から見えない設計。
///
/// 将来 modal scene (pause menu 等) が確定要件化したら OpenModalScene / CloseModalScene の
/// 専用 API を追加して内部を stack 化する。 今は YAGNI で単一保持に閉じる。

#include <memory>

namespace NS::Scene
{

    class SceneBase;

    /// SceneBase の lifecycle を管理する単一 scene ホルダ。 LoadScene で切替える。
    class SceneManager
    {
    public:
        SceneManager();
        ~SceneManager();

        SceneManager(const SceneManager&) = delete;
        SceneManager& operator=(const SceneManager&) = delete;
        SceneManager(SceneManager&&) = delete;
        SceneManager& operator=(SceneManager&&) = delete;

        /// 現 scene を OnShutdown してから新 scene を OnStart。
        /// nullptr 渡しで「scene 無し」 状態に。
        void LoadScene(std::unique_ptr<SceneBase> scene);

        /// 現在 active な scene。 未ロードなら nullptr。
        [[nodiscard]] SceneBase* Current() noexcept;
        [[nodiscard]] const SceneBase* Current() const noexcept;

        /// 現在 active な scene が存在するか。
        [[nodiscard]] bool HasScene() const noexcept;

        /// 現 scene の OnUpdate にフォワード。 未ロードなら no-op。
        void Update();
        /// 現 scene の OnRender にフォワード。 未ロードなら no-op。
        void Render();

    private:
        std::unique_ptr<SceneBase> m_current;
    };

} // namespace NS::Scene
