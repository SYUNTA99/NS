#pragma once

/// @file render_context.h
/// @brief ns::scene::RenderContext — IRenderable::Draw に渡る描画コンテキスト。
///
/// Scene::OnRender が 1 回構築し、登録された全 IRenderable に同じ参照を渡す。
/// per-frame で共有される ViewProjection / alpha ( 補間係数) / Renderer 参照を束ねる。

#include "ns/core/math.h"

namespace ns::graphics
{
    class Renderer;
}

namespace ns::scene
{

    /// 1 フレームの描画でこれ以上分割しない最小の共有状態。
    struct RenderContext
    {
        /// 描画先 Renderer。BeginFrame は Application が、各 Renderable は Bind/Draw のみ行う。
        ns::graphics::Renderer* renderer = nullptr;
        /// active CameraComponent から取り出した VP 行列 (Alpha 補間済を渡す想定)。
        ns::core::Matrix viewProjection{};
        /// fixed step 補間係数 [0,1]。Application::Alpha() を Scene が転記。
        float alpha = 1.0f;
    };

} // namespace ns::scene
