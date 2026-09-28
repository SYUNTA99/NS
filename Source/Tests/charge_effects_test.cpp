#include "Editor/EditorObjects.h"
#include "Game/Player.h"

#include <Game/Level/CollisionInput.h>
#include <Game/Level/ImpactInputJudge.h>
#include <Game/Level/ImpactResolver.h>
#include <Game/Player/ChargeEffects.h>
#include <Game/Player/EffectLayerList.h>
#include <Game/Player/PlayerComponent.h>
#include <Runtime/Core/CameraData.h>
#include <Runtime/Core/Logger.h>
#include <Runtime/Core/Math.h>
#include <Runtime/Graphics/EffectScene.h>
#include <Runtime/Graphics/GraphicObject.h>
#include <Runtime/Graphics/RenderTarget.h>
#include <Runtime/Graphics/Renderer.h>
#include <Runtime/Graphics/Texture.h>
#include <Runtime/Object/Components/PlayerInput.h>
#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/ObjectList.h>
#include <Runtime/Object/Reflection/ComponentEntry.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Platform/Clock.h>
#include <Runtime/Platform/Filesystem.h>
#include <Runtime/Platform/Window.h>

#include "hit_zones_entry.h"
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using NS::Core::Vector3;
    using NS::Game::Player::ChargeEffects;
    using NS::Game::Player::EffectLayerRecord;

    constexpr float k_FixedDt = 1.0f / 60.0f;
    // 押してから溜めに入るまでと、溜めきるまでのフレーム数。欄「チャージしきい値秒」0.2 (12 フレーム) と
    // 「チャージ満タン秒」1.0 (60 フレーム) の既定。押したフレームを保持の 1 フレーム目と数える
    constexpr int k_ChargingAfterPress = 11;
    constexpr int k_FullAfterPress = 59;
    // 突進を出す向き。的は +X に置く
    constexpr Vector3 k_SlamDirection{1.0f, 0.0f, 0.0f};

    struct ChargeRig
    {
        NS::Game::Player::PlayerComponent* player = nullptr;
        NS::Game::Level::CollisionInput* input = nullptr;
        NS::Game::Level::ImpactResolver* resolver = nullptr;
        ChargeEffects* effects = nullptr;
    };

    // 置かれた壊せる物と同じ RigidBody。飛ぶまではキネマティック。当たりの相手は Breakable を持つ物
    nlohmann::json MakeLaunchableRigidBodyEntry()
    {
        nlohmann::json entry = NS::Obj::MakeComponentEntry("RigidBody");
        NS::Obj::SetField(entry, "キネマティック", true);
        return entry;
    }

    // 床の上の自機と、withTarget なら +X の 6 m 先に当たる的を置く。押しは試しが判定へ直に入れるので、
    // 実機の入力を読む CollisionInput と PlayerInput は寝かせる
    ChargeRig BuildRig(NS::Obj::Scene& scene, bool withTarget)
    {
        NS::Platform::FrameTimer::SetFixedDelta(k_FixedDt);
        nlohmann::json data = NS::Obj::MakeSceneJson();
        nlohmann::json player = MakePlayerObject(Vector3{0.0f, 1.41f, 0.0f}, NS::Core::Quaternion{});
        NS::Obj::ObjectJsonComponents(player).push_back(NS::Obj::MakeComponentEntry("ImpactResolver"));
        NS::Obj::ObjectJsonComponents(player).push_back(NS::Obj::MakeComponentEntry("CollisionInput"));
        NS::Obj::ObjectJsonComponents(player).push_back(NS::Obj::MakeComponentEntry("ChargeEffects"));
        NS::Obj::SceneJsonObjects(data).push_back(player);
        for (std::int16_t i = -3; i <= 40; ++i)
        {
            NS::Obj::SceneJsonObjects(data).push_back(NS::Editor::MakeCellObject(i, 0, 0));
        }
        if (withTarget)
        {
            nlohmann::json target = NS::Editor::MakeCellObject(6, 1, 0);
            NS::Obj::ObjectJsonComponents(target).push_back(MakeLaunchableRigidBodyEntry());
            NS::Obj::ObjectJsonComponents(target).push_back(NS::Obj::MakeComponentEntry("Breakable"));
            NS::Obj::ObjectJsonComponents(target).push_back(NsTest::MakeTestHitZonesEntry());
            NS::Obj::SceneJsonObjects(data).push_back(target);
        }
        scene.LoadJson(std::move(data));

        ChargeRig rig;
        Player* live = FindPlayer(scene.Objects());
        EXPECT_NE(live, nullptr);
        if (live == nullptr)
        {
            return rig;
        }
        rig.player = live->FindComponent<NS::Game::Player::PlayerComponent>();
        rig.input = live->FindComponent<NS::Game::Level::CollisionInput>();
        rig.resolver = live->FindComponent<NS::Game::Level::ImpactResolver>();
        rig.effects = live->FindComponent<ChargeEffects>();
        if (NS::Obj::PlayerInput* input = live->FindComponent<NS::Obj::PlayerInput>())
        {
            input->SetActive(false);
        }
        if (rig.input != nullptr)
        {
            rig.input->SetActive(false);
        }
        return rig;
    }

    // 1 フレームを回す。押しを判定へ入れ、放したフレームは CollisionInput と同じく溜め量を添えて突進を要求する
    void StepFrame(NS::Obj::Scene& scene, const ChargeRig& rig, bool held)
    {
        NS::Game::Level::ImpactInputJudge& judge = rig.input->Judge();
        judge.Step(held);
        const NS::Game::Level::SlamKind fired = judge.TakeFired();
        if (fired != NS::Game::Level::SlamKind::None)
        {
            float charge01 = 0.0f;
            if (fired == NS::Game::Level::SlamKind::Charged)
            {
                charge01 = judge.Charge01();
            }
            rig.player->RequestBodySlam(charge01, k_SlamDirection);
        }
        scene.OnUpdate();
    }

    // 床へ着くまで回す。落下が混ざると突進のフレームが揺れる
    void Settle(NS::Obj::Scene& scene, const ChargeRig& rig)
    {
        for (int i = 0; i < 30 && !rig.player->IsGrounded(); ++i)
        {
            StepFrame(scene, rig, false);
        }
    }

    [[nodiscard]] std::vector<const EffectLayerRecord*> RecordsNamed(const ChargeRig& rig, std::string_view name)
    {
        std::vector<const EffectLayerRecord*> found;
        for (const EffectLayerRecord& record : rig.effects->Layers().Records())
        {
            if (record.name == name)
            {
                found.push_back(&record);
            }
        }
        return found;
    }

    // 名前の層が 1 つだけ出ている。出ていなければ nullptr で、試しを落とす
    [[nodiscard]] const EffectLayerRecord* OnlyRecord(const ChargeRig& rig, std::string_view name)
    {
        const std::vector<const EffectLayerRecord*> found = RecordsNamed(rig, name);
        EXPECT_EQ(found.size(), 1u) << name;
        if (found.size() != 1u)
        {
            return nullptr;
        }
        return found.front();
    }

    // 押したフレームの記録の番号を返す。押すまでのフレームは離して回す
    [[nodiscard]] int Press(NS::Obj::Scene& scene, const ChargeRig& rig)
    {
        StepFrame(scene, rig, true);
        return rig.effects->Layers().Step();
    }
} // namespace

