#pragma once

#include <cstdint>

namespace NS::Obj
{
    class HitSensor;
}

namespace GL::Level
{
    //! @brief センサーの種類。誰が誰に応じるかを決める Game の語彙
    //! @details 種類は Player / MapObj / Goal / DeathZone の OnInit が付ける。保存はしない
    //! HitSensor には名前の無い数として渡り、HitSensorDirector は種類を見ずに重なった組の両方へ知らせる
    //! 応じるかを決めるのは Goal / DeathZone の AttackSensor と、体当たりの相手を絞る ImpactResolver
    enum class SensorKind : std::uint8_t
    {
        Unset = 0,  //!< 未設定。どの受け手も応じない
        PlayerBody, //!< 自機の体。ゴールと落下死の範囲が応じる
        MapObjBody, //!< 置物の体。体当たりの相手になる
        Area,       //!< ゴールと落下死の範囲。自機の体にだけ応じる
    };

    //! @brief センサーに種類を付ける
    //! @param[in,out] sensor 種類を付けるセンサー
    //! @param[in] kind 付ける種類。Unset を付けると未設定へ戻る
    void SetSensorKind(NS::Obj::HitSensor& sensor, SensorKind kind) noexcept;

    //! @brief センサーが指定の種類か
    //! @param[in] sensor 調べるセンサー
    //! @param[in] kind 照合する種類
    //! @return 指定の種類の場合 true、それ以外の場合は false。Unset を問うと常に false
    [[nodiscard]] bool IsSensorKind(const NS::Obj::HitSensor& sensor, SensorKind kind) noexcept;
} // namespace GL::Level
