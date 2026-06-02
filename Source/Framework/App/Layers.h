#pragma once

/// @file Layers.h
/// @brief NS::App::Layers — Layer 群を順序付きで保持するコンテナ
///
/// @details Application が 1 個所有し、 Run ループから iteration される。 regular layer
/// (AddLayer) と overlay (AddOverlay) を区別し、 overlay は常に末尾側に挿入される
/// 反復順 (begin → end) で OnUpdate / OnRender が呼ばれるので、 overlay は描画最後・
/// update も後段、 regular は先に処理される
///
/// @note Application::Run ループ内から AddLayer / AddOverlay / Remove を呼ぶのは禁止
///       (iterator 無効化)。 layer 構成は WinMain 段階で確定させる方針。 動的追加要件化時は
///       deferred queue 導入で対応 (本クラスの拡張)

#include <cstddef>
#include <memory>
#include <vector>

namespace NS::App
{

    class Layer;

    /// Layer 群を順序付きで保持し、 Application::Run が iteration するコンテナ
    /// regular / overlay の挿入位置は内部で区別され、 overlay は常に後段で反復される
    class Layers
    {
    public:
        Layers();
        ~Layers();

        Layers(const Layers&) = delete;
        Layers& operator=(const Layers&) = delete;
        Layers(Layers&&) = delete;
        Layers& operator=(Layers&&) = delete;

        /// regular layer を追加する (overlay より前で反復される)
        void AddLayer(std::unique_ptr<Layer> layer);

        /// overlay layer を追加する (regular より後ろ、 OnRender が最後)
        void AddOverlay(std::unique_ptr<Layer> overlay);

        /// 指定 layer を取り外して所有権を返す (見つからなければ nullptr)。 caller が寿命制御可
        std::unique_ptr<Layer> Remove(Layer* layer) noexcept;

        [[nodiscard]] std::size_t Size() const noexcept { return m_layers.size(); }
        [[nodiscard]] bool Empty() const noexcept { return m_layers.empty(); }

        [[nodiscard]] auto begin() noexcept { return m_layers.begin(); }
        [[nodiscard]] auto end() noexcept { return m_layers.end(); }
        [[nodiscard]] auto begin() const noexcept { return m_layers.begin(); }
        [[nodiscard]] auto end() const noexcept { return m_layers.end(); }
        [[nodiscard]] auto rbegin() noexcept { return m_layers.rbegin(); }
        [[nodiscard]] auto rend() noexcept { return m_layers.rend(); }

    private:
        std::vector<std::unique_ptr<Layer>> m_layers;
        std::size_t m_overlayBegin = 0;
    };

} // namespace NS::App
