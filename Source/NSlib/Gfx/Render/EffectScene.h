#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"
#include "NSlib/Gfx/Render/EffectTypes.h"
#include "NSlib/Gfx/Render/SceneDepthCopy.h"

#pragma warning(push, 0)
#include <Effekseer.h>
#include <EffekseerRendererCommon/EffekseerRenderer.Renderer.h>
#pragma warning(pop)

#include <array>
#include <string>
#include <string_view>
#include <unordered_map>

namespace NS
{
    class CameraData;
} // namespace NS

namespace NS
{
    namespace Gfx
    {
        class D3DEffect;
        class D3DTexture;

        //! Effekseer の再生と描画。D3DDevice の構築後に作り、D3DEffect を渡して再生する
        class EffectScene : public NonCopyable
        {
        public:
            EffectScene() noexcept;
            ~EffectScene();

            [[nodiscard]] bool IsValid() const noexcept;

            //! エフェクトを再生し、操作用のハンドルを返す
            [[nodiscard]] EffectHandle Play(const D3DEffect* effect, const EffectPlayDesc& desc) noexcept;

            //! ワールド座標の姿勢を設定する (次の Update で反映)
            void SetTransform(EffectHandle handle,
                              const Vector3& position,
                              const Quaternion& rotation,
                              const Vector3& scale) noexcept;

            //! 動的入力 (index 0〜3) を設定する。生成時の初期値は最初の Update 前に設定すること
            void SetDynamicInput(EffectHandle handle, int index, float value) noexcept;

            //! 子も含めて次の Update で消去する (deltaSeconds == 0.0f の Update でも反映)
            void Stop(EffectHandle handle) noexcept;

            //! 新規生成を止め、既存のパーティクルは寿命まで残す
            void StopRoot(EffectHandle handle) noexcept;

            void StopAll() noexcept;

            [[nodiscard]] bool Exists(EffectHandle handle) const noexcept;

            //! deltaSeconds == 0.0f の場合は時間を進めず Play と Stop のみ反映し、負値は何もしない
            void Update(float deltaSeconds) noexcept;

            //! 全エフェクトを描画する。D24_UNORM_S8_UINT の深度をコピーできない場合は深度なしで描画する
            //! timeSeconds はシェーダーへ渡す経過秒
            void Draw(const CameraData& camera, float timeSeconds) noexcept;

            //! 直前の Draw でエフェクトへ深度を渡せたか
            [[nodiscard]] bool PassesDepth() const noexcept;

            //! 直前の Draw で深度コピー不可により深度なし描画へフォールバックしたか
            [[nodiscard]] bool IsUsingDepthFallback() const noexcept { return m_depthFallback; }

            [[nodiscard]] uint32 DepthCopiesCreated() const noexcept { return m_depth.CopiesCreated(); }

        private:
            //! SceneDepthCopy のテクスチャを Effekseer へ渡す形にした物
            struct DepthTextureRef
            {
                uint32 textureId = 0;
                Effekseer::Backend::TextureRef effekseerTexture;
            };

            void PassDepth(const CameraData& camera) noexcept;
            [[nodiscard]] Effekseer::Backend::TextureRef DepthTextureFor(const SceneDepthCopy::CaptureResult& capture) noexcept;
            void FallBackWithoutDepth(std::string_view reason, uint32 width, uint32 height) noexcept;

            Effekseer::ManagerRef m_manager;
            EffekseerRenderer::RendererRef m_renderer;
            std::unordered_map<std::string, uint32> m_playCounts; // D3DEffect::Path() ごとの、StopAll からの再生の回数

            SceneDepthCopy m_depth;
            std::array<DepthTextureRef, 2> m_depthTextures{};
            uint32 m_nextDepthTexture = 0;
            bool m_depthFallback = false;
            std::string m_failedDepthKey;
        };
    } // namespace Gfx
} // namespace NS
