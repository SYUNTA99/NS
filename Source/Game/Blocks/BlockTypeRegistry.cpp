#include "Game/Blocks/BlockTypeRegistry.h"

#include "Game/Blocks/BlockRegistry.h"
#include "Game/Level/LevelData.h"

#include <array>
#include <string>
#include <utility>

namespace NS::Game::Blocks
{
    namespace
    {
        using NS::Game::Level::ComponentData;
        using NS::Game::Level::FieldValue;
        using NS::Game::Level::ObjectInstance;

        // 1m grid セルの半径。 grid 配置物の当たり箱と wedge 半サイズに使う
        constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};

        // 掴み判定が現挙動を保つためのポール寸法。 PoleComponent 既定と高さが異なる
        constexpr float kPoleRadius = 0.15f;
        constexpr float kPoleHeight = 1.0f;

        ComponentData Make(std::string typeName, std::vector<FieldValue> fields)
        {
            ComponentData component;
            component.typeName = std::move(typeName);
            component.fields = std::move(fields);
            return component;
        }

        // 種別色を載せた MeshRenderer を組む。 grid 描画は mesh / material 固定で色だけ kind 由来
        ComponentData MeshData(std::string mesh, std::string material, std::uint16_t kind)
        {
            const NS::Math::Color color = GetBaseColor(kind);
            const NS::Math::Vector3 baseColor{color.R(), color.G(), color.B()};
            return Make("MeshRendererComponent",
                        {FieldValue{"Mesh", std::move(mesh)},
                         FieldValue{"Material", std::move(material)},
                         FieldValue{"Base Color", baseColor}});
        }

        std::vector<ComponentData> SolidRecipe(const ObjectInstance&)
        {
            return {MeshData("cube", "block", kBlockIdSolid),
                    Make("BoxColliderComponent", {FieldValue{"Half Extents", kCellHalfExtents}})};
        }

        // slope は mesh と角度だけ違う同型。 4 種それぞれの記述子から共通本体を呼ぶ
        std::vector<ComponentData> SlopeRecipe(std::uint16_t kind, std::string mesh)
        {
            return {MeshData(std::move(mesh), "block", kind),
                    Make("SlopeColliderComponent",
                         {FieldValue{"Angle (deg)", GetSlopeAngleDegrees(kind)},
                          FieldValue{"Half Extents", kCellHalfExtents}})};
        }
        std::vector<ComponentData> Slope45Recipe(const ObjectInstance&)
        {
            return SlopeRecipe(kBlockIdSlope45, "wedge45");
        }
        std::vector<ComponentData> Slope30Recipe(const ObjectInstance&)
        {
            return SlopeRecipe(kBlockIdSlope30, "wedge30");
        }
        std::vector<ComponentData> Slope22Recipe(const ObjectInstance&)
        {
            return SlopeRecipe(kBlockIdSlope22, "wedge22");
        }
        std::vector<ComponentData> Slope15Recipe(const ObjectInstance&)
        {
            return SlopeRecipe(kBlockIdSlope15, "wedge15");
        }

        std::vector<ComponentData> PoleRecipe(const ObjectInstance&)
        {
            return {MeshData("pole", "block", kBlockIdPole),
                    Make("PoleComponent", {FieldValue{"Radius", kPoleRadius}, FieldValue{"Height", kPoleHeight}})};
        }

        std::vector<ComponentData> HazardRecipe(const ObjectInstance&)
        {
            return {MeshData("cube", "block", kBlockIdHazard),
                    Make("BoxColliderComponent", {FieldValue{"Half Extents", kCellHalfExtents}}),
                    Make("HazardComponent", {})};
        }

        std::vector<ComponentData> WaterRecipe(const ObjectInstance&)
        {
            return {MeshData("cube", "water", kBlockIdWater)};
        }

        std::vector<ComponentData> DecorationRecipe(const ObjectInstance&)
        {
            return {MeshData("cube", "block", kBlockIdDecoration)};
        }

        // コイン / スターは視覚も当たりも持たず、 拾得の意味だけを PickupComponent で表す
        std::vector<ComponentData> CoinRecipe(const ObjectInstance&)
        {
            return {Make("PickupComponent", {FieldValue{"Pickup Kind", 0}})};
        }

        std::vector<ComponentData> StarRecipe(const ObjectInstance&)
        {
            return {Make("PickupComponent", {FieldValue{"Pickup Kind", 1}})};
        }
    } // namespace

    std::span<const BlockTypeDescriptor> BlockTypeDescriptors() noexcept
    {
        // paletteSlot を絞ると toolbar 並びの Solid / Coin / Star / Spawn / Slope45 / Pole / Hazard / Water になる
        static const std::array<BlockTypeDescriptor, 12> descriptors = {{
            {kBlockIdSolid, "Solid", false, true, &SolidRecipe},
            {kBlockIdCoin, "Coin", false, true, &CoinRecipe},
            {kBlockIdPowerStar, "Star", false, true, &StarRecipe},
            {kBlockIdSpawn, "Spawn", true, true, nullptr},
            {kBlockIdSlope45, "Slope 45", false, true, &Slope45Recipe},
            {kBlockIdSlope30, "Slope 30", false, false, &Slope30Recipe},
            {kBlockIdSlope22, "Slope 22.5", false, false, &Slope22Recipe},
            {kBlockIdSlope15, "Slope 15", false, false, &Slope15Recipe},
            {kBlockIdPole, "Pole", false, true, &PoleRecipe},
            {kBlockIdHazard, "Hazard", false, true, &HazardRecipe},
            {kBlockIdWater, "Water", false, true, &WaterRecipe},
            {kBlockIdDecoration, "Decoration", false, false, &DecorationRecipe},
        }};
        return descriptors;
    }

    const BlockTypeDescriptor* FindBlockType(std::uint16_t id) noexcept
    {
        for (const BlockTypeDescriptor& desc : BlockTypeDescriptors())
            if (desc.id == id)
                return &desc;
        return nullptr;
    }
} // namespace NS::Game::Blocks