// 押したフレームに丸まりの殻・回転の弧・玉を包む溜まる光が出る。溜めに入る前は削る粉が出ない
TEST(ChargeEffects, PressShowsTheCurlTheSpinAndTheGatherOnThePressFrame)
{
    NS::Obj::Scene scene;
    const ChargeRig rig = BuildRig(scene, false);
    ASSERT_NE(rig.effects, nullptr);
    Settle(scene, rig);

    const int press = Press(scene, rig);

    const EffectLayerRecord* curl = OnlyRecord(rig, ChargeEffects::k_Curl);
    const EffectLayerRecord* spin = OnlyRecord(rig, ChargeEffects::k_Spin);
    const EffectLayerRecord* gather = OnlyRecord(rig, ChargeEffects::k_Gather);
    ASSERT_NE(curl, nullptr);
    ASSERT_NE(spin, nullptr);
    ASSERT_NE(gather, nullptr);
    EXPECT_EQ(curl->startStep, press);
    EXPECT_EQ(spin->startStep, press);
    EXPECT_EQ(gather->startStep, press);
    EXPECT_TRUE(RecordsNamed(rig, ChargeEffects::k_Grind).empty());
}

// 丸まりの殻は押してから決めたフレーム数で消える。押し続けても残らない
TEST(ChargeEffects, CurlEndsAFixedNumberOfFramesAfterThePress)
{
    NS::Obj::Scene scene;
    const ChargeRig rig = BuildRig(scene, false);
    Settle(scene, rig);
    const int press = Press(scene, rig);
    for (int i = 0; i < 10; ++i)
    {
        StepFrame(scene, rig, true);
    }

    const EffectLayerRecord* curl = OnlyRecord(rig, ChargeEffects::k_Curl);
    ASSERT_NE(curl, nullptr);
    ASSERT_TRUE(curl->endStep.has_value());
    EXPECT_EQ(curl->endStep.value(), press + ChargeEffects::k_CurlSteps);
}

