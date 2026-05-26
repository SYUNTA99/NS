#pragma once

/// @file Layer.h
/// @brief NS::App::Layer — Application::Run が反復駆動する処理単位の基底。
///
/// @details Hazel Engine 流の Layered Architecture (Application → LayerStack → Layer)
/// の Layer 部。  で Game / Editor / Debug HUD を独立した Layer として並立
/// させる基盤。 Event class 階層は導入せず、 入力は Application::Get()->Input() の
/// polling で参照する。 active flag は各 Layer の自己判断用 (pause overlay / Edit-Play
/// toggle 等で SetActive(false) して OnUpdate / OnRender をスキップ)。
///
/// @note Application::Run ループ内から PushLayer / Pop を呼ぶことは禁止
///       (iterator 無効化)。 layer 構成は WinMain 段階で確定させる方針。
///       動的 Push が要件化したら LayerStack に deferred queue を導入する。

#include <string>
#include <string_view>
#include <utility>

namespace NS::App
{

    /// Application::Run が反復駆動する処理単位の基底。
    /// OnAttach → OnUpdate (fixed step) × N → OnRender (variable) → OnDetach の lifecycle。
    /// 派生は m_active を SetActive(false) で off にすると OnUpdate / OnRender がスキップされる。
    class Layer
    {
    public:
        explicit Layer(std::string name = "Layer") noexcept : m_name(std::move(name)) {}
        virtual ~Layer() = default;

        Layer(const Layer&) = delete;
        Layer& operator=(const Layer&) = delete;
        Layer(Layer&&) = delete;
        Layer& operator=(Layer&&) = delete;

        /// Application::Run の Init フェーズで一度呼ばれる (LayerStack 反復)。
        virtual void OnAttach() {}

        /// Application::Run の Shutdown フェーズで一度呼ばれる (LayerStack 逆順反復)。
        virtual void OnDetach() {}

        /// fixed step ループ内で 1 step あたり 1 回呼ばれる。 dt は FrameTimer::FixedDelta()。
        virtual void OnUpdate() {}

        /// variable frame の render フェーズで 1 frame あたり 1 回呼ばれる。
        virtual void OnRender() {}

        [[nodiscard]] std::string_view Name() const noexcept { return m_name; }
        [[nodiscard]] bool IsActive() const noexcept { return m_active; }
        void SetActive(bool active) noexcept { m_active = active; }

    private:
        std::string m_name;
        bool m_active = true;
    };

} // namespace NS::App
