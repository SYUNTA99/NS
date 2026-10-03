#pragma once

#include <cstdint>

namespace NS::Obj
{
    class HitSensor;
}

namespace NS::Game::Level
{
    //! @brief センサーの種類。誰が誰に応じるかを決める Game の語彙
    //! @details 種類は Player / MapObj / Goal / KillZone のコンストラクタが付ける。保存はしない
    //! 値は Runtime の HitSensor には名前の無い数として渡る。今は HitSensorType へ写している
    enum class SensorKind : std::uint8_t
    {
        Unset = 0,  //!< 未設定。どの受け手にも応じられない
        PlayerBody, //!< 自機の体。範囲 (ゴール・落下死) に調べられる
        MapObjBody, //!< 置物の体。体当たりに調べられる
        Area,       //!< 範囲 (ゴール・落下死)。自機の体を調べる
    };

    //! @brief センサーに種類を付ける
    //! @param[in,out] sensor 種類を付けるセンサー
    //! @param[in] kind 付ける種類。Unset は Runtime の種類に無いので、ログを出して何もしない
    void SetSensorKind(NS::Obj::HitSensor& sensor, SensorKind kind) noexcept;

    //! @brief センサーが指定の種類か
    //! @param[in] sensor 調べるセンサー
    //! @param[in] kind 照合する種類
    //! @return 指定の種類の場合 true、それ以外の場合は false。Unset を問うと常に false
    [[nodiscard]] bool IsSensorKind(const NS::Obj::HitSensor& sensor, SensorKind kind) noexcept;
} // namespace NS::Game::Level