// 溜めに入ったフレームに削る粉が出る。押したフレームに出た溜まる光は出し直さずに続く
TEST(ChargeEffects, ChargingShowsTheGrindOnTheChargingFrame)
{
    NS::Obj::Scene scene;
    const ChargeRig rig = BuildRig(scene, false);
    Settle(scene, rig);
    const int press = Press(scene, rig);
    for (int i = 0; i < k_ChargingAfterPress + 3; ++i)
    {
        StepFrame(scene, rig, true);
    }

    const EffectLayerRecord* grind = OnlyRecord(rig, ChargeEffects::k_Grind);
    const EffectLayerRecord* gather = OnlyRecord(rig, ChargeEffects::k_Gather);
    ASSERT_NE(grind, nullptr);
    ASSERT_NE(gather, nullptr);
    EXPECT_EQ(grind->startStep, press + k_ChargingAfterPress);
    EXPECT_EQ(gather->startStep, press);
    EXPECT_FALSE(grind->endStep.has_value());
    EXPECT_FALSE(gather->endStep.has_value());
}

// 溜めきったフレームに閃きが 1 回だけ出る。押し続けても 2 回目は無く、決めたフレーム数で消える
TEST(ChargeEffects, FullChargeFlashesOnceWhileHeld)
{
    NS::Obj::Scene scene;
    const ChargeRig rig = BuildRig(scene, false);
    Settle(scene, rig);
    const int press = Press(scene, rig);
    for (int i = 0; i < k_FullAfterPress + 40; ++i)
    {
        StepFrame(scene, rig, true);
    }

    const EffectLayerRecord* full = OnlyRecord(rig, ChargeEffects::k_Full);
    ASSERT_NE(full, nullptr);
    EXPECT_EQ(full->startStep, press + k_FullAfterPress);
    ASSERT_TRUE(full->endStep.has_value());
    EXPECT_EQ(full->endStep.value(), press + k_FullAfterPress + ChargeEffects::k_FullFlashSteps);
}

// 溜まる光へ渡す溜めきってからの数は溜めきったフレームで 1、押している間 1 フレームに 1 ずつ増え、
// 溜めきる前と放した後は 0
TEST(ChargeEffects, FramesSinceFullChargeCountsFromOneWhileHeld)
{
    NS::Obj::Scene scene;
    const ChargeRig rig = BuildRig(scene, false);
    Settle(scene, rig);
    static_cast<void>(Press(scene, rig));
    for (int i = 0; i < k_FullAfterPress - 1; ++i)
    {
        StepFrame(scene, rig, true);
    }
    EXPECT_EQ(rig.effects->FramesSinceFullCharge(), 0);

    StepFrame(scene, rig, true);
    EXPECT_EQ(rig.effects->FramesSinceFullCharge(), 1);
    for (int i = 0; i < 4; ++i)
    {
        StepFrame(scene, rig, true);
    }
    EXPECT_EQ(rig.effects->FramesSinceFullCharge(), 5);

    StepFrame(scene, rig, false);
    EXPECT_EQ(rig.effects->FramesSinceFullCharge(), 0);
}

// 放したフレームに溜めている間の層と、まだ出ている溜めきりの閃きを消し、同じフレームに弾けと突進の尾を出す。
// 弾けは決めたフレーム数で消える
TEST(ChargeEffects, ReleaseClearsTheHeldLayersAndStartsTheBurstAndTheTrail)
{
    NS::Obj::Scene scene;
    const ChargeRig rig = BuildRig(scene, false);
    Settle(scene, rig);
    static_cast<void>(Press(scene, rig));
    for (int i = 0; i < k_FullAfterPress; ++i)
    {
        StepFrame(scene, rig, true);
    }
    StepFrame(scene, rig, false);
    const int release = rig.effects->Layers().Step();
    ASSERT_TRUE(rig.player->IsBodySlamming());
    for (int i = 0; i < ChargeEffects::k_BurstSteps + 2; ++i)
    {
        StepFrame(scene, rig, false);
    }

    // 溜めきりの次のフレームに放す。溜めきりの閃きも、消すと決めたフレームを待たずに放したフレームで消える
    for (std::string_view name :
         {ChargeEffects::k_Spin, ChargeEffects::k_Grind, ChargeEffects::k_Gather, ChargeEffects::k_Full})
    {
        const EffectLayerRecord* held = OnlyRecord(rig, name);
        ASSERT_NE(held, nullptr);
        ASSERT_TRUE(held->endStep.has_value()) << name;
        EXPECT_EQ(held->endStep.value(), release) << name;
    }
    const EffectLayerRecord* burst = OnlyRecord(rig, ChargeEffects::k_Burst);
    const EffectLayerRecord* trail = OnlyRecord(rig, ChargeEffects::k_Trail);
    ASSERT_NE(burst, nullptr);
    ASSERT_NE(trail, nullptr);
    EXPECT_EQ(burst->startStep, release);
    EXPECT_EQ(trail->startStep, release);
    ASSERT_TRUE(burst->endStep.has_value());
    EXPECT_EQ(burst->endStep.value(), release + ChargeEffects::k_BurstSteps);
}

// タップは溜めに入らないので、削る粉と溜めきりの閃きが出ない。押したフレームに出た丸まりの殻・回転の弧・溜まる光は
// 放したフレームに消え、弾けと尾が出る
TEST(ChargeEffects, TapShowsNoChargingLayers)
{
    NS::Obj::Scene scene;
    const ChargeRig rig = BuildRig(scene, false);
    Settle(scene, rig);
    static_cast<void>(Press(scene, rig));
    StepFrame(scene, rig, false);
    const int release = rig.effects->Layers().Step();
    ASSERT_TRUE(rig.player->IsBodySlamming());
    for (int i = 0; i < 10; ++i)
    {
        StepFrame(scene, rig, false);
    }

    EXPECT_TRUE(RecordsNamed(rig, ChargeEffects::k_Grind).empty());
    EXPECT_TRUE(RecordsNamed(rig, ChargeEffects::k_Full).empty());
    for (std::string_view name : {ChargeEffects::k_Curl, ChargeEffects::k_Spin, ChargeEffects::k_Gather})
    {
        const EffectLayerRecord* held = OnlyRecord(rig, name);
        ASSERT_NE(held, nullptr);
        ASSERT_TRUE(held->endStep.has_value()) << name;
        EXPECT_EQ(held->endStep.value(), release) << name;
    }
    const EffectLayerRecord* burst = OnlyRecord(rig, ChargeEffects::k_Burst);
    ASSERT_NE(burst, nullptr);
    EXPECT_EQ(burst->startStep, release);
    EXPECT_NE(OnlyRecord(rig, ChargeEffects::k_Trail), nullptr);
}

