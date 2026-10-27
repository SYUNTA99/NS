#include "NSlib/CoreObject/SensorDeclarations.h"

#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    void SensorDeclarations::ShapeOfKind(std::unique_ptr<HitSensor>& member, uint8 kind, HitSensorShape shape)
    {
        m_owner->CreateShapeSensor(member, kind, shape);
    }

    void SensorDeclarations::FollowOfKind(std::unique_ptr<HitSensor>& member,
                                          uint8 kind,
                                          std::function<SensorVolume(const Matrix& rootWorld)> volume)
    {
        m_owner->CreateFollowSensor(member, kind, std::move(volume));
    }
} // namespace NS
