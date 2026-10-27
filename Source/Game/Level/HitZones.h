#pragma once

#include "Game/Level/HitTier.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Object.h"

#include <vector>

namespace GL
{
    namespace Level
    {
        /// 相手の面の赤領域と、領域内外の威力倍率
        /// 面上の位置は左右 u・上下 v で、どちらも -1〜1
        struct HitFace
        {
            bool round = true;                // 丸か。偽なら箱
            float width = 0.43f;              // 面全体に対する赤の左右の半分の幅の割合。0〜1
            float height = 0.43f;             // 面全体に対する赤の上下の半分の幅の割合。0〜1
            float centerU = 0.0f;             // 赤の中心の左右の位置。-1〜1、自機から見て右が正
            float centerV = 0.0f;             // 赤の中心の上下の位置。-1〜1、上が正
            float powerScale = 1.0f;          // 赤で当たった時の当たり位置の係数
            float remainderPowerScale = 0.7f; // 赤の外 (外れ) の当たり位置の係数
        };

        /// JudgeHitFace の結果
        struct HitFaceJudgement
        {
            HitTier tier = HitTier::Wide; // 当たった段。赤の外は外れ
            float powerScale = 1.0f;      // 威力の倍率。赤の外なら残りの威力の倍率
            float u = 0.0f;               // 面の上の左右の位置。自機から見て右が正。-1〜1 が触れられる幅
            float v = 0.0f;               // 面の上の上下の位置。上が正。-1〜1 が触れられる高さ
            float offset01 = 1.0f;        // 横ずれ。u の大きさを 0〜1 に丸めた値
            float ratio = 1.0f;           // 丸める前の u の大きさ。1 を超える線は、線を進む自機の縁が届かない
            float along = 0.0f;           // 線の起点から相手の体の中心までの、線に沿った水平の距離 (m)。後ろは負
            NS::Vector3 linePoint;        // 相手の体の中心に一番近い線の上の点を、中心の高さに置いた物
            /// 自機の玉が相手の表面に触れる点。線が届かない時は、線に一番近い表面の点
            NS::Vector3 surfacePoint;
            NS::HitSensorShape bodyShape = NS::HitSensorShape::Sphere; // 相手の体の形の種類
        };

        /// 相手の正面で判定し、赤の内側は Center、それ以外は Wide とする
        /// origin は自機の玉の中心、direction は水平成分を使う。playerRadius は m
        /// 面の座標は体の半幅・半高と自機半径の和を 1 とする。赤の幅は 0〜1、位置は -1〜1、非有限は 0 に補正する
        /// 球・カプセル・箱以外の体、無効な水平方向、非有限の起点・半径なら false で out を変更しない
        [[nodiscard]] bool JudgeHitFace(const HitFace& face,
                                        const NS::SensorVolume& body,
                                        const NS::Vector3& origin,
                                        const NS::Vector3& direction,
                                        float playerRadius,
                                        HitFaceJudgement& out) noexcept;

        /// JudgeHitFace の条件で判定する。失敗時は Wide、赤の外の倍率、横ずれ 1、表面点は体の中心とする
        /// 他の欄は既定値。未知の形で最強の当たりが出ないよう、予測と裁定の失敗処理を共通にする
        [[nodiscard]] HitFaceJudgement JudgeHitFaceOrWide(const HitFace& face,
                                                          const NS::SensorVolume& body,
                                                          const NS::Vector3& origin,
                                                          const NS::Vector3& direction,
                                                          float playerRadius) noexcept;

        /// 赤の中心の世界 y を出す。赤が覆う範囲を持たなければ体の中心を使う
        /// 体・方向・半径の条件は JudgeHitFace と同じ。失敗時は false で outHeight を変更しない
        [[nodiscard]] bool HitFaceAimHeight(const HitFace& face,
                                            const NS::SensorVolume& body,
                                            const NS::Vector3& direction,
                                            float playerRadius,
                                            float& outHeight) noexcept;

