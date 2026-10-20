#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/CoreObject/Reflection/Curve.h"
#include "NSlib/Gfx/Render/FrameConstants.h"

namespace NS
{
    //! @brief 描く形だけの揺れの形
    enum class DrawShakeShape
    {
        Side,  //!< 左右へ振る横揺れ。BodyShakeOffset
        Depth, //!< 押す向きへ押しては戻す奥揺れ。DepthShakeOffset
    };

    //! @brief 描く形だけを axis の向きへ OffsetAt のずれで揺らす。Model::StartDrawShake で頼む
    struct DrawShakeDesc
    {
        DrawShakeShape shape = DrawShakeShape::Side;
        Vector3 axis{1.0f, 0.0f, 0.0f}; //!< 揺らす世界の長さ 1 の向き
        float amplitude = 0.0f;         //!< 最初の振れ幅 (m)
        int length = 0;                 //!< 揺れのフレーム数
        uint32 seed = 0;                //!< 振れ幅のばらつきの種
        float firstSign = 1.0f;         //!< 横の 1 フレーム目の向き
        int flipFrames = 1;             //!< 横の左右を入れ替えるフレーム数
        int pushFrames = 1;             //!< 奥の押し直すフレーム数
        float returnRatio = 0.0f;       //!< 奥の押してから 1 フレームごとの倍率

        //! @brief frame フレーム目の、描く形の世界のずれを返す
        //! @param[in] frame 揺れの何フレーム目か。始まりのフレームが 1
        //! @return ずれ。frame が 1 より前、横は length 以降、奥は length より後で 0 ベクトル
        [[nodiscard]] Vector3 OffsetAt(int frame) const noexcept;
    };

    //! @brief 衝突点から体の裏へ伝わる頂点の震え。Model::StartDrawTremor で頼む
    struct DrawTremorDesc
    {
        Vector3 contactOffset{};        //!< 衝突点の、根の位置からのずれ (m)。体と一緒に動く
        float amplitudePixels = 0.0f;   //!< 振れ幅。高さ referenceHeight ピクセルの画面の上のピクセル数
        float amplitudeRatio = 0.0f;    //!< 振れ幅の体の半径に対する割合。正ならピクセルの欄より先に使う
        int reachFrames = 0;            //!< 衝突点から体の一番遠い所へ届くまでのフレーム数
        int length = 0;                 //!< 震えのフレーム数
        float referenceHeight = 720.0f; //!< ピクセルの寸法を決めた基準の画面の高さ
        float bodyLength = 0.0f;        //!< 衝突点から裏まで届く長さ (m)。0 以下なら全体が同時に震え始める
    };

    //! @brief 描く倍率の曲線の再生。Model::StartDrawShape で頼む
    //! @details 曲線の横軸は始めたフレームを 0 にしたフレーム数。点の無い曲線は 1
    struct DrawShapeDesc
    {
        Curve along{};             //!< alongDirection の向きの倍率
        Curve height{};            //!< 高さの倍率
        Curve side{};              //!< alongDirection に直角な水平の向きの倍率
        Vector3 alongDirection{};  //!< 水平の長さ 1 の向き。0 ベクトルなら水平の倍率は掛けない
        int length = 0;            //!< 再生するフレーム数

        //! @brief frame フレーム目の世界の軸ごとの倍率を返す
        //! @details 水平の向きの成分の 2 乗で along と side を x と z に混ぜる。軸に沿う向きでは元の倍率のまま
        [[nodiscard]] Vector3 ScaleAt(int frame) const noexcept;
    };

    //! @brief 横揺れの、frame フレーム目の振れ幅の包みを返す
    //! @details 振れ幅 × (1 − frame ÷ length)。BodyShakeOffset の大きさはこれを超えない
    //! @param[in] frame 揺れの何フレーム目か。始まりのフレームが 1
    //! @param[in] length 揺れのフレーム数。0 以下は揺らさない
    //! @return 包み。frame が 1 より前か length 以降は 0
    [[nodiscard]] float BodyShakeReach(int frame, int length, float amplitude) noexcept;

    //! 横揺れの符号付きのずれを返す。BodyShakeReach の範囲で弱め、同じ seed・frame では同じ値
    //! frame は開始を 1 とする。1 未満か length 以降、length が非正なら 0
    //! firstSign は負なら −、他は +。flipFrames ごとに左右を反転し、1 未満は 1 に補正する
    [[nodiscard]] float
        BodyShakeOffset(int frame, int length, float amplitude, uint32 seed, float firstSign, int flipFrames) noexcept;

    //! 押して戻す揺れのずれを返す。同じ seed と押し番号では同じばらつき
    //! frame は開始を 1 とする。1 未満か length より後、length が非正なら 0
    //! pushFrames は 1 以上に補正する。returnRatio は押した後の毎フレームの減衰倍率
    [[nodiscard]] float
        DepthShakeOffset(int frame, int length, float depth, int pushFrames, float returnRatio, uint32 seed) noexcept;

    //! 高さ referenceHeight 基準のピクセル数を、世界位置 at の奥行きで m に変換する
    //! at がカメラの後ろなら距離で測る。基準高さ・カメラの向きが不正なら 0
    [[nodiscard]] float ScreenPixelsToMeters(float pixels,
                                             const CameraPose& pose,
                                             const Vector3& at,
                                             float referenceHeight = 720.0f) noexcept;

    //! 描画用の震えを作る。elapsedFrames は開始を 0 とする経過、root は現在の根の位置
    //! 正の振幅割合をピクセル指定より優先する。時間外か震える期間が無ければ振幅 0
    [[nodiscard]] Gfx::TremorCB
        MakeTremor(const DrawTremorDesc& desc, int elapsedFrames, const Vector3& root, const CameraPose& pose) noexcept;

    //! 震えの全項目が有限の場合 true。有限でない震えを描くと形が消える
    [[nodiscard]] bool IsFiniteTremor(const Gfx::TremorCB& tremor) noexcept;
} // namespace NS
