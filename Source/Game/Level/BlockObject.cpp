#include "Game/Level/BlockObject.h"

#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"

namespace NS::Game::Level
{
    nlohmann::json MakeMeshRendererEntry(std::string_view meshName,
                                         std::string_view materialName,
                                         const NS::Core::Vector3& baseColor)
    {
        nlohmann::json entry = NS::Obj::MakeComponentEntry("MeshRenderer");
        NS::Obj::SetField(entry, "メッシュ", meshName);
        NS::Obj::SetField(entry, "マテリアル", materialName);
        NS::Obj::SetField(entry, "基本色", baseColor);
        return entry;
    }

    nlohmann::json MakeCellCubeComponents()
    {
        nlohmann::json box = NS::Obj::MakeComponentEntry("BoxCollider");
        // 1m 立方の cube mesh に当たりを合わせる
        NS::Obj::SetField(box, "半径", NS::Core::Vector3{0.5f, 0.5f, 0.5f});
        // 色はテクスチャ未解決時のフォールバック
        return nlohmann::json::array(
            {MakeMeshRendererEntry("cube", "", NS::Core::Vector3{0.70f, 0.70f, 0.75f}), std::move(box)});
    }

    NS::Obj::ObjectData MakeCellObject(std::int16_t x, std::int16_t y, std::int16_t z)
    {
        NS::Obj::ObjectData object{};
        object.components = MakeCellCubeComponents();
        // components を確定した後に transform を書き込む。先に書くと components 代入が TransformComponent を消す
        NS::Obj::SetObjectPosition(object, NS::Core::Vector3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
        return object;
    }
} // namespace NS::Game::Level
