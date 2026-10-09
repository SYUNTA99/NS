#pragma once

#include "NSlib/Graphics/FrameConstants.h"
#include "NSlib/Graphics/Pipeline.h"

#include <cstddef>

namespace NS::Gfx
{
    class Mesh;
    class Material;
    class Buffer;
    class Renderer;
    class Shader;

    //! @brief 1回の描画に必要なデータをまとめた構造体
    // FrameCB の 16 byte 整列を値で抱えるため DrawItem に想定どおりのパディングが入る。C4324 を黙らせる
#pragma warning(push)
#pragma warning(disable : 4324)
    struct DrawItem
    {
        Mesh* mesh = nullptr;                //!< 描画する形状データ
        Material* material = nullptr;        //!< 表面の質感やシェーダ設定
        BlendMode blend = BlendMode::Opaque; //!< 合成モード
        //! 手前の物に隠れた画素だけへ描く場合 true。その時の合成は blend に関わらず半透明で、深度は書かない
        bool occludedOnly = false;
        bool twoSided = false; //!< 裏からも描く場合 true。偽なら裏を向いた面は描かない
        FrameCB constants{};   //!< オブジェクトの座標やカメラ、ライトなどの基本定数

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

    //! @brief DrawItem の形を、渡したパイプラインとピクセルシェーダで描く
    //! @details 頂点シェーダと定数は material の物を使うので、骨で動く形も同じ姿勢で描ける
    //! mesh か material が空なら何もしない
    //! @param[in] pixelShader material のピクセルシェーダの代わりに差す物。入力は SV_POSITION から始める
    void IssueDrawItemWith(Renderer& renderer,
                           const DrawItem& item,
                           const Pipeline& pipeline,
                           const Shader& pixelShader) noexcept;

} // namespace NS::Gfx