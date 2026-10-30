#include "NSlib/CoreObject/ClassObjs/HitSensorsObj.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/IGet/IGetHitSensor.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <string>

namespace NS
{
    HitSensorsObj::HitSensorsObj() noexcept = default;
    HitSensorsObj::~HitSensorsObj() noexcept = default;

    ShapeHitSensor* HitSensorsObj::AddShapeOfKind(SceneActor* parent,
                                                  std::string_view name,
                                                  uint8 kind,
                                                  HitSensorShape shape)
    {
        NS_ASSERT(Scene, parent != nullptr, "HitSensorsObj::AddShape: 持ち主が null");
        return Add<ShapeHitSensor>(parent, name, kind, shape);
    }

    FollowHitSensor* HitSensorsObj::AddFollowOfKind(SceneActor* parent,
                                                    std::string_view name,
                                                    uint8 kind,
                                                    std::function<SensorVolume(const Matrix& rootWorld)> volume)
    {
        NS_ASSERT(Scene, parent != nullptr, "HitSensorsObj::AddFollow: 持ち主が null");
        return Add<FollowHitSensor>(parent, name, kind, std::move(volume));
    }

    template <class TSensor, class TShape>
    TSensor* HitSensorsObj::Add(SceneActor* parent, std::string_view name, uint8 kind, TShape&& shape)
    {
        // 同じ名前が 2 つあると、.actor の値と動作の入り切りがどちらを指すか決まらない
        if (Find(name) != nullptr)
        {
            NS_LOG_ERROR(Scene, "{} のセンサー {} は作成済み。2 つ目は作らない", parent->ClassName(), name);
            return nullptr;
        }
        std::unique_ptr<TSensor> sensor =
            std::make_unique<TSensor>(parent, std::string{name}, kind, std::forward<TShape>(shape));
        TSensor* raw = sensor.get();
        InitHitSensor(parent, raw);
        m_sensors.push_back(raw);
        m_held.push_back(Held{.sensor = std::move(sensor), .declared = SerializeClassObjFields(raw)});
        return raw;
    }

    HitSensor* HitSensorsObj::Find(std::string_view name) const noexcept
    {
        for (HitSensor* sensor : m_sensors)
        {
            if (sensor->Name() == name)
            {
                return sensor;
            }
        }
        return nullptr;
    }

    void HitSensorsObj::EnableAll() noexcept
    {
        for (HitSensor* sensor : m_sensors)
        {
            EnableHitSensor(sensor);
        }
    }

    void HitSensorsObj::DisableAll() noexcept
    {
        for (HitSensor* sensor : m_sensors)
        {
            DisableHitSensor(sensor);
        }
    }

    void HitSensorsObj::EndAll(const IGetHitSensor* user)
    {
        NS_ASSERT(Scene, user != nullptr, "HitSensorsObj::EndAll: 窓口が null");
        for (HitSensor* sensor : m_sensors)
        {
            EndHitSensor(user, sensor);
        }
    }

    void HitSensorsObj::ReloadValues(const SceneActor* owner)
    {
        NS_ASSERT(Scene, owner != nullptr, "HitSensorsObj::ReloadValues: 持ち主が null");
        const nlohmann::json* actorFile = ActorFileLibrary::Get().Values(owner->ActorFileName());
        if (actorFile == nullptr)
        {
            return;
        }
        // 前に読んだ値を残さない。取り消しで hitSensors が消えた時に、ファイルに無い大きさで当たり続ける
        for (const Held& held : m_held)
        {
            (void)ApplyJsonFields(held.sensor.get(), held.declared); // 自分で書き出した値なので知らない欄は無い
        }
        const nlohmann::json empty = nlohmann::json::object();
        const nlohmann::json* values = ActorFileHitSensors(*actorFile);
        if (values == nullptr)
        {
            values = &empty;
        }
        if (!values->is_object())
        {
            NS_LOG_ERROR(Scene, "{}.actor の hitSensors が object でない。読まない", owner->ActorFileName());
            return;
        }
        // 値を持つセンサーが hitSensors に無いと、作った時の大きさで黙って当たる
        for (const HitSensor* sensor : m_sensors)
        {
            const ReflectionInfo* info = sensor->GetReflection();
            if (info != nullptr && info->fieldCount > 0 && !values->contains(sensor->Name()))
            {
                NS_LOG_WARN(Scene,
                            "{}.actor の hitSensors に {} のセンサー {} が無いので、作った時の値で当たる",
                            owner->ActorFileName(),
                            owner->ClassName(),
                            sensor->Name());
            }
        }
        for (nlohmann::json::const_iterator entry = values->begin(); entry != values->end(); ++entry)
        {
            HitSensor* sensor = Find(entry.key());
            if (sensor == nullptr)
            {
                NS_LOG_WARN(Scene,
                            "{}.actor の hitSensors に、{} が持たないセンサー {} があり、読まない",
                            owner->ActorFileName(),
                            owner->ClassName(),
                            entry.key());
                continue;
            }
            (void)ApplyJsonFields(sensor, entry.value()); // 知らない欄は ApplyJsonFields が WARN で知らせる
        }
    }

    void HitSensorsObj::WriteValues(nlohmann::json& actorFile) const
    {
        // 読まなかった名前の値は字面のまま残す。捨てると、直していない本人の値が .actor から消える
        nlohmann::json values = nlohmann::json::object();
        const nlohmann::json* current = ActorFileHitSensors(actorFile);
        if (current != nullptr && current->is_object())
        {
            values = *current;
        }
        for (const HitSensor* sensor : m_sensors)
        {
            const ReflectionInfo* info = sensor->GetReflection();
            if (info == nullptr || info->fieldCount == 0)
            {
                continue;
            }
            values[sensor->Name()] = SerializeClassObjFields(sensor);
        }
        SetActorFileHitSensors(actorFile, std::move(values));
    }
} // namespace NS
