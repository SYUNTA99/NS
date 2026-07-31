#pragma once

namespace NS::Editor
{
    //! Assets から資産をドラッグする時の荷札。 ドロップ元 (AssetsPanel) と受け側 (Scene ビュー) が同じ綴りで合わせる
    inline constexpr const char* k_MaterialDragType = "NS_MATERIAL";
    inline constexpr const char* k_MeshDragType = "NS_MESH";
} // namespace NS::Editor
