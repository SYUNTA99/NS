#pragma once

/// @file Layers.h
/// @brief NS::App::Layers — Layer 群を順序付きで保持するコンテナ
///
/// @details Application が 1 個所有し、Run ループから反復される
/// AddLayer の regular と AddOverlay の overlay を区別し、overlay は常に末尾に挿入される
/// begin → end の反復順で OnUpdate / OnRender が呼ばれ、overlay は常に後段で実行される
///
/// iterator が無効化されるため Run ループ内から AddLayer / AddOverlay / Remove を呼ぶことは禁止
/// layer 構成は WinMain 段階で確定させる。動的追加が必要なら deferred queue を導入する

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

        /// overlay より前で反復される regular layer を追加する
        void AddLayer(std::unique_ptr<Layer> layer);

        /// OnRender が最後になるよう overlay layer を regular より後ろへ追加する
        void AddOverlay(std::unique_ptr<Layer> overlay);

        /// 指定 layer を取り外して所有権を返す。見つからなければ nullptr が返る。 caller が寿命制御可
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
