#pragma once

#include "NSlib/Core/Types.h"

namespace NS
{
    class HitSensor;
}

namespace GL
{
    namespace Level
    {
        //! @brief センサーの応答先を識別する Game の種類
        //! @details 保存しない。重なりは全組へ通知され、応答の可否は各受け手が決める
        enum class SensorKind : NS::uint8
        {
            Unset = 0,  //!< 未設定。どの受け手も応じない
            PlayerBody, //!< 自機の体。ゴールと落下死の範囲が応じる
            MapObjBody, //!< 置物の体。体当たりの相手になる
            Area,       //!< ゴールと落下死の範囲。自機の体にだけ応じる
        };

        //! Unset を付けると未設定へ戻す
        void SetSensorKind(NS::HitSensor* sensor, SensorKind kind) noexcept;

        //! 指定の種類の場合 true、それ以外の場合は false。Unset は常に false
        [[nodiscard]] bool IsSensorKind(const NS::HitSensor* sensor, SensorKind kind) noexcept;
    } // namespace Level
} // namespace GL
