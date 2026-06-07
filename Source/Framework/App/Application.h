#pragma once

/// @file Application.h
/// @brief NS::App::Application — Engine layer。 Window/Renderer/Input/Audio + Layers を所有しメインループを駆動
///
/// @details Layered Architecture (Application → Layers → Layer → SceneManager → Scene)
/// の Application 部。 サブシステム (Window/Renderer/Input、 Audio は placeholder) を RAII 所有し、
/// `Run()` で Init → MainLoop → Shutdown を Template Method として実行する。 Layer 群は WinMain 段階
/// で PushLayer / PushOverlay により積み、 Run ループ内では追加/削除しない (iterator 無効化、 動的 Push 対応は別
/// task)
///
/// 多重起動禁止 (s_instance 単一保持で assert)。 構築失敗時は `IsValid() == false` を返し、
/// `Run` は -1 で即終了。 Static accessor (`Get` / `Quit`) は未構築時に nullptr を返す no-throw 設計
/// 時刻や経過時間、補間 alpha が要るなら `NS::Core::FrameTimer` を直接呼ぶ

#include "Framework/Graphics/Renderer.h"
#include "Framework/Platform/Window.h"

#include <memory>

namespace NS::App
{
    class Layer;
}

namespace NS::Platform
{
    class Input;
}

namespace NS::UI
{
    class ImGuiContext;
}

namespace NS::App
{

    /// Application 構築パラメータ
    /// Window / Renderer の Desc を内包し、メインループの固定 delta + クリアカラーも持つ
    struct ApplicationDesc
    {
        NS::Platform::WindowDesc window{};
        NS::Graphics::RendererDesc renderer{};
        /// 固定 Update の delta 秒。デフォルト 1/60
        float fixedDelta = 1.0f / 60.0f;
        /// BeginFrame のクリアカラー (RGBA)。Application が毎フレーム適用する
        float clearR = 0.10f;
        float clearG = 0.10f;
        float clearB = 0.15f;
        float clearA = 1.0f;
    };

    /// Engine layer。 Subsystem (Window/Renderer/Input/Audio) を RAII 所有、 Layers で Layer 群を駆動
    /// Static: Get / Quit でグローバルアクセサ提供 (時刻系は `NS::Core::FrameTimer::*` を直接呼ぶ)
    /// 多重起動禁止 (s_instance 単一保持で assert)
    class Application
    {
    public:
        struct Impl;

        explicit Application(const ApplicationDesc& desc);
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        /// 構築成功判定。Window / Renderer のいずれかが失敗していたら false
        [[nodiscard]] bool IsValid() const noexcept;

        /// regular layer を追加する (overlay より前)。 Application::Run の前に呼ぶこと
        /// Run ループ内からの呼出は iterator 無効化のため禁止
        void AddLayer(std::unique_ptr<NS::App::Layer> layer);

        /// overlay layer を追加する (regular より後ろ、 OnRender が最後)
        /// Run ループ内からの呼出は iterator 無効化のため禁止
        void AddOverlay(std::unique_ptr<NS::App::Layer> overlay);

        /// Init → MainLoop → Shutdown を順に呼ぶ Template Method
        /// IsValid()==false / layer が 1 個も追加されてないなら -1 を返して即終了
        int Run();

        [[nodiscard]] NS::Platform::Window& Window() noexcept;
        [[nodiscard]] NS::Graphics::Renderer& Renderer() noexcept;
        [[nodiscard]] NS::Platform::Input& Input() noexcept;

        /// Debug / Development build のみ実体を持つ ImGuiContext
        /// GameDebug / GameRelease では nullptr (ImGui 非搭載 shipping を保証)
        /// Editor 層が WantCaptureMouse / WantCaptureKeyboard で UI 排他制御に使う
        [[nodiscard]] NS::UI::ImGuiContext* ImGui() noexcept;

        /// 現在の Application インスタンス。未構築時は nullptr
        [[nodiscard]] static Application* Get() noexcept;
        /// 次フレームの MainLoop ループ抜け要求。Get() が nullptr の場合は何もしない
        static void Quit() noexcept;

    private:
        void Init();
        void MainLoop();
        void Shutdown();

        std::unique_ptr<Impl> m_pImpl;
        static Application* s_instance;
    };

    /// Game 側で実装必須。 ApplicationDesc を game 固有設定で組んで Application を構築する
    /// CreateInitialScene は廃止 (2026-05-26)、 Layer 構成は WinMain 側で PushLayer して指定する
    [[nodiscard]] std::unique_ptr<Application> CreateApplication();

} // namespace NS::App
