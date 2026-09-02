#pragma once

#include "Runtime/Graphics/FrameConstants.h"
#include "Runtime/Graphics/Pipeline.h"

#include <cstddef>

namespace NS::Graphics
{
    class Mesh;
    class Material;
    class Buffer;
    class Renderer;

    //! @brief 1回の描画に必要なデータをまとめた構造体
    // FrameCB の 16 byte 整列を値で抱えるため DrawItem に想定どおりのパディングが入る。C4324 を黙らせる
#pragma warning(push)
#pragma warning(disable : 4324)
    struct DrawItem
    {
        Mesh* mesh = nullptr;                //!< 描画する形状データ
        Material* material = nullptr;        //!< 表面の質感やシェーダ設定
        BlendMode blend = BlendMode::Opaque; //!< 合成モード
        FrameCB constants{};                 //!< オブジェクトの座標やカメラ、ライトなどの基本定数

        // 頂点シェーダへの追加データ。スキンメッシュのボーンパレットがこれを使う
        const Buffer* extraVsCb = nullptr;
        const void* extraVsData = nullptr;
        std::size_t extraVsSize = 0;
        unsigned extraVsSlot = 1;
    };
#pragma warning(pop)

    //! @brief DrawItem をもとに描画を出す
    //! @note mesh か material が空なら何もしない
    void IssueDrawItem(Renderer& renderer, const DrawItem& item) noexcept;

} // namespace NS::Graphics