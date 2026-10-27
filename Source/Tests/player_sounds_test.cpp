#include "Game/Player/PlayerSounds.h"
#include "NSlib/CoreObject/IGet/IGetAudio.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"

#include <gtest/gtest.h>

namespace
{
    // 部品 Sound も装置も持たないので、鳴らす関数は何もしない
    class SilentAudio final : public NS::IGetAudio
    {
    public:
        [[nodiscard]] NS::Sound* GetSound() const noexcept override { return nullptr; }
        [[nodiscard]] NS::Audio::AudioDevice* GetAudioDevice() const noexcept override { return nullptr; }
    };

    constexpr float k_Dt = 1.0f / 60.0f;
} // namespace

// 接地が細かく切れて着地が続いても、着地の音は最短の間隔をあけてからでないと次を頼まない
TEST(PlayerSoundsTest, LandingAgainWithinTheShortestIntervalDoesNotRequestTheSound)
{
    const SilentAudio audio;
    GL::Player::PlayerSounds sounds;
    ASSERT_EQ(NS::ApplyJsonFields(&sounds, nlohmann::json{{"着地の音の最短の間隔", 0.1f}}), 0u);

    sounds.AllowLand();
    sounds.RequestLand();
    EXPECT_TRUE(sounds.IsLandRequested());
    sounds.Update(&audio, k_Dt);

    sounds.AllowLand();
    sounds.RequestLand();
    EXPECT_FALSE(sounds.IsLandRequested());

    for (int i = 0; i < 7; ++i)
    {
        sounds.Update(&audio, k_Dt);
    }
    sounds.AllowLand();
    sounds.RequestLand();
    EXPECT_TRUE(sounds.IsLandRequested());
}
