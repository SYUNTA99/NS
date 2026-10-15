#pragma once

namespace NS
{
    namespace Editor
    {
        //! Assets から資産をドラッグする時の受け渡しデータ。ドロップ元 (AssetsPanel) と受け側 (Scene ビュー)
        //! が同じ綴りで合わせる
        inline constexpr const char* k_MaterialDragType = "NS_MATERIAL";
        inline constexpr const char* k_MeshDragType = "NS_MESH";
    } // namespace Editor
} // namespace NS
