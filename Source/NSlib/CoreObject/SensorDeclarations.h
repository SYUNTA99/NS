#pragma once

#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"

#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace NS
{
    class SceneActor;

    //! 種類に使える型。下の値が uint8 の enum
    template <class TKind>
    concept SensorKindEnum = std::is_enum_v<TKind> && std::same_as<std::underlying_type_t<TKind>, uint8>;

    //! アクターのクラスが宣言したセンサーを、基底の SceneActor に作らせる
    //! 作れるのは SceneActor だけで、DeclareSensors を呼ぶ間だけ渡される。だから OnInit からはセンサーを作れない。
    //! 作ったセンサーは一覧に入り、種類が付く。表に無いメンバーと作成済みのメンバーは ERROR を出して作らない
    class SensorDeclarations
    {
    public:
        SensorDeclarations(const SensorDeclarations&) = delete;
        SensorDeclarations& operator=(const SensorDeclarations&) = delete;

        //! 独立した形のセンサー (ShapeHitSensor) を作る。大きさは .actor の欄の値
        template <SensorKindEnum TKind> void Shape(std::unique_ptr<HitSensor>& member, TKind kind, HitSensorShape shape)
        {
            ShapeOfKind(member, static_cast<uint8>(kind), shape);
        }

        //! 持ち主の当たりの形を毎回 volume から読むセンサー (FollowHitSensor) を作る
        template <SensorKindEnum TKind>
        void Follow(std::unique_ptr<HitSensor>& member,
                    TKind kind,
                    std::function<SensorVolume(const Matrix& rootWorld)> volume)
        {
            FollowOfKind(member, static_cast<uint8>(kind), std::move(volume));
        }

    private:
        friend class SceneActor;
        explicit SensorDeclarations(SceneActor* owner) noexcept : m_owner(owner) {}

        void ShapeOfKind(std::unique_ptr<HitSensor>& member, uint8 kind, HitSensorShape shape);
        void FollowOfKind(std::unique_ptr<HitSensor>& member,
                          uint8 kind,
                          std::function<SensorVolume(const Matrix& rootWorld)> volume);

        SceneActor* m_owner = nullptr; // 宣言したアクター (非所有)
    };
} // namespace NS

//! アクターのクラスの本体に、そのクラスが持つセンサーを並べる。NS_SENSORS_END で閉じる
//! 基底の SceneActor が OnInit の前に作るので、OnInit と状態の OnEnter から読める。
//! 派生のクラスが並べ直すと、基底の並びを置き換える
#define NS_SENSORS_BEGIN() void DeclareSensors(::NS::SensorDeclarations& sensors) override {
//! 独立した形のセンサーを 1 つ並べる。kind は Game の種類、shape は NS::HitSensorShape
#define NS_SENSOR(member, kind, shape) sensors.Shape(member, kind, shape);
//! 形を毎回メンバー関数 function(rootWorld) から読むセンサーを 1 つ並べる。OnInit の後にしか形を問われない
#define NS_FOLLOW_SENSOR(member, kind, function)                                                                       \
    sensors.Follow(member, kind, [this](const ::NS::Matrix& rootWorld) { return function(rootWorld); });
#define NS_SENSORS_END() }