// 弾けの大きさは溜め量で単調に増え、既定の欄でタップ 0.75 倍、溜めきり 1 倍
TEST(ChargeEffects, ReleaseBurstScaleGrowsWithTheCharge)
{
    const ChargeEffects effects;
    EXPECT_FLOAT_EQ(effects.ReleaseBurstScale(0.0f), 0.75f);
    EXPECT_FLOAT_EQ(effects.ReleaseBurstScale(1.0f), 1.0f);
    float previous = effects.ReleaseBurstScale(0.0f);
    for (int i = 1; i <= 10; ++i)
    {
        const float scale = effects.ReleaseBurstScale(static_cast<float>(i) / 10.0f);
        EXPECT_GT(scale, previous) << i;
        previous = scale;
    }
    EXPECT_FLOAT_EQ(effects.ReleaseBurstScale(2.0f), 1.0f);
    EXPECT_FLOAT_EQ(effects.ReleaseBurstScale(std::nanf("")), 0.75f);
}

// 弾けと尾を向ける回転は +Z を渡した水平の向きへ回す
TEST(ChargeEffects, YawTowardTurnsForwardOntoTheHorizontalDirection)
{
    const std::array<Vector3, 4> directions{
        Vector3{1.0f, 0.0f, 0.0f}, Vector3{-1.0f, 0.0f, 0.0f}, Vector3{0.0f, 0.0f, -1.0f}, Vector3{0.6f, 0.5f, 0.8f}};
    for (const Vector3& direction : directions)
    {
        const Vector3 turned = Vector3::Transform(Vector3{0.0f, 0.0f, 1.0f}, ChargeEffects::YawToward(direction));
        Vector3 flat{direction.x, 0.0f, direction.z};
        flat.Normalize();
        EXPECT_NEAR(turned.x, flat.x, 1e-5f);
        EXPECT_NEAR(turned.y, 0.0f, 1e-5f);
        EXPECT_NEAR(turned.z, flat.z, 1e-5f);
    }
    const Vector3 unturned = Vector3::Transform(Vector3{0.0f, 0.0f, 1.0f}, ChargeEffects::YawToward(Vector3{}));
    EXPECT_NEAR(unturned.z, 1.0f, 1e-6f);
}

// 当たったら止めの頭で突進の尾の親を止め、決めたフレーム数の後に消える。止めの間に尾の点は増えない
TEST(ChargeEffects, TrailRootStopsOnTheFreezeHead)
{
    NS::Obj::Scene scene;
    const ChargeRig rig = BuildRig(scene, true);
    ASSERT_NE(rig.resolver, nullptr);
    Settle(scene, rig);
    static_cast<void>(Press(scene, rig));
    for (int i = 0; i < k_FullAfterPress + 1; ++i)
    {
        StepFrame(scene, rig, true);
    }
    std::optional<int> freezeHead;
    for (int i = 0; i < 60; ++i)
    {
        StepFrame(scene, rig, false);
        if (!freezeHead.has_value() && rig.resolver->FreezeBeganThisStep())
        {
            freezeHead = rig.effects->Layers().Step();
        }
    }

    ASSERT_TRUE(freezeHead.has_value());
    const EffectLayerRecord* trail = OnlyRecord(rig, ChargeEffects::k_Trail);
    ASSERT_NE(trail, nullptr);
    ASSERT_TRUE(trail->rootStopStep.has_value());
    EXPECT_EQ(trail->rootStopStep.value(), freezeHead.value());
    ASSERT_TRUE(trail->endStep.has_value());
    EXPECT_EQ(trail->endStep.value(), freezeHead.value() + ChargeEffects::k_TrailFadeSteps);
}