        /// 面の上の 1 つの形。赤の範囲か、外れの面
        /// 位置と大きさは面の上の左右 u・上下 v で表す
        /// 丸は楕円、箱は長方形で、縁ちょうどは外
        struct HitFaceShape
        {
            HitTier tier = HitTier::Wide; // 形の中で当たった時の段
            bool round = true;            // 楕円か。偽なら長方形
            float centerU = 0.0f;         // 中心の左右の位置。自機から見て右が正
            float centerV = 0.0f;         // 中心の上下の位置。上が正
            float halfU = 1.0f;           // 左右の半分の幅
            float halfV = 1.0f;           // 上下の半分の幅
            /// 丸の縁の分割数。既定の近似誤差は約半パーセント
            int outlineSegments = 32;
        };

#if !defined(NS_SHIPPING)
        /// 相手の正面の面を世界に置いた時の位置と軸
        struct HitFaceFrame
        {
            NS::Vector3 center;                                        // 面の中心。相手の正面に接する点
            NS::Vector3 right;                                         // 面の左右の軸。自機から見て右
            NS::Vector3 up;                                            // 面の上下の軸。真上
            NS::Vector3 normal;                                        // 面の向き。自機の方
            float reachU = 0.0f;                                       // u が 1 の所までの長さ (m)
            float reachV = 0.0f;                                       // v が 1 の所までの長さ (m)
            NS::HitSensorShape bodyShape = NS::HitSensorShape::Sphere; // 相手の体の形の種類
        };

        /// エディタの描画用に、突進と逆を向く平面を相手の正面へ置く
        /// 幅・高さと入力条件は JudgeHitFace と同じ。失敗時は false で out を変更しない
        [[nodiscard]] bool MakeHitFaceFrame(const NS::SensorVolume& body,
                                            const NS::Vector3& direction,
                                            float playerRadius,
                                            HitFaceFrame& out) noexcept;

        /// 面の座標を世界の点へ変換する。u は自機から見て右、v は上が正
        [[nodiscard]] NS::Vector3 HitFacePoint(const HitFaceFrame& frame, float u, float v) noexcept;

        /// 下に描く順に形を返す。先頭は球なら半径 1 の円、それ以外は ±1 の矩形。その上に赤を重ねる
        /// 赤の補正は JudgeHitFace と同じ。幅が 0 の赤は含めない
        [[nodiscard]] std::vector<HitFaceShape> HitFaceShapes(const HitFace& face,
                                                              NS::HitSensorShape bodyShape) noexcept;

        /// 縁を一周する点を返す。x は u、y は v。末尾から先頭へ結ぶと閉じる
        /// 楕円は outlineSegments を 3 以上に補正して分割し、長方形は 4 隅を返す
        [[nodiscard]] std::vector<NS::Vector2> HitFaceShapeOutline(const HitFaceShape& shape) noexcept;
#endif

        /// アクターごとの赤領域と威力倍率を保持する部品
        class HitZones : public NS::Object
        {
        public:
            [[nodiscard]] const HitFace& Face() const noexcept { return m_face; }

            void SetRound(bool round) noexcept;
            /// 赤の左右の半分の幅の割合を置く。0〜1 へ丸める。非数と無限は 0
            void SetWidth(float width) noexcept;
            /// 赤の上下の半分の幅の割合を置く。0〜1 へ丸める。非数と無限は 0
            void SetHeight(float height) noexcept;
            /// 赤の中心の左右の位置を置く。-1〜1 へ丸める。非数と無限は 0
            void SetCenterU(float u) noexcept;
            /// 赤の中心の上下の位置を置く。-1〜1 へ丸める。非数と無限は 0
            void SetCenterV(float v) noexcept;
            /// 赤の当たり位置の係数を置く。負は 0。非数と無限は 0
            void SetPowerScale(float scale) noexcept;
            /// 赤の外の当たり位置の係数を置く。負は 0。非数と無限は 0
            void SetRemainderPowerScale(float scale) noexcept;

            // 赤は .actor の値で決まり、Inspector で直せる
            NS_REFLECT_BEGIN(HitZones, NS::Object)
            NS_ACCESSOR(bool, "丸", Face().round, SetRound)
            NS_ACCESSOR(float, "横幅", Face().width, SetWidth)
            NS_ACCESSOR(float, "縦の幅", Face().height, SetHeight)
            NS_ACCESSOR(float, "左右の位置", Face().centerU, SetCenterU)
            NS_ACCESSOR(float, "上下の位置", Face().centerV, SetCenterV)
            NS_ACCESSOR(float, "威力の倍率", Face().powerScale, SetPowerScale)
            NS_ACCESSOR(float, "残りの威力の倍率", Face().remainderPowerScale, SetRemainderPowerScale)
            NS_REFLECT_END()

        private:
            HitFace m_face;
        };
    } // namespace Level
} // namespace GL
