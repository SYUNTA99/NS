#include "Framework/Graphics/InstanceBatcher.h"

// Impl は detail/instance_batcher_impl.cpp に集中させているため、 ここでは facade のみ。
// detail に <d3d11.h> を閉じ込めて公開ヘッダから漏らさない構造 (Skybox / Mesh と同じ pImpl 流派)。

namespace NS::Graphics
{
    // Impl 本体定義は detail/instance_batcher_impl.cpp 側。 こちらは前方宣言 + facade 委譲のみで、
    // unique_ptr<Impl> の dtor は impl 側で =default する必要があり .cpp 分割は維持する。

} // namespace NS::Graphics
