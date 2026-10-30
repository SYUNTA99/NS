#pragma once

#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/Object.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include "NSlib/Core/Types.h"
#include <concepts>
#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace NS
{
    class IGetHitSensor;
    class SceneActor;

    //! 種類に使える型。下の値が uint8 の enum
    template <class TKind>
    concept SensorKindEnum = std::is_enum_v<TKind> && std::same_as<std::underlying_type_t<TKind>, uint8>;

    //! アクターが持つセンサーを、名前つきで並べて持つ部品
    //! センサーを作るのは AddShape・AddFollow だけで、作った時に管理クラスへ 1 回入れる。外へは非所有のポインタだけを出す
    class HitSensorsObj final : public Object
    {
    public:
        HitSensorsObj() noexcept;
        ~HitSensorsObj() noexcept override;
        HitSensorsObj(const HitSensorsObj&) = delete;
        HitSensorsObj& operator=(const HitSensorsObj&) = delete;
        HitSensorsObj(HitSensorsObj&&) = delete;
        HitSensorsObj& operator=(HitSensorsObj&&) = delete;

        //! 独立した形のセンサーを作り、管理クラスへ入れて並びに足す。大きさは .actor の hitSensors の値
        //! @param[in] parent 持ち主。事前条件: nullptr でない
        //! @return 作ったセンサー。同じ名前が並びにあれば ERROR を出して作らず nullptr
        template <SensorKindEnum TKind>
        ShapeHitSensor* AddShape(SceneActor* parent, std::string_view name, TKind kind, HitSensorShape shape)
        {
            return AddShapeOfKind(parent, name, static_cast<uint8>(kind), shape);
        }

        //! 持ち主の当たりの形を毎回 volume から読むセンサーを作り、管理クラスへ入れて並びに足す
        //! @param[in] parent 持ち主。事前条件: nullptr でない
        //! @return 作ったセンサー。同じ名前が並びにあれば ERROR を出して作らず nullptr
        template <SensorKindEnum TKind>
        FollowHitSensor* AddFollow(SceneActor* parent,
                                   std::string_view name,
                                   TKind kind,
                                   std::function<SensorVolume(const Matrix& rootWorld)> volume)
        {
            return AddFollowOfKind(parent, name, static_cast<uint8>(kind), std::move(volume));
        }

        //! name のセンサー。無ければ nullptr
        [[nodiscard]] HitSensor* Find(std::string_view name) const noexcept;
        //! 作った順の全部のセンサー
        [[nodiscard]] const std::vector<HitSensor*>& Sensors() const noexcept { return m_sensors; }

        //! 持ち主が出た時に、全部のセンサーを調べる対象へ戻す
        void EnableAll() noexcept;
        //! 持ち主が消えた時に、全部のセンサーを入れたまま調べる対象から外す
        void DisableAll() noexcept;
        //! 全部のセンサーを管理クラスから外す。持ち主が終わる時 (OnEndPlay) に呼ぶ
        //! @param[in] user 管理クラスを取得する窓口。事前条件: nullptr でない
        void EndAll(const IGetHitSensor* user);

        //! 持ち主の .actor の hitSensors から、名前ごとの値を同じ名前のセンサーへ書く
        //! 先に全部のセンサーを作った時の値へ戻すので、前に読んだ値は残らない。並びに無い名前と、
        //! 値を持つのに hitSensors に無いセンサーは WARN を出す。持ち主に .actor が無ければ何もしない
        //! @param[in] owner 持ち主。事前条件: nullptr でない
        void ReloadValues(const SceneActor* owner);
        //! actorFile へ、ReloadValues が読むのと同じ形の hitSensors を書く。値を持たないセンサーは書かない
        //! 並びに無い名前の値は字面のまま残す
        void WriteValues(nlohmann::json& actorFile) const;

        NS_REFLECT_NONE(HitSensorsObj, Object)

    private:
        // 作ったセンサーと、作った時の値。ReloadValues が .actor を当てる前に戻す先
        struct Held
        {
            std::unique_ptr<HitSensor> sensor;
            nlohmann::json declared;
        };

        ShapeHitSensor* AddShapeOfKind(SceneActor* parent, std::string_view name, uint8 kind, HitSensorShape shape);
        FollowHitSensor* AddFollowOfKind(SceneActor* parent,
                                         std::string_view name,
                                         uint8 kind,
                                         std::function<SensorVolume(const Matrix& rootWorld)> volume);
        // TSensor を作り、管理クラスへ入れて並びに足す。同じ名前があれば ERROR を出して作らず nullptr
        template <class TSensor, class TShape>
        TSensor* Add(SceneActor* parent, std::string_view name, uint8 kind, TShape&& shape);

        std::vector<Held> m_held;          // 作った順のセンサー (所有)
        std::vector<HitSensor*> m_sensors; // m_held と同じ順の非所有のポインタ。Sensors() が参照で返す並び
    };
} // namespace NS
