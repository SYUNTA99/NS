#include "Framework/App/Layers.h"

#include "Framework/App/Layer.h"

#include <algorithm>

namespace NS::App
{

    Layers::Layers() = default;

    Layers::~Layers() = default;

    void Layers::AddLayer(std::unique_ptr<Layer> layer)
    {
        if (!layer)
            return;
        m_layers.insert(m_layers.begin() + static_cast<std::ptrdiff_t>(m_overlayBegin), std::move(layer));
        ++m_overlayBegin;
    }

    void Layers::AddOverlay(std::unique_ptr<Layer> overlay)
    {
        if (!overlay)
            return;
        m_layers.push_back(std::move(overlay));
    }

    std::unique_ptr<Layer> Layers::Remove(Layer* layer) noexcept
    {
        if (layer == nullptr)
            return nullptr;

        auto it = std::find_if(
            m_layers.begin(), m_layers.end(), [layer](const std::unique_ptr<Layer>& p) { return p.get() == layer; });
        if (it == m_layers.end())
            return nullptr;

        const std::size_t idx = static_cast<std::size_t>(it - m_layers.begin());
        std::unique_ptr<Layer> removed = std::move(*it);
        m_layers.erase(it);

        if (idx < m_overlayBegin)
            --m_overlayBegin;

        return removed;
    }

} // namespace NS::App
