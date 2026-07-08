#include "Framework/Scene/CameraSubsystem.h"

#include "Framework/Scene/Components/CameraBrainComponent.h"
#include "Framework/Scene/Components/CameraComponent.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/SceneBase.h"
#include "Framework/Scene/SubsystemRegistry.h"

namespace NS::Scene
{
    CameraSubsystem::CameraSubsystem() = default;
    CameraSubsystem::~CameraSubsystem() = default;

    void CameraSubsystem::Initialize(SceneBase& scene) noexcept
    {
        // 実カメラ 1 個 + Brain を載せる host。Brain が登録 vcam から選んで実カメラへ書く
        m_host = std::make_unique<GameObject>();
        auto* camera = m_host->AddComponent<CameraComponent>();
        camera->SetUp({0.0f, 1.0f, 0.0f});
        m_brain = m_host->AddComponent<CameraBrainComponent>();
        m_host->AttachScene(&scene);
        m_host->OnStart();
    }

    void CameraSubsystem::Deinitialize() noexcept
    {
        if (m_host)
            m_host->OnEndPlay();
        m_brain = nullptr;
        m_host.reset();
    }

    CameraComponent* CameraSubsystem::MainCamera() const noexcept
    {
        if (m_brain != nullptr)
            return m_brain->Camera();
        return nullptr;
    }

    // カメラはどのシーンにも 1 系統あるため常時生成する
    NS_REGISTER_SUBSYSTEM(CameraSubsystem, SubsystemTier::Scene, [](const SceneBase&) { return true; })
} // namespace NS::Scene
