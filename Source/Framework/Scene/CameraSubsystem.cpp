#include "Framework/Scene/CameraSubsystem.h"

#include "Framework/Scene/Components/CameraBrainComponent.h"
#include "Framework/Scene/SceneBase.h"
#include "Framework/Scene/SubsystemRegistry.h"

namespace NS::Scene
{
    CameraComponent* CameraSubsystem::MainCamera() const noexcept
    {
        return (m_brain != nullptr) ? m_brain->Camera() : nullptr;
    }

    // カメラはどのシーンにも 1 系統あるため常時生成する
    NS_REGISTER_SUBSYSTEM(CameraSubsystem, SubsystemTier::Scene, [](const SceneBase&) { return true; })
} // namespace NS::Scene