// 当たらずに突進が終わったら、終わったフレームに尾の親を止める
TEST(ChargeEffects, TrailRootStopsWhenTheSlamEndsWithoutAHit)
{
    NS::Obj::Scene scene;
    const ChargeRig rig = BuildRig(scene, false);
    Settle(scene, rig);
    static_cast<void>(Press(scene, rig));
    StepFrame(scene, rig, false);
    ASSERT_TRUE(rig.player->IsBodySlamming());
    std::optional<int> slamEnded;
    for (int i = 0; i < 120 && !slamEnded.has_value(); ++i)
    {
        StepFrame(scene, rig, false);
        if (!rig.player->IsBodySlamming())
        {
            slamEnded = rig.effects->Layers().Step();
        }
    }

    ASSERT_TRUE(slamEnded.has_value());
    const EffectLayerRecord* trail = OnlyRecord(rig, ChargeEffects::k_Trail);
    ASSERT_NE(trail, nullptr);
    ASSERT_TRUE(trail->rootStopStep.has_value());
    EXPECT_EQ(trail->rootStopStep.value(), slamEnded.value());
}

namespace
{
    constexpr int k_TargetSize = 96;

    // 白に近い画素と数える、色の成分の最小
    constexpr std::uint8_t k_NearWhite = 230;

    // 描画先の全画素を 1 回読み戻し、画素ごとに visit(赤, 緑, 青) を呼ぶ
    template <typename Visit> void VisitPixels(const NS::Gfx::RenderTarget& target, Visit visit)
    {
        ID3D11Texture2D* source = target.Color()->Native();
        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        NS::Gfx::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(NS::Gfx::Gpu().device->CreateTexture2D(&desc, nullptr, &staging)))
        {
            ADD_FAILURE() << "読み戻し用のテクスチャを作れなかった";
            return;
        }
        ID3D11DeviceContext* context = NS::Gfx::Gpu().context;
        context->CopyResource(staging.Get(), source);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            ADD_FAILURE() << "読み戻し用のテクスチャを開けなかった";
            return;
        }
        const std::uint8_t* bytes = static_cast<const std::uint8_t*>(mapped.pData);
        for (UINT row = 0; row < desc.Height; ++row)
        {
            const std::uint8_t* line = bytes + static_cast<std::size_t>(row) * mapped.RowPitch;
            for (UINT column = 0; column < desc.Width; ++column)
            {
                visit(line[column * 4 + 0], line[column * 4 + 1], line[column * 4 + 2]);
            }
        }
        context->Unmap(staging.Get(), 0);
    }

    // 描画先の全画素の色の成分の和
    [[nodiscard]] double SumOfChannels(const NS::Gfx::RenderTarget& target)
    {
        double sum = 0.0;
        VisitPixels(target, [&sum](std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
            sum += static_cast<double>(red) + static_cast<double>(green) + static_cast<double>(blue);
        });
        return sum;
    }

    // 色の成分が 3 つとも k_NearWhite 以上の画素の数
    [[nodiscard]] int NearWhitePixels(const NS::Gfx::RenderTarget& target)
    {
        int count = 0;
        VisitPixels(target, [&count](std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
            if (red >= k_NearWhite && green >= k_NearWhite && blue >= k_NearWhite)
            {
                ++count;
            }
        });
        return count;
    }
} // namespace

// 置いた絵を描く台。EffectScene は構築時に Gpu() の device と context を使うので、Renderer より後に作る
class ChargeEffectsPictureTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        NS::Core::Logger::Init();
        NS::Platform::WindowDesc windowDesc{};
        windowDesc.title = "ns_charge_effects";
        windowDesc.size = NS::Core::Size2D{k_TargetSize, k_TargetSize};
        windowDesc.visible = false;
        m_window = std::make_unique<NS::Platform::Window>(windowDesc);
        ASSERT_TRUE(m_window->IsValid());
        NS::Gfx::RendererDesc rendererDesc{};
