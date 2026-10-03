#include "Game/Level/Goal.h"

#include "Game/Level/LevelMessages.h"
#include "Runtime/Object/Components/HitSensor.h"
#include "Runtime/Object/Components/Model.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Game::Level
{
    Goal::Goal() noexcept
    {
        // 目印の金色の立方体
        (void)CreatePart("Model");
        NS::Obj::Model* mesh = ModelPart();
        mesh->SetMeshRef("cube");
        mesh->SetBaseColor(NS::Core::Vector3{1.0f, 0.84f, 0.0f});
        // 「触れた」とみなすプレイヤー中心からの距離 0.9m の球。プレイヤーの体の寸法ぶん手前で触れる
        NS::Obj::ShapeHitSensor* area = NS::Obj::ComponentCast<NS::Obj::ShapeHitSensor>(CreatePart("BodySensor"));
        area->SetType(NS::Obj::HitSensorType::Area);
        area->SetSphere(0.9f);
    }

    void Goal::AttackSensor(NS::Obj::HitSensor& self, NS::Obj::HitSensor& other)
    {
        // 触れている間は毎フレーム送る。流れの最中の知らせは進行役が捨てる
        if (other.Type() == NS::Obj::HitSensorType::PlayerBody)
        {
            (void)SendMsgGoal(other, self);
        }
    }

    bool IsGoalObject(const nlohmann::json& object) noexcept
    {
        return NS::Obj::ObjectJsonClass(object) == "Goal";
    }

    NS_PLACEABLE(Goal, "ゴール")
} // namespace NS::Game::Level
