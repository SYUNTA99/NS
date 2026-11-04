#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"

#include <array>
#include <memory>
#include <string_view>

namespace NS
{
    namespace Gfx
    {
        class D3DTexture;

        //! 今の描画先の深度をシェーダーから読めるテクスチャへ写す
        //! ビューごとに大きさが違うので、大きさごとに 2 枚まで持ち回す
        class SceneDepthCopy : public NonCopyable
        {
        public:
            struct CaptureResult
            {
                const D3DTexture* texture = nullptr; // 写したテクスチャ。写せなかった時と描画先に深度が無い時は nullptr
                uint32 textureId = 0;                // テクスチャを作り直すと変わる番号。写せなかった時は 0
                std::string_view failure{};          // 写せなかった理由。描画先に深度が無いだけの時は空
                uint32 width = 0;
                uint32 height = 0;
            };

            SceneDepthCopy() noexcept;
            ~SceneDepthCopy();

            //! 今の描画先の深度を写す。結果の texture は次の Capture まで有効
            [[nodiscard]] CaptureResult Capture() noexcept;

            //! 作ったテクスチャの数
            [[nodiscard]] uint32 CopiesCreated() const noexcept { return m_copiesCreated; }

        private:
            struct Copy
            {
                std::unique_ptr<D3DTexture> texture;
                uint32 id = 0;
                uint32 width = 0;
                uint32 height = 0;
                uint32 lastUsed = 0;
            };

            [[nodiscard]] Copy* CopyFor(uint32 width, uint32 height) noexcept;

            std::array<Copy, 2> m_copies{};
            uint32 m_copiesCreated = 0;
            uint32 m_captureCount = 0;
        };
    } // namespace Gfx
} // namespace NS