#ifdef NS_BUILD_DEBUG
        rendererDesc.enableDebugLayer = true;
#else
        rendererDesc.enableDebugLayer = false;
#endif
        rendererDesc.vsync = false;
        m_renderer = std::make_unique<NS::Gfx::Renderer>(rendererDesc, *m_window);
        ASSERT_TRUE(m_renderer->IsValid());
        m_target = NS::Gfx::RenderTarget::Create(NS::Core::Size2D{k_TargetSize, k_TargetSize});
        ASSERT_TRUE(m_target && m_target->IsValid());
        const std::string assets = NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::ContentRoot(), "Assets");
        m_effects = std::make_unique<NS::Gfx::EffectScene>(NS::Platform::FileSystem::Combine(assets, "Effects"));
        ASSERT_TRUE(m_effects->IsValid());
    }

    void TearDown() override
    {
        m_effects.reset();
        if (m_renderer)
        {
            m_renderer->SetSceneTarget(nullptr);
        }
        m_target.reset();
        m_renderer.reset();
        m_window.reset();
        NS::Core::Logger::Shutdown();
    }

    // 溜め量 charge01 を動的入力 0 番に入れて 1 本出し、frames フレーム進めた絵の色の成分の和を返す
    // fullFrames があれば溜めきってからの数として動的入力 1 番に入れる
    [[nodiscard]] double DrawnAmount(std::string_view name,
                                     float charge01,
                                     int frames,
                                     std::optional<float> fullFrames = std::nullopt)
    {
        Draw(name, charge01, frames, fullFrames);
        return SumOfChannels(*m_target);
    }

    // DrawnAmount と同じ絵を描き、白に近い画素の数を返す
    [[nodiscard]] int DrawnNearWhitePixels(std::string_view name, float charge01, int frames, float fullFrames)
    {
        Draw(name, charge01, frames, fullFrames);
        return NearWhitePixels(*m_target);
    }

    void Draw(std::string_view name, float charge01, int frames, std::optional<float> fullFrames)
    {
        m_effects->StopAll();
        m_effects->Update(0.0f);
        NS::Gfx::EffectPlayDesc desc{};
        desc.dynamicInputs[0] = charge01;
        desc.dynamicInputs[1] = fullFrames;
        const NS::Gfx::EffectHandle handle = m_effects->Play(name, desc);
        EXPECT_TRUE(handle.IsValid()) << name;
        for (int i = 0; i < frames; ++i)
        {
            m_effects->Update(k_FixedDt);
        }
        NS::Core::CameraData camera;
        camera.SetPosition(Vector3{0.0f, 0.0f, -6.0f});
        camera.SetAspectRatio(1.0f);
        m_renderer->BeginSceneView(m_target.get());
        m_effects->Draw(camera);
    }

    std::unique_ptr<NS::Platform::Window> m_window;
    std::unique_ptr<NS::Gfx::Renderer> m_renderer;
    std::unique_ptr<NS::Gfx::RenderTarget> m_target;
    std::unique_ptr<NS::Gfx::EffectScene> m_effects;
};

