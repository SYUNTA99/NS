#pragma once

/// @file Layer.h
/// @brief NS::App::Layer — Application::Run が反復駆動する処理単位の基底
///
/// @details Game / Editor / Debug HUD を独立した Layer として並立させる基盤
/// イベントクラス階層は持たず、入力は Application::Get()->Input() のポーリングで参照する
/// active フラグは各 Layer の自己判断用 (pause overlay / Edit-Play 切替等で
/// SetActive(false) にすると OnUpdate / OnRender がスキップされる)
///
/// Run ループ内から PushLayer / Pop を呼ぶことは禁止 (iterator 無効化)
/// layer 構成は WinMain 段階で確定させる。動的変更が必要なら deferred queue を導入する

#include <string>
#include <string_view>
#include <utility>

namespace NS::App
{

    /// Application::Run が反復駆動する処理単位の基底。SetActive(false) で Update/Render をスキップ
    class Layer
    {
    public:
        explicit Layer(std::string name = "Layer") noexcept : m_name(std::move(name)) {}
        virtual ~Layer() = default;

        Layer(const Layer&) = delete;
        Layer& operator=(const Layer&) = delete;
        Layer(Layer&&) = delete;
        Layer& operator=(Layer&&) = delete;

        /// Application::Run の Init フェーズで一度呼ばれる (LayerStack 反復)
        virtual void OnAttach() {}

        /// Application::Run の Shutdown フェーズで一度呼ばれる (LayerStack 逆順反復)
        virtual void OnDetach() {}

        /// fixed step ループ内で 1 step あたり 1 回呼ばれる。 dt は FrameTimer::FixedDelta()
        virtual void OnUpdate() {}

        /// variable frame の render フェーズで 1 frame あたり 1 回呼ばれる
        virtual void OnRender() {}

        [[nodiscard]] std::string_view Name() const noexcept { return m_name; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }
        void SetActive(bool active) noexcept { m_active = active; }

    private:
        std::string m_name;
        bool m_active = true;
    };

} // namespace NS::App
