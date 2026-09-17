#include "Game/Level/BlockObject.h"

#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"

namespace NS::Game::Level
{
    nlohmann::json MakeMeshRendererEntry(std::string_view meshName,
                                         std::string_view materialName,
                                         const NS::Core::Vector3& baseColor)
    {
        nlohmann::json entry = NS::Object::MakeComponentEntry("MeshRendererComponent");
        NS::Object::SetField(entry, "メッシュ", meshName);
        NS::Object::SetField(entry, "マテリアル", materialName);
        NS::Object::SetField(entry, "基本色", baseColor);
        return entry;
    }

    nlohmann::json MakeCellCubeComponents()
    {
        nlohmann::json box = NS::Object::MakeComponentEntry("BoxColliderComponent");
        NS::Object::SetField(box, "半径", k_CellHalfExtents);
        return nlohmann::json::array({MakeMeshRendererEntry("cube", "", k_SolidBaseColor), std::move(box)});
    }

    NS::Object::ObjectData MakeCellObject(std::int16_t x, std::int16_t y, std::int16_t z)
    {
        NS::Object::ObjectData object{};
        object.components = MakeCellCubeComponents();
        // components を確定した後に transform を書き込む。 先に書くと components 代入が TransformComponent を消す
        NS::Object::SetObjectPosition(object, NS::Core::Vector3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
        return object;
    }
} // namespace NS::Game::Level
