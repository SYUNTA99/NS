#include "Game/Blocks/AutoTile.h"

#include "Framework/Graphics/TextureArray.h"
#include "Game/Level/LevelData.h"
#include "Game/Theme/ThemeRegistry.h"

#include <cstddef>
#include <string>
#include <variant>

using namespace NS::Game::Theme;

namespace NS::Game::Blocks
{

    namespace
    {
        // 64 通りの 6-neighbor bitmask を popcount ベースで 8 variant に縮約するテーブル。 variant は 0=孤立 から
        // 7=完全埋没 まで 上下軸 bit 2 / 3 の有無で同 popcount でも variant をずらして天井面を区別する
        constexpr std::uint8_t kBitmaskToVariant[64] = {
            0, 1, 1, 2, 1, 2, 2, 3, // 0..7
            3, 4, 4, 5, 4, 5, 5, 6, // 8..15  bit 3 = -Y で床に埋まる
            1, 2, 2, 3, 2, 3, 3, 4, // 16..23 bit 4 = +Z
            4, 5, 5, 6, 5, 6, 6, 7, // 24..31
            1, 2, 2, 3, 2, 3, 3, 4, // 32..39 bit 5 = -Z
            4, 5, 5, 6, 5, 6, 6, 7, // 40..47
            2, 3, 3, 4, 3, 4, 4, 5, // 48..55 +Z と -Z で通路
            5, 6, 6, 7, 6, 7, 7, 7, // 56..63 完全埋没側
        };
        constexpr std::uint8_t kVariantsPerTheme = 8;

        // 連結判定に使う視覚キー。 メッシュ参照 + マテリアル参照 + マテリアル添字が一致する隣接だけを連結扱いにする
        // materialRef は MeshRenderer の "Material" 文字列。 solid("block") と water("water") は mesh / 添字が同じでも
        // 見た目が違うので連結させない
        struct VisualTileKey
        {
            std::string mesh;
            std::string materialRef;
            int materialIndex = -1;

            [[nodiscard]] bool operator==(const VisualTileKey& other) const noexcept
            {
                return materialIndex == other.materialIndex && mesh == other.mesh && materialRef == other.materialRef;
            }
        };

        // object の視覚キーを導く。 MeshRenderer の反射 "Mesh" / "Material" 値とマテリアル添字で連結同一性を決める
        // MeshRenderer を持たない object (coin / star 等) は空メッシュキーになり solid と連結しない
        VisualTileKey ComputeVisualKey(const NS::Game::Level::ObjectInstance& object)
        {
            VisualTileKey key;
            key.materialIndex = object.materialIndex;
            const NS::Game::Level::ComponentData* renderer =
                NS::Game::Level::FindComponentData(object, "MeshRendererComponent");
            if (renderer == nullptr)
                return key; // MeshRenderer 無し (coin / star 等) は空メッシュキーで solid と連結しない
            if (const NS::Game::Level::FieldValue* mesh = NS::Game::Level::FindField(*renderer, "Mesh"))
                if (const auto* meshName = std::get_if<std::string>(&mesh->value))
                    key.mesh = *meshName;
            if (const NS::Game::Level::FieldValue* material = NS::Game::Level::FindField(*renderer, "Material"))
                if (const auto* matRef = std::get_if<std::string>(&material->value))
                    key.materialRef = *matRef;
            return key;
        }
    } // namespace

    std::uint16_t LookupTextureSlice(ThemeId theme, std::uint8_t neighborMask) noexcept
    {
        // theme 範囲外 → Grass (入力境界の fallback)
        if (static_cast<std::size_t>(theme) >= static_cast<std::size_t>(ThemeId::Count))
        {
            theme = ThemeId::Grass;
        }
        // bitmask 範囲外 (>=64) は base slice (offset 0) にフォールバック
        if (neighborMask >= 64)
        {
            const ThemeData& td0 = Get(theme);
            const std::uint16_t base0 = td0.blockTextureArrayBaseSlice;
            return base0 < NS::Graphics::TextureArray::kTotalSlices ? base0 : static_cast<std::uint16_t>(0);
        }

        const ThemeData& td = Get(theme);
        const std::uint16_t base = td.blockTextureArrayBaseSlice;
        const std::uint8_t variant = kBitmaskToVariant[neighborMask];
        // variant が 8 を超えないテーブルを書いてあるが、 念のため clamp する
        const std::uint8_t safeVariant = variant < kVariantsPerTheme ? variant : static_cast<std::uint8_t>(0);
        const std::uint32_t slice = static_cast<std::uint32_t>(base) + static_cast<std::uint32_t>(safeVariant);
        if (slice >= NS::Graphics::TextureArray::kTotalSlices)
        {
            return 0; // 念のため範囲外 clamp
        }
        return static_cast<std::uint16_t>(slice);
    }

    std::uint8_t ComputeNeighborMask(const NS::Game::Level::LevelData& level,
                                     std::int16_t x,
                                     std::int16_t y,
                                     std::int16_t z) noexcept
    {
        // bit 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z の順で 6 方向
        static constexpr std::int16_t kOffsets[6][3] = {
            {+1, 0, 0},
            {-1, 0, 0},
            {0, +1, 0},
            {0, -1, 0},
            {0, 0, +1},
            {0, 0, -1},
        };

        const std::size_t center = NS::Game::Level::FindGridObjectAtCell(level, x, y, z);
        if (center == NS::Game::Level::kNoObjectIndex)
            return 0;
        const VisualTileKey centerKey = ComputeVisualKey(level.objects[center]);

        std::uint8_t mask = 0;
        for (int i = 0; i < 6; ++i)
        {
            const std::int16_t nx = static_cast<std::int16_t>(x + kOffsets[i][0]);
            const std::int16_t ny = static_cast<std::int16_t>(y + kOffsets[i][1]);
            const std::int16_t nz = static_cast<std::int16_t>(z + kOffsets[i][2]);
            const std::size_t neighbor = NS::Game::Level::FindGridObjectAtCell(level, nx, ny, nz);
            if (neighbor != NS::Game::Level::kNoObjectIndex && ComputeVisualKey(level.objects[neighbor]) == centerKey)
            {
                mask = static_cast<std::uint8_t>(mask | (1u << i));
            }
        }
        return mask;
    }

    void SetSpawnMarker(NS::Game::Level::LevelData& level, std::int16_t x, std::int16_t y, std::int16_t z) noexcept
    {
        level.spawnX = x;
        level.spawnY = y;
        level.spawnZ = z;
    }

} // namespace NS::Game::Blocks
