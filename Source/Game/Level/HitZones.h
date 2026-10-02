#pragma once

#include "Game/Level/HitTier.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Components/HitSensor.h"

namespace NS::Game::Level
{
    //! @brief 相手の面の赤 1 つと、赤の外の威力の倍率
    //! @details 面の上の位置 (左右 u・上下 v、どちらも -1〜1) のうち赤が覆う所を、形・広さ・位置で決める
    //! 置物の部品 HitZones が持ち、体当たりの答えにこの写しを載せて渡す
    struct HitFace
    {
        bool round = true; //!< 丸か。偽なら箱
        // 前の本人の指定「真ん中 0.5 m」を、半径 0.5 の相手と半径 0.65 の自機で割合に直した値。0.5 ÷ 1.15 ≒ 0.43
        float width = 0.43f;  //!< 面全体に対する赤の左右の半分の幅の割合。0〜1
        float height = 0.43f; //!< 面全体に対する赤の上下の半分の幅の割合。0〜1
        float centerU = 0.0f; //!< 赤の中心の左右の位置。-1〜1、自機から見て右が正
        float centerV = 0.0f; //!< 赤の中心の上下の位置。-1〜1、上が正
        // 赤の当たりは威力を減らさない。今までの「突進位置係数カーブ」の中心の値
        float powerScale = 1.0f; //!< 赤で当たった時の当たり位置の係数
        // かすめた当たりの威力を 3 割落とす。今までの「突進位置係数カーブ」の端の値
        float remainderPowerScale = 0.7f; //!< 赤の外 (外れ) の当たり位置の係数
    };

    //! @brief JudgeHitFace の結果
    struct HitFaceJudgement
    {
        HitTier tier = HitTier::Wide; //!< 当てはまった決まりの段。どの決まりにも当てはまらなければ外れ
        float powerScale = 1.0f;      //!< 当てはまった決まりの威力の倍率。どれにも当てはまらなければ残りの威力の倍率
        float u = 0.0f;               //!< 面の上の左右の位置。自機から見て右が正。-1〜1 が触れられる幅
        float v = 0.0f;               //!< 面の上の上下の位置。上が正。-1〜1 が触れられる高さ
        float offset01 = 1.0f;        //!< 横ずれ。u の大きさを 0〜1 に丸めた値
        float ratio = 1.0f;           //!< 丸める前の u の大きさ。1 を超える線は、線を進む自機の縁が届かない
        float along = 0.0f;           //!< 線の起点から相手の体の中心までの、線に沿った水平の距離 (m)。後ろは負
        NS::Core::Vector3 linePoint;  //!< 相手の体の中心に一番近い線の上の点を、中心の高さに置いた物
        //! 自機の玉が相手の表面に触れる点。線が届かない時は、線に一番近い表面の点
        NS::Core::Vector3 surfacePoint;
        NS::Obj::HitSensorShape bodyShape = NS::Obj::HitSensorShape::Sphere; //!< 相手の体の形の種類
    };

    //! @brief 突進の線と相手の面から、段・威力の倍率・面の上の位置・線の通った点・表面に触れる点を出す
    //! @details 面は相手の物で、いつも自機が来る向きを向く。面の上の位置は、相手の体の中心から線までの左右と上下の
    //! ずれを、左右は「半幅 + 自機の半径」、上下は「半分の高さ + 自機の半径」で割った物。半幅と半分の高さは体の形から
    //! 出す (球は半径、カプセルは筒を線に直交する水平の軸と真上へ写した長さ + 半径、箱は 3 軸を写した長さの和)
    //! 段は決まりの並びを上から見て、最初に当てはまった決まりの段と威力の倍率。どれにも当てはまらなければ外れと
    //! 残りの威力の倍率。赤の欄の非数は 0、幅は 0〜1、位置は -1〜1 へ丸めて読む
    //! 線は origin を通り、origin の高さのまま direction の水平の向きへ伸びる直線
    //! @param[in] face 相手の赤の欄
    //! @param[in] body 相手の体のセンサーの世界の形
    //! @param[in] origin 線の起点。自機の玉の中心
    //! @param[in] direction 突進の向き。縦の成分は捨てる
    //! @param[in] playerRadius 自機の半径 (m)。届く幅と高さに足す
    //! @param[out] out 判定の結果。false の時は触らない
    //! @return 判定できた場合 true。体の形が球・カプセル・箱のどれでもない場合、向きの水平の長さが 0 か有限でない
    //! 場合、起点か自機の半径が有限でない場合は false
    [[nodiscard]] bool JudgeHitFace(const HitFace& face,
                                    const NS::Obj::SensorVolume& body,
                                    const NS::Core::Vector3& origin,
                                    const NS::Core::Vector3& direction,
                                    float playerRadius,
                                    HitFaceJudgement& out) noexcept;

    //! @brief 置物の面の赤 1 つと残りの威力の倍率を持つ部品
    //! @details 種類の既定値 (MapObj.json) が全部の置物の既定を決め、個体は値だけを上書きする
    //! 段は持ち主が体当たりの答えに載せた写しで JudgeHitFace が決める
    class HitZones : public NS::Obj::Component
    {
    public:
        //! 赤の欄
        [[nodiscard]] const HitFace& Face() const noexcept { return m_face; }

        //! 丸にするかを置く
        void SetRound(bool round) noexcept;
        //! 赤の左右の半分の幅の割合を置く。0〜1 へ丸める。非数と無限は 0
        void SetWidth(float width) noexcept;
        //! 赤の上下の半分の幅の割合を置く。0〜1 へ丸める。非数と無限は 0
        void SetHeight(float height) noexcept;
        //! 赤の中心の左右の位置を置く。-1〜1 へ丸める。非数と無限は 0
        void SetCenterU(float u) noexcept;
        //! 赤の中心の上下の位置を置く。-1〜1 へ丸める。非数と無限は 0
        void SetCenterV(float v) noexcept;
        //! 赤の当たり位置の係数を置く。負は 0。非数と無限は 0
        void SetPowerScale(float scale) noexcept;
        //! 赤の外の当たり位置の係数を置く。負は 0。非数と無限は 0
        void SetRemainderPowerScale(float scale) noexcept;

        // 種類の既定値と個体の Inspector で赤を決める
        NS_REFLECT_BEGIN(HitZones, NS::Obj::Component)
        NS_REFLECT_ACCESSOR(bool, "丸", Face().round, SetRound)
        NS_REFLECT_ACCESSOR(float, "横幅", Face().width, SetWidth)
        NS_REFLECT_ACCESSOR(float, "縦の幅", Face().height, SetHeight)
        NS_REFLECT_ACCESSOR(float, "左右の位置", Face().centerU, SetCenterU)
        NS_REFLECT_ACCESSOR(float, "上下の位置", Face().centerV, SetCenterV)
        NS_REFLECT_ACCESSOR(float, "威力の倍率", Face().powerScale, SetPowerScale)
        NS_REFLECT_ACCESSOR(float, "残りの威力の倍率", Face().remainderPowerScale, SetRemainderPowerScale)
        NS_REFLECT_END()

    private:
        HitFace m_face;
    };
} // namespace NS::Game::Level
