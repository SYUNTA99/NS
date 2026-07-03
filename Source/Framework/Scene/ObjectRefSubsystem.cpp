#include "Framework/Scene/ObjectRefSubsystem.h"

#include "Framework/Scene/SceneBase.h"
#include "Framework/Scene/SubsystemRegistry.h"

namespace NS::Scene
{
    void ObjectRefSubsystem::Register(std::uint32_t id, GameObject* object)
    {
        if (id == 0 || object == nullptr)
            return;
        m_objects[id] = object;
    }

    GameObject* ObjectRefSubsystem::Resolve(ObjectRef ref) const noexcept
    {
        if (!ref.IsSet())
            return nullptr;
        const auto it = m_objects.find(ref.id);
        return (it != m_objects.end()) ? it->second : nullptr;
    }

    // オブジェクト間参照はどのシーンでも使い得るため常時生成する
    NS_REGISTER_SUBSYSTEM(ObjectRefSubsystem, SubsystemTier::Scene, [](const SceneBase&) { return true; })
} // namespace NS::Scene
