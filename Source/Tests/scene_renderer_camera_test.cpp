#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/Core/Windows/Window.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

// 1 フレームのカメラは描く側が実カメラから 1 つに決める。物・空・エフェクトが同じカメラで描かれる

namespace
{
    constexpr int k_Width = 64;
    constexpr int k_Height = 36;

    class BoxActor : public NS::SceneActor
    {
    protected:
        void OnInit() override
        {
            NS::Model* body = CreateClassObj(m_model);
            NS::InitDraw(this, body, &Root());
            body->SetMeshRef("cube");
            body->SetMaterialRef("Assets/Materials/player.mat");
        }
    };

    // 実カメラの視点でシーンを 1 回描き、バックバッファを青・緑・赤・不透明度の順で返す。読めなければ空
    std::vector<NS::uint8> RenderAndRead(NS::Gfx::D3DDevice& device, NS::Scene& scene)
    {
        device.BeginFrame(0.0f, 0.0f, 0.0f, 1.0f);
        scene.OnRender();
        std::vector<NS::uint8> pixels;
        NS::Size2D size{};
        if (!device.ReadBackbufferPixels(pixels, size))
        {
            return {};
        }
        return pixels;
    }

    // 左上のピクセルと色の違うピクセルの数
    int CountDifferentFromCorner(const std::vector<NS::uint8>& bgra)
    {
        int count = 0;
        for (std::size_t i = 0; i + 3 < bgra.size(); i += 4)
        {
            if (bgra[i] != bgra[0] || bgra[i + 1] != bgra[1] || bgra[i + 2] != bgra[2])
            {
                ++count;
            }
        }
        return count;
    }
} // namespace

TEST(SceneRendererCamera, ActorsDrawTheSameAfterTheManagerDropsTheRealCamera)
{
    NS::OS::Window window(
        NS::OS::WindowDesc{.title = "描く側のカメラの試し", .size = {k_Width, k_Height}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    const NS::AssetManagerScope useAssets{assets};

    {
        NS::Scene scene;
        scene.SetDevice(&device);
        (void)scene.SpawnActor<BoxActor>("Box");
        // 世界を止め、管理役が実カメラを書き換えずに、補間も掛けずに描く
        scene.SetSimulationEnabled(false);
        NS::SceneCamera* camera = scene.MainCamera();
        ASSERT_NE(camera, nullptr);
        camera->SetPosition(NS::Vector3{0.0f, 1.0f, -3.0f});
        camera->SetTarget(NS::Vector3{0.0f, 0.0f, 0.0f});

        const std::vector<NS::uint8> withCamera = RenderAndRead(device, scene);
        ASSERT_EQ(withCamera.size(), static_cast<std::size_t>(k_Width * k_Height * 4));
        // 箱が画面に写っている事を先に確かめる。写らなければ、カメラを外しても変わらないのは当たり前になる
        ASSERT_GT(CountDifferentFromCorner(withCamera), 50);

        scene.GetCameraManager()->SetCamera(nullptr);
        const std::vector<NS::uint8> withoutCamera = RenderAndRead(device, scene);
        EXPECT_EQ(withoutCamera, withCamera);
    }
    device.BindBackbuffer();
}
