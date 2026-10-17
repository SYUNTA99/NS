#pragma once

namespace NS
{
    namespace Gfx
    {
        class D3DShader;

        //! @brief 描く段が使う組み込みのシェーダー。持ち主は渡す側で、描く段は指すだけ
        //! @details 渡す側は描く段より長く持つ。読み直しは同じ D3DShader のまま中身を替えるので、
        //! 描く段は指したまま新しい中身で描く。nullptr のシェーダーを使う段は描かない
        struct BuiltinShaders
        {
            D3DShader* fullscreenVs = nullptr; //!< ScreenOverlay の全画面塗り・Bloom・ScreenPasses が共有する
            D3DShader* fadePs = nullptr;
            D3DShader* screenRectVs = nullptr;
            D3DShader* screenRectPs = nullptr;
            D3DShader* skyboxVs = nullptr;
            D3DShader* skyboxPs = nullptr;
            D3DShader* debugLineVs = nullptr;
            D3DShader* debugLinePs = nullptr;
            D3DShader* bloomThresholdPs = nullptr;
            D3DShader* bloomDownPs = nullptr;
            D3DShader* bloomUpPs = nullptr;
            D3DShader* bloomCompositePs = nullptr;
            D3DShader* screenDistortionPs = nullptr;
            D3DShader* bodyMaskPs = nullptr;
        };
    } // namespace Gfx
} // namespace NS
