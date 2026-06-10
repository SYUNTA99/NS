#pragma once

/// @file RenderContext.h
/// @brief NS::Scene::RenderContext — IRenderable::Draw に渡る描画コンテキスト
///
/// Scene::OnRender が 1 回構築し、登録された全 IRenderable に同じ参照を渡す
/// per-frame で共有される ViewProjection / alpha (補間係数) / Renderer 参照を束ねる

#include "Framework/Graphics/RenderSettings.h"
#include "Framework/Math/Math.h"

namespace NS::Graphics
{
    class Renderer;
}

namespace NS::Scene
{

    /// 1 フレームの描画でこれ以上分割しない最小の共有状態
    struct RenderContext
    {
        /// 描画先 Renderer。BeginFrame は Application 側、各 Renderable は Bind/Draw のみ
        NS::Graphics::Renderer* renderer = nullptr;
        /// active CameraComponent から取り出した VP 行列 (Alpha 補間済を渡す想定)
        NS::Math::Matrix viewProjection{};
        /// fixed step 補間係数 [0,1]。NS::Core::FrameTimer::Alpha() を Scene が転記
        float alpha = 1.0f;
        /// scene 段まで解決済の描画設定 (project 既定 ← scene override)。object 段は各 Draw で最終解決する
        NS::Graphics::RenderSettings resolvedSettings{};
    };

} // namespace NS::Scene
