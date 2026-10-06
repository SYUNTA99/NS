#pragma once

#include "Game/Level/HitTier.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Object/Component.h"
#include "NSlib/Object/Components/HitSensor.h"

#include <vector>

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
        // 赤の当たりは威力を減らさない
        float powerScale = 1.0f; //!< 赤で当たった時の当たり位置の係数
        // かすめた当たりの威力を 3 割落とす
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
        NS::Vector3 linePoint;        //!< 相手の体の中心に一番近い線の上の点を、中心の高さに置いた物
        //! 自機の玉が相手の表面に触れる点。線が届かない時は、線に一番近い表面の点
        NS::Vector3 surfacePoint;
        NS::Obj::HitSensorShape bodyShape = NS::Obj::HitSensorShape::Sphere; //!< 相手の体の形の種類
    };

    //! @brief 突進の線と相手の面から、段・威力の倍率・面の上の位置・線の通った点・表面に触れる点を出す
    //! @details 面は相手の物で、いつも自機が来る向きを向く。面の上の位置は、相手の体の中心から線までの左右と上下の
    //! ずれを、左右は「半幅 + 自機の半径」、上下は「半分の高さ + 自機の半径」で割った物。半幅と半分の高さは体の形から
    //! 出す (球は半径、カプセルは筒を線に直交する水平の軸と真上へ写した長さ + 半径、箱は 3 軸を写した長さの和)
    //! 赤の内側は中心の段と赤の威力倍率、それ以外は外れと残りの威力倍率
    //! 赤の欄の非数は 0、幅は 0〜1、位置は -1〜1 へ丸めて読む
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
                                    const NS::Vector3& origin,
                                    const NS::Vector3& direction,
                                    float playerRadius,
                                    HitFaceJudgement& out) noexcept;

    //! @brief JudgeHitFace で判定し、判定できない時は外れの結果を返す
    //! @details 体当たりの裁定と狙いの予測が同じ結果を出すよう、判定できない時の扱いをここ 1 か所で決める
    //! 判定できない時は、段が外れ、威力の倍率が赤の外の威力の倍率 (JudgeHitFace と同じく丸めて読む)、横ずれが 1、
    //! 表面に触れる点が相手の体の中心。他の欄は HitFaceJudgement の既定のまま
    //! 判定できない体を中心近く・係数 1 にすると、形の分からない相手で一番強い当たりが出るため
    //! @param[in] face 相手の赤の欄
    //! @param[in] body 相手の体のセンサーの世界の形
    //! @param[in] origin 線の起点。自機の玉の中心
    //! @param[in] direction 突進の向き。縦の成分は捨てる
    //! @param[in] playerRadius 自機の半径 (m)
    //! @return 判定の結果。判定できない時は外れの結果
    [[nodiscard]] HitFaceJudgement JudgeHitFaceOrWide(const HitFace& face,
                                                      const NS::Obj::SensorVolume& body,
                                                      const NS::Vector3& origin,
                                                      const NS::Vector3& direction,
                                                      float playerRadius) noexcept;

    //! @brief 溜めて放つ突進が狙う高さ (世界の y) を出す
    //! @details 段の決まりの並びの先頭にある赤の中心の高さ。相手の体の中心の高さ + 赤の上下の位置 ×
    //! (半分の高さ + 自機の半径)。半分の高さは JudgeHitFace と同じく体の形から出す。
    //! 赤がどこも覆わない (横幅か縦の幅が 0) 時は相手の体の中心の高さ。赤の欄は JudgeHitFace と同じく丸めて読む
    //! @param[in] face 相手の赤の欄
    //! @param[in] body 相手の体のセンサーの世界の形
    //! @param[in] direction 突進の向き。縦の成分は捨てる
    //! @param[in] playerRadius 自機の半径 (m)
    //! @param[out] outHeight 狙う高さ。false の時は触らない
    //! @return 出せた場合 true。体の形が球・カプセル・箱のどれでもない場合、向きの水平の長さが 0 か有限でない
    //! 場合、自機の半径が有限でない場合は false
    [[nodiscard]] bool HitFaceAimHeight(const HitFace& face,
                                        const NS::Obj::SensorVolume& body,
                                        const NS::Vector3& direction,
                                        float playerRadius,
                                        float& outHeight) noexcept;

    //! @brief 面の上の 1 つの形。段の決まりが覆う範囲か、外れの面
    //! @details 位置と大きさは面の上の位置 (左右 u・上下 v) で表す。丸は楕円、箱は長方形で、縁ちょうどは外
    struct HitFaceShape
    {
        HitTier tier = HitTier::Wide; //!< 形の中で当たった時の段
        bool round = true;            //!< 楕円か。偽なら長方形
        float centerU = 0.0f;         //!< 中心の左右の位置。自機から見て右が正
        float centerV = 0.0f;         //!< 中心の上下の位置。上が正
        float halfU = 1.0f;           //!< 左右の半分の幅
        float halfV = 1.0f;           //!< 上下の半分の幅
    };

