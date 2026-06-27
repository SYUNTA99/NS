#include "Editor/PaletteTemplates.h"

#include "Game/Blocks/BlockRegistry.h"
#include "Game/Blocks/BlockTypeRegistry.h"
#include "Game/Blocks/BuildPlacedObject.h"

#include <utility>

namespace NS::Editor
{
    namespace
    {
        // 表示名は種別記述子から引く。 種別の表示名定義はそちらへ一本化する
        const char* NameForKind(std::uint16_t kind) noexcept
        {
            const NS::Game::Blocks::BlockTypeDescriptor* desc = NS::Game::Blocks::FindBlockType(kind);
            return desc != nullptr ? desc->displayName : "?";
        }
    } // namespace

    PaletteTemplate PaletteTemplateForKind(std::uint16_t kind) noexcept
    {
        PaletteTemplate tmpl{};
        tmpl.name = NameForKind(kind);
        tmpl.isSpawn = (kind == NS::Game::Blocks::kBlockIdSpawn);

        if (tmpl.isSpawn)
        {
            // spawn は LevelData::spawnX/Y/Z を上書きする marker で、 grid object として積まない
            NS::Game::Level::ObjectInstance marker{};
            marker.materialIndex = -1;
            marker.flags = 0;
            tmpl.rotatable = false;
            tmpl.prototype = std::move(marker);
            return tmpl;
        }

        // prototype は cell 原点の grid 配置物。 kind が決める mesh / 当たり / 拾得を実 component へ展開して持つ
        NS::Game::Level::ObjectInstance prototype = NS::Game::Level::MakeGridObject(0, 0, 0, 0);
        prototype.components = NS::Game::Blocks::MaterializeLegacyKind(kind, prototype);
        // 回転可否は組み上がった component から導く (slope か grid 固形)
        tmpl.rotatable = NS::Game::Blocks::IsRotatableObject(prototype);
        tmpl.prototype = std::move(prototype);
        return tmpl;
    }

    const char* SlopeVariantName(std::uint16_t slopeKind) noexcept
    {
        if (!NS::Game::Blocks::IsSlopeBlock(slopeKind))
            return "Slope";
        const NS::Game::Blocks::BlockTypeDescriptor* desc = NS::Game::Blocks::FindBlockType(slopeKind);
        return desc != nullptr ? desc->displayName : "Slope";
    }

    const std::array<PaletteTemplate, 8>& PaletteTemplateSlots() noexcept
    {
        // 登録簿の paletteSlot 並び順がそのまま toolbar スロット。 CategoryPalette の slot 並びと一致を保つ
        static const std::array<PaletteTemplate, 8> slots = []() {
            std::array<PaletteTemplate, 8> result{};
            auto out = result.begin();
            for (const NS::Game::Blocks::BlockTypeDescriptor& desc : NS::Game::Blocks::BlockTypeDescriptors())
                if (desc.paletteSlot && out != result.end())
                    *out++ = PaletteTemplateForKind(desc.id);
            return result;
        }();
        return slots;
    }
} // namespace NS::Editor
