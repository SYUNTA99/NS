#pragma once

#include "NSlib/Core/Math.h"

#include <span>
#include <vector>

namespace GL
{
    namespace Level
    {
        //! 反動の向きの表の作り方
        enum class ReboundTableShape
        {
            ThreePlanes, //!< 縦・横・奥の 3 枚の面の円をそれぞれ分割する。8 のとき 18 方向
            YawPitch,    //!< 水平の向きと上下の傾きを同じ角度で分割する。8 のとき 26 方向
        };

        //! @brief 分割の数を 8・12・16 のうち一番近い物へ丸めて、1 段の角度を返す
        //! @return ラジアン。8 なら π/4
        [[nodiscard]] float DirectionStepRadians(int divisions) noexcept;

        //! @brief 反動の向きの表を作る。向きは世界の軸に揃え、全部が単位の長さ
        //! @details 分割の数は 8・12・16 のうち一番近い物へ丸める
        [[nodiscard]] std::vector<NS::Vector3> MakeReboundDirections(int divisions, ReboundTableShape shape);

        //! 物理の反射 r = v − 2(v·n)n。normal は単位の長さ
        [[nodiscard]] NS::Vector3 ReflectAcross(const NS::Vector3& velocity, const NS::Vector3& normal) noexcept;

        //! @brief 面に当たった向きを、表のうち面から離れる向きで物理の反射に一番近い物へ割り当てる
        //! @param[in] incoming 当たる直前の進む向き。長さは問わない
        //! @param[in] normal 当たった面の外向きの単位法線
        //! @param[out] outDirection 割り当てた向き。失敗した場合は書き換えない
        //! @return 表に面から離れる向きが無い場合 false、それ以外の場合は true
        [[nodiscard]] bool AssignReboundDirection(std::span<const NS::Vector3> table,
                                                  const NS::Vector3& incoming,
                                                  const NS::Vector3& normal,
                                                  NS::Vector3& outDirection) noexcept;

        //! @brief 割り当てた向きを、スティックで倒した向きの隣へ 1 段だけずらす
        //! @details 隣は、割り当てた向きから 1 段の角度以内の表の向き。倒した向きのうち
        //! 割り当てた向きに垂直な成分へ一番動く物を選ぶ。垂直な成分が無い (倒していない、真上へ返って上下に倒した)
        //! 時と、その向きへ動く隣が無い時と、選んだ隣が面に沿うか面へ向かう時は割り当てた向きのまま。
        //! 選んだ隣が使えない時に次に近い隣へ逸らさないのは、倒していない左右へずれるのを防ぐため
        //! @param[in] shift スティックを世界へ写した向き。長さは倒し具合
        //! @param[in] stepRadians 表の 1 段の角度
        [[nodiscard]] NS::Vector3 ShiftReboundDirection(std::span<const NS::Vector3> table,
                                                        const NS::Vector3& assigned,
                                                        const NS::Vector3& normal,
                                                        const NS::Vector3& shift,
                                                        float stepRadians) noexcept;

        //! @brief スティックの倒しを、カメラから見た向きのまま、返る向きに垂直な面の向きにする
        //! @details 左右はカメラの右を、前後はカメラの上を、返る向きに垂直な面へ落とした向きに掛ける。返る向きが
        //! カメラの上に近く、落とした上が短いほど、カメラの前を落とした向きで補う。返る向きと画面の上が重なっても、
        //! 前後に倒せば画面の奥と手前へずれる
        //! @param[in] assigned 表へ割り当てた返る向き。単位の長さ
        //! @param[in] cameraRight カメラの右。cameraUp・cameraForward と直交する単位の向き
        //! @param[in] stickRight スティックの右への倒し。左は負
        //! @param[in] stickUp スティックの前への倒し。後ろは負
        //! @return ShiftReboundDirection へ渡すずらしの向き
        [[nodiscard]] NS::Vector3 ShiftInputOnScreen(const NS::Vector3& assigned,
                                                     const NS::Vector3& cameraRight,
                                                     const NS::Vector3& cameraUp,
                                                     const NS::Vector3& cameraForward,
                                                     float stickRight,
                                                     float stickUp) noexcept;

        //! @brief 単位の向き from を、単位の向き to へ最大 maxRadians だけ回す
        //! @details 2 つが真逆で回す面が決まらない時は from のまま
        [[nodiscard]] NS::Vector3 TurnToward(const NS::Vector3& from, const NS::Vector3& to, float maxRadians) noexcept;
    } // namespace Level
} // namespace GL
