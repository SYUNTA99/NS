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
            ThreePlanes, // 縦・横・奥の 3 枚の面の円をそれぞれ分割する。8 のとき 18 方向
            YawPitch,    // 水平の向きと上下の傾きを同じ角度で分割する。8 のとき 26 方向
        };

        //! 分割の数を 8・12・16 のうち一番近い物へ丸めて、1 段の角度を返す
        //! @return ラジアン。8 なら π/4
        [[nodiscard]] float DirectionStepRadians(int divisions) noexcept;

        //! 反動の向きの表を作る。向きは世界の軸に揃え、全部が単位の長さ
        //! 分割の数は 8・12・16 のうち一番近い物へ丸める
        [[nodiscard]] std::vector<NS::Vector3> MakeReboundDirections(int divisions, ReboundTableShape shape);

        //! 物理の反射 r = v − 2(v·n)n。normal は単位の長さ
        [[nodiscard]] NS::Vector3 ReflectAcross(const NS::Vector3& velocity, const NS::Vector3& normal) noexcept;

        //! 面に当たった向きを、表のうち面から離れる向きで物理の反射に一番近い物へ割り当てる
        //! @param[in] incoming 当たる直前の進む向き。長さは問わない
        //! @param[in] normal 当たった面の外向きの単位法線
        //! @param[out] outDirection 割り当てた向き。失敗した場合は書き換えない
        //! @return 表に面から離れる向きが無い場合 false、それ以外の場合は true
        [[nodiscard]] bool AssignReboundDirection(std::span<const NS::Vector3> table,
                                                  const NS::Vector3& incoming,
                                                  const NS::Vector3& normal,
                                                  NS::Vector3& outDirection) noexcept;

        //! カメラから見た画面の向き。全部が単位の長さ
        struct ScreenAxes
        {
            NS::Vector3 right{};       //!< 画面の右
            NS::Vector3 up{};          //!< 画面の上
            NS::Vector3 forward{};     //!< カメラの見る向き
            NS::Vector3 flatForward{}; //!< 見る向きを水平にした向き
        };

        //! 入力補正が返る向きをどうしたか
        enum class ReboundInputResult
        {
            Kept,    //!< 入力が無いか、入力の側へ 1 目盛りで曲げられる向きが 1 つに決まらず、割り当てた向きのまま
            Matched, //!< 入力が割り当てた向きと画面で合った。向きは割り当てた向きのまま
            Shifted, //!< 入力の側へ 1 目盛りだけ変えた
        };

        //! 入力補正で選んだ返る向き
        struct ReboundChoice
        {
            NS::Vector3 direction{};
            ReboundInputResult result = ReboundInputResult::Kept;
        };

        //! 入力補正の候補を決める当たり
        struct ReboundHit
        {
            NS::Vector3 assigned{}; //!< 表へ割り当てた返る向き。単位の長さ
            NS::Vector3 normal{};   //!< 当たった面の外向きの単位法線
            NS::Vector3 incoming{}; //!< 当たる直前の進む向き。単位の長さ
        };

        //! 入力補正で選べる幅
        struct ReboundChoiceLimits
        {
            float screenTieRadians = 0.0f;  //!< 入力と同じだけ近いと見なす角度の差。合ったかは画面の上、曲げる先は世界の向きで計る
            float depthSlackRadians = 0.0f; //!< 奥と手前へ飛ぶ量の上限に足す角度。カメラと当たる前の向きのわずかなずれを吸う
            float stepRadians = 0.0f;       //!< 表の 1 段の角度。分割の数から作る
            float matchVisibleLength = 0.0f; //!< 割り当てた向きを画面の右と上へ映した長さがこれより短いと、入力と合ったとみなさない
        };

        //! @brief 入力が割り当てた向きと画面で合えばそのまま、合わなければ入力の側へ表の 1 目盛りだけ曲げる
        //! @details 候補は、面から離れる表の向き。カメラの水平の前の向きの成分が、当たる前の向きの成分に余裕の角度の分を
        //! 足した物より大きい向きは外す。割り当てた向きは候補の検査にかけずに画面の上の角度を計り、画面へ映した長さが
        //! 合ったとみなす下限以上で、全部の候補で一番近い物との差が同じに見なす差の中なら、入力が合ったとして割り当てた向きを返す。
        //! 合わなければ、割り当てた向きの隣 (表の傾きと水平の番号をそれぞれ 1 つまで動かした物) の候補から選ぶ。入力はカメラの右と上で
        //! 世界の向きにし、面へ向かう分は面に沿って上へ向かう向きに置き換える (床と天井では捨てる)。その向きとの角度が割り当てた向き
        //! より小さい候補のうち、一番近い物から同じに見なす差の中の物を同じだけ近いとし、割り当てた向きから離れる角度が一番小さい物を選ぶ。
        //! 離れる角度も同じ 2 つが残ったら (左右に同じだけ近い時) 曲げない
        //! @param[in] stickRight スティックの右への入力。左は負
        //! @param[in] stickUp スティックの前への入力。後ろは負
        [[nodiscard]] ReboundChoice ChooseReboundDirection(std::span<const NS::Vector3> table,
                                                           const ReboundHit& hit,
                                                           const ScreenAxes& screen,
                                                           float stickRight,
                                                           float stickUp,
                                                           const ReboundChoiceLimits& limits) noexcept;

        //! スティックの倒しを、カメラから見た向きのまま、返る向きに垂直な面の向きにする
        //! 左右はカメラの右を、前後はカメラの上を、返る向きに垂直な面へ落とした向きに掛ける。返る向きが
        //! カメラの上に近く、落とした上が短いほど、カメラの前を落とした向きで補う。返る向きと画面の上が重なっても、
        //! 前後に倒せば画面の奥と手前へずれる
        //! @param[in] assigned 表へ割り当てた返る向き。単位の長さ
        //! @param[in] cameraRight カメラの右。cameraUp・cameraForward と直交する単位の向き
        //! @param[in] stickRight スティックの右への倒し。左は負
        //! @param[in] stickUp スティックの前への倒し。後ろは負
        //! @return 反動の後の曲げの目標の向き。長さは入力の大きさ
        [[nodiscard]] NS::Vector3 ShiftInputOnScreen(const NS::Vector3& assigned,
                                                     const NS::Vector3& cameraRight,
                                                     const NS::Vector3& cameraUp,
                                                     const NS::Vector3& cameraForward,
                                                     float stickRight,
                                                     float stickUp) noexcept;

        //! 単位の向き from を、単位の向き to へ最大 maxRadians だけ回す
        //! 2 つが真逆で回す面が決まらない時は from のまま
        [[nodiscard]] NS::Vector3 TurnToward(const NS::Vector3& from, const NS::Vector3& to, float maxRadians) noexcept;
    } // namespace Level
} // namespace GL
