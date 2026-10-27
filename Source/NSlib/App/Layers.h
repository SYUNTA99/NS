#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace NS
{

    class Layer;

    //! 通常のレイヤーの後にオーバーレイを保持するコンテナ。構成は起動時に確定する
    class Layers : public NonCopyable
    {
    public:
        Layers();
        ~Layers();

        void AddLayer(std::unique_ptr<Layer> layer);
        void AddOverlay(std::unique_ptr<Layer> overlay);

        [[nodiscard]] bool Empty() const noexcept { return m_layers.empty(); }

        [[nodiscard]] std::vector<std::unique_ptr<Layer>>::iterator begin() noexcept { return m_layers.begin(); }
        [[nodiscard]] std::vector<std::unique_ptr<Layer>>::iterator end() noexcept { return m_layers.end(); }
        [[nodiscard]] std::vector<std::unique_ptr<Layer>>::reverse_iterator rbegin() noexcept
        {
            return m_layers.rbegin();
        }
        [[nodiscard]] std::vector<std::unique_ptr<Layer>>::reverse_iterator rend() noexcept { return m_layers.rend(); }

    private:
        std::vector<std::unique_ptr<Layer>> m_layers;
        std::size_t m_overlayBegin = 0;
    };

} // namespace NS
