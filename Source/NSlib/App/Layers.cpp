#include "NSlib/App/Layers.h"

#include "NSlib/App/Layer.h"

namespace NS
{

    Layers::Layers() = default;

    Layers::~Layers() = default;

    void Layers::AddLayer(std::unique_ptr<Layer> layer)
    {
        if (!layer)
        {
            return;
        }

        m_layers.insert(m_layers.begin() + static_cast<std::ptrdiff_t>(m_overlayBegin), std::move(layer));
        ++m_overlayBegin;
    }

    void Layers::AddOverlay(std::unique_ptr<Layer> overlay)
    {
        if (!overlay)
        {
            return;
        }
        m_layers.push_back(std::move(overlay));
    }

} // namespace NS