// 溜めている間の層 (回転の弧・溜まる光・削る粉) の絵は、溜め量 0 → 0.5 → 溜めきりの直前で描いた量が増える
// 回転の弧と削る粉は溜めきりでも増える。溜まる光は溜めきりで玉を包む光を消し、替わる光は溜めきってからの数が
// 無いと出ないので、溜めきりで減る
TEST_F(ChargeEffectsPictureTest, HeldLayersDrawMoreAsTheChargeGrows)
{
    constexpr float k_JustBeforeFull = 0.97f;
    for (std::string_view name : {ChargeEffects::k_Spin, ChargeEffects::k_Gather, ChargeEffects::k_Grind})
    {
        ASSERT_TRUE(m_effects->Preload(name)) << name;
        const double empty = DrawnAmount(name, 0.0f, 20);
        const double half = DrawnAmount(name, 0.5f, 20);
        const double nearlyFull = DrawnAmount(name, k_JustBeforeFull, 20);
        const double full = DrawnAmount(name, 1.0f, 20);
        EXPECT_LT(empty, half) << name;
        EXPECT_LT(half, nearlyFull) << name;
        if (name == ChargeEffects::k_Gather)
        {
            EXPECT_LT(full, nearlyFull) << name;
        }
        else
        {
            // 削る粉は 0.97 と 1 で出る数がほとんど変わらず、粒の置き方の揺れの方が大きい
            // 溜めきりで落ちない事を 1% の幅で見る
            EXPECT_LE(nearlyFull, full * 1.01) << name;
        }
    }
}

// 溜まる光は溜めきってからの数 (動的入力 1 番) が 1〜2 の間 (F〜F + 1) に広がりきった光を出し、3〜4 (F + 2〜F + 3) で
// 白に近い画素の多い白いはじけに替え、5 (F + 4) で薄め、6 (F + 5) から後は放すまで落ち着いた光で玉を包む
// 落ち着いた光は広がりきった光と溜めきりの直前の玉を包む光より少なく、押し続けている長さで変わらない
TEST_F(ChargeEffectsPictureTest, GatherShowsTheFullChargeLightsUntilRelease)
{
    constexpr float k_JustBeforeFull = 0.97f;
    ASSERT_TRUE(m_effects->Preload(ChargeEffects::k_Gather));
    const double nearlyFull = DrawnAmount(ChargeEffects::k_Gather, k_JustBeforeFull, 20, 0.0f);
    const double before = DrawnAmount(ChargeEffects::k_Gather, 1.0f, 20, 0.0f);
    const double first = DrawnAmount(ChargeEffects::k_Gather, 1.0f, 20, 1.0f);
    const double second = DrawnAmount(ChargeEffects::k_Gather, 1.0f, 20, 2.0f);
    const double third = DrawnAmount(ChargeEffects::k_Gather, 1.0f, 20, 3.0f);
    const double fourth = DrawnAmount(ChargeEffects::k_Gather, 1.0f, 20, 4.0f);
    const double fading = DrawnAmount(ChargeEffects::k_Gather, 1.0f, 20, 5.0f);
    const double settled = DrawnAmount(ChargeEffects::k_Gather, 1.0f, 20, 6.0f);
    const double settledNext = DrawnAmount(ChargeEffects::k_Gather, 1.0f, 20, 7.0f);
    const double settledLong = DrawnAmount(ChargeEffects::k_Gather, 1.0f, 20, 20.0f);
    const int secondWhite = DrawnNearWhitePixels(ChargeEffects::k_Gather, 1.0f, 20, 2.0f);
    const int thirdWhite = DrawnNearWhitePixels(ChargeEffects::k_Gather, 1.0f, 20, 3.0f);
    // 吸い込まれる点は出す位置が毎回変わるので、同じ姿の比べは 1% の幅で見る
    EXPECT_LT(before, first);
    EXPECT_NEAR(first, second, first * 0.01);
    EXPECT_LT(secondWhite, thirdWhite);
    EXPECT_NEAR(third, fourth, third * 0.01);
    EXPECT_LT(fading, first);
    EXPECT_LT(before, fading);
    // 落ち着いた光が無いと settled は before と揺れの幅の中で並ぶので、1% を超えて多い事を見る
    EXPECT_LT(before * 1.01, settled);
    EXPECT_LT(settled, first);
    EXPECT_LT(settled, nearlyFull);
    EXPECT_NEAR(settledNext, settled, settled * 0.01);
    EXPECT_NEAR(settledLong, settled, settled * 0.01);
}