#if !defined(NS_SHIPPING)
    //! @brief 相手の正面の面を世界に置いた時の位置と軸
    struct HitFaceFrame
    {
        NS::Vector3 center;                                                  //!< 面の中心。相手の正面に接する点
        NS::Vector3 right;                                                   //!< 面の左右の軸。自機から見て右
        NS::Vector3 up;                                                      //!< 面の上下の軸。真上
        NS::Vector3 normal;                                                  //!< 面の向き。自機の方
        float reachU = 0.0f;                                                 //!< u が 1 の所までの長さ (m)
        float reachV = 0.0f;                                                 //!< v が 1 の所までの長さ (m)
        NS::Obj::HitSensorShape bodyShape = NS::Obj::HitSensorShape::Sphere; //!< 相手の体の形の種類
    };

    //! @brief 突進の向きから見た相手の正面の面を、世界に置く
    //! @details 面は向きの逆を向き、相手の体の正面に接する平面に立つ。左右と上下の長さは JudgeHitFace と同じく
    //! 「半幅 + 自機の半径」「半分の高さ + 自機の半径」。エディタが面を描くのに使い、出荷には載せない
    //! @param[in] body 相手の体のセンサーの世界の形
    //! @param[in] direction 突進の向き。縦の成分は捨てる
    //! @param[in] playerRadius 自機の半径 (m)
    //! @param[out] out 面の位置と軸。false の時は触らない
    //! @return 置けた場合 true。体の形が球・カプセル・箱のどれでもない場合、向きの水平の長さが 0 か有限でない
    //! 場合、自機の半径が有限でない場合は false
    [[nodiscard]] bool MakeHitFaceFrame(const NS::Obj::SensorVolume& body,
                                        const NS::Vector3& direction,
                                        float playerRadius,
                                        HitFaceFrame& out) noexcept;

    //! @brief 面の上の位置を世界の点にする
    //! @param[in] frame MakeHitFaceFrame で置いた面
    //! @param[in] u 左右の位置。自機から見て右が正
    //! @param[in] v 上下の位置。上が正
    //! @return 面の上の世界の点
    [[nodiscard]] NS::Vector3 HitFacePoint(const HitFaceFrame& frame, float u, float v) noexcept;

    //! @brief 面に重ねて描く形を、下に敷く順に返す
    //! @details 先頭は外れの面。球の相手は触れられる丸 (半径 1 の円)、それ以外は四角 (±1)。その後に段の決まりの
    //! 形を、JudgeHitFace が見る並びの逆 (優先の低い順) で続ける。後の形ほど上に描けば、当てはまる段の色が見える
    //! 形は JudgeHitFace と同じ決まりから取る。どこも覆わない決まり (幅 0 の赤) は入れない
    //! 赤の欄は JudgeHitFace と同じく丸めて読む
    //! @param[in] face 相手の赤の欄
    //! @param[in] bodyShape 相手の体の形の種類
    //! @return 描く形。下に敷く物が先
    [[nodiscard]] std::vector<HitFaceShape> HitFaceShapes(const HitFace& face,
                                                          NS::Obj::HitSensorShape bodyShape) noexcept;

    //! @brief 形の縁の点を、面の上の位置で一周ぶん返す
    //! @details 楕円は 32 等分した点、長方形は 4 つの角。点はどれも縁の上で、隣どうしを結ぶと縁になる
    //! 楕円の点を結んだ多角形は楕円より内側へ最大 0.5 % 入る
    //! @param[in] shape 形
    //! @return 縁の点。x が左右の位置 u、y が上下の位置 v
    [[nodiscard]] std::vector<NS::Vector2> HitFaceShapeOutline(const HitFaceShape& shape) noexcept;
#endif

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
