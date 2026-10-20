#pragma once

#include "NSlib/Gfx/Render/DrawItem.h"

#include <optional>

namespace NS
{
    class Object;
    namespace Gfx
    {
        class D3DRenderTarget;
    }
    namespace Editor
    {
        struct AssetPreviewPose
        {
            float yaw = 0.45f;
            float pitch = -0.15f;
            float zoom = 1.0f;
        };

        //! 材質は球、モデルは全体が収まる形状表示を組む。専用の骨入力が必要な材質などはnullopt
        [[nodiscard]] std::optional<Gfx::DrawItem> MakeAssetPreviewItem(
            const Object& asset, const AssetPreviewPose& pose = {});
        //! 別の画像へ描き、バックバッファへ戻す。次フレームのシーン描画先は変更しない
        [[nodiscard]] bool RenderAssetPreview(Gfx::D3DDevice& device,
                                               Gfx::D3DRenderTarget& target,
                                               const Gfx::DrawItem& item) noexcept;
    }
}
