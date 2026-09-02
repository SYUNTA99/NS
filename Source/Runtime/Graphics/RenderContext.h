#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/RenderSettings.h"

namespace NS::Graphics
{
    class Renderer;

    //! @brief 1フレームの描画処理全体で共有されるコンテキスト情報
    struct RenderContext
    {
        //! 描画先のレンダラー
        Renderer* renderer = nullptr;

        //! カメラのビュープロジェクション行列
        NS::Core::Matrix viewProjection{};

        //! カメラのワールド座標
        NS::Core::Vector3 cameraPosition{};

        //! 固定ステップ更新と描画のズレを埋める補間係数 (0.0 〜 1.0)
        float alpha = 1.0f;

        //! シーン全体に適用される共通の描画設定
        RenderSettings resolvedSettings{};
    };

} // namespace NS::Graphics