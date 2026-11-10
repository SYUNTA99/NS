#pragma once

#include "NSlib/Gfx/Render/DrawConstants.h"
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

        //! 見本の描く 1 回と、見本を見るビューの値の組
        struct AssetPreviewItem
        {
            Gfx::DrawItem item;
            Gfx::ViewConstants view;
        };

        //! 材質は球、メッシュとアクターは全体が収まる形状表示を組む。
        //! 骨付きアクターは呼ぶ前に骨の描画定数を準備し、描画まではアクターを保持する。
        //! 描く形がない場合や、球に骨入力専用の材質を使う場合は nullopt
        [[nodiscard]] std::optional<AssetPreviewItem> MakeAssetPreviewItem(
            const Object& asset, const AssetPreviewPose& pose = {});
        //! 別の画像へ描き、バックバッファへ戻す。次フレームのシーン描画先は変更しない
        [[nodiscard]] bool RenderAssetPreview(Gfx::D3DDevice& device,
                                               Gfx::D3DRenderTarget& target,
                                               const AssetPreviewItem& preview) noexcept;
    }
}
