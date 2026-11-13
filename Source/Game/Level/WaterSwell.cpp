#include "Game/Level/WaterSwell.h"

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/Asset/Material.h"

#include <cmath>
#include <cstddef>
#include <string_view>

namespace GL
{
    namespace Level
    {
        namespace
        {
            // name の値の index 番目の数。名前が無い・数が足りない時は 0
            [[nodiscard]] float ParamNumber(const NS::MaterialParamValues& params, std::string_view name, std::size_t index)
            {
                const NS::MaterialParamValues::const_iterator found = params.find(std::string{name});
                if (found == params.end() || found->second.numbers.size() <= index)
                {
                    return 0.0f;
                }
                return static_cast<float>(found->second.numbers[index]);
            }

            [[nodiscard]] WaterSwellWave ReadWave(const NS::MaterialParamValues& params, std::string_view name)
            {
                return WaterSwellWave{.angle = ParamNumber(params, name, 0),
                                      .wavelength = ParamNumber(params, name, 1),
                                      .weight = ParamNumber(params, name, 2)};
            }
        } // namespace

        WaterSwell ReadWaterSwell(const NS::Material* material)
        {
            if (material == nullptr || material->IsEmpty())
            {
                return WaterSwell{};
            }
            const NS::MaterialParamValues& params = material->File().params;
            return WaterSwell{.height = ParamNumber(params, "swellHeight", 0),
                              .speed = ParamNumber(params, "swellSpeed", 0),
                              .gravity = ParamNumber(params, "swellGravity", 0),
                              .directionX = ParamNumber(params, "swellDirection", 0),
                              .directionZ = ParamNumber(params, "swellDirection", 1),
                              .waves = {ReadWave(params, "swellWaveA"),
                                        ReadWave(params, "swellWaveB"),
                                        ReadWave(params, "swellWaveC")}};
        }

        float WaterSwellHeight(const WaterSwell& swell, float x, float z, float timeSeconds) noexcept
        {
            float height = 0.0f;
            for (const WaterSwellWave& wave : swell.waves)
            {
                if (wave.wavelength <= 0.0f)
                {
                    continue;
                }
                // うねりの向きを angle だけ回した長さ 1 の向き。向きが 0 の時は 0 のまま (シェーダーと同じ)
                const float sine = std::sin(wave.angle);
                const float cosine = std::cos(wave.angle);
                float directionX = swell.directionX * cosine - swell.directionZ * sine;
                float directionZ = swell.directionX * sine + swell.directionZ * cosine;
                const float length = std::sqrt(directionX * directionX + directionZ * directionZ);
                if (length > NS::k_Epsilon)
                {
                    directionX /= length;
                    directionZ /= length;
                }
                const float waveNumber = NS::k_Pi * 2.0f / wave.wavelength;
                const float omega = std::sqrt(swell.gravity * waveNumber) * swell.speed;
                const float phase = (x * directionX + z * directionZ) * waveNumber - timeSeconds * omega;
                height += std::sin(phase) * swell.height * wave.weight;
            }
            return height;
        }
    } // namespace Level
} // namespace GL
