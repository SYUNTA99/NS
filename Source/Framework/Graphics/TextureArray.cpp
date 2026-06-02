#include "Framework/Graphics/TextureArray.h"

// Impl 本体は detail/texture_array_impl.cpp に集中させているため、 ここでは facade のみ
// detail に <d3d11.h> を閉じ込めて公開ヘッダから漏らさない構造 (Texture / Skybox と同じ pImpl 流派)

namespace NS::Graphics
{
    // Impl 本体定義は detail/texture_array_impl.cpp 側。 unique_ptr<Impl> のデストラクタが
    // 完全型を要求するため .cpp 分割を維持し、 ここでは facade 委譲のみを置く

} // namespace NS::Graphics
