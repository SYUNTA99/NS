#include "Editor/EditorObjects.h"

#include "Runtime/Graphics/Mesh.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/ObjectList.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"

namespace NS::Editor
{
    namespace
    {
        // cell ブラシの回転値 0..3 を Y 軸 90° 刻みの yaw ラジアンへ写す係数
        constexpr float k_QuarterTurnYaw = NS::Core::k_Pi * 0.5f;

        // クラス名から UI に出す名前を引く。置けるクラスは登録の表示名、置けないクラスはクラス名
        // 戻り値は登録か JSON の文字列を指すので、呼出側が持っている間は切れない
        const char* ClassDisplayName(std::string_view className) noexcept
        {
            if (className.empty())
                return "Actor";
            const NS::Obj::TypeRegistry::Entry* entry = NS::Obj::TypeRegistry::Get().Find(className);
            if (entry == nullptr)
                return "Actor";
            return entry->label != nullptr ? entry->label : entry->className;
        }
    } // namespace

    std::int16_t ObjectCellX(const nlohmann::json& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(NS::Obj::ObjectPosition(object).x));
    }

    std::int16_t ObjectCellY(const nlohmann::json& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(NS::Obj::ObjectPosition(object).y));
    }

    std::int16_t ObjectCellZ(const nlohmann::json& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(NS::Obj::ObjectPosition(object).z));
    }

    bool IsCellBrushObject(const nlohmann::json& object) noexcept
    {
        // 自前の振る舞いを持つ Actor は、ブラシの置換や削除で崩さない
        return NS::Obj::ObjectJsonClass(object) == "MapParts";
    }

    bool IsCellBrushObject(const NS::Obj::Actor& object) noexcept
    {
        // 実行時の一時オブジェクトは配置物でないため対象外
        return !object.IsTransient() && std::string_view{object.ClassName()} == "MapParts";
    }

    std::int16_t ObjectCellX(const NS::Obj::Actor& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.Root().Position().x));
    }

    std::int16_t ObjectCellY(const NS::Obj::Actor& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.Root().Position().y));
    }

    std::int16_t ObjectCellZ(const NS::Obj::Actor& object) noexcept
    {
        return static_cast<std::int16_t>(std::lround(object.Root().Position().z));
    }

    std::uint32_t FindObjectIdAtCell(const NS::Obj::ObjectList& objects,
                                     std::int16_t x,
                                     std::int16_t y,
                                     std::int16_t z) noexcept
    {
        for (NS::Obj::Actor* objPtr : objects)
        {
            NS::Obj::Actor& object = *objPtr;
            if (IsCellBrushObject(object) && ObjectCellX(object) == x && ObjectCellY(object) == y &&
                ObjectCellZ(object) == z)
            {
                return object.Id();
            }
        }
        return NS::Obj::k_NoObjectId;
    }

    std::uint8_t CellRotationStep(const nlohmann::json& object) noexcept
    {
        // q と -q は同じ回転なので fabs で符号を無視し、4 候補から一番近いものを選ぶ
        const NS::Core::Quaternion current = NS::Obj::ObjectRotation(object);
        std::uint8_t best = 0;
        float bestDot = -2.0f;
        for (std::uint8_t step = 0; step < 4; ++step)
        {
            const float yaw = static_cast<float>(step) * k_QuarterTurnYaw;
            const NS::Core::Quaternion candidate = NS::Core::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
            const float dot = std::fabs(current.x * candidate.x + current.y * candidate.y + current.z * candidate.z +
                                        current.w * candidate.w);
            if (dot > bestDot)
            {
                bestDot = dot;
                best = step;
            }
        }
        return best;
    }

    void SetCellRotationStep(nlohmann::json& object, std::uint8_t rotationStep) noexcept
    {
        const float yaw = static_cast<float>(rotationStep & 0x03) * k_QuarterTurnYaw;
        const NS::Core::Quaternion rotation = NS::Core::Quaternion::CreateFromYawPitchRoll(yaw, 0.0f, 0.0f);
        NS::Obj::SetObjectRotation(object, rotation);
    }

    bool IsRotatableObject(const nlohmann::json& object)
    {
        // 地形の部品はどれも回せる。球は回しても見た目が変わらないだけ
        return IsCellBrushObject(object);
    }

    const char* ObjectDisplayName(const nlohmann::json& object)
    {
        // 名前は JSON の中の文字列を指す。std::string の中身なので終端がある
        const std::string_view name = NS::Obj::ObjectJsonName(object);
        if (!name.empty())
            return name.data();
        return ClassDisplayName(NS::Obj::ObjectJsonClass(object));
    }

    const char* ObjectDisplayName(const NS::Obj::Actor& object)
    {
        if (!object.Name().empty())
            return object.Name().c_str();
        return ClassDisplayName(object.ClassName());
    }

    NS::Core::AABB PickLocalBounds(const NS::Obj::Actor& object) noexcept
    {
        const NS::Obj::MeshRenderer* renderer = object.FindComponent<NS::Obj::MeshRenderer>();
        if (renderer != nullptr && renderer->GetMesh() != nullptr)
            return renderer->GetMesh()->LocalBounds();
        return NS::Core::AABB{NS::Core::Vector3{0.0f, 0.0f, 0.0f}, NS::Core::Vector3{0.5f, 0.5f, 0.5f}};
    }

} // namespace NS::Editor
