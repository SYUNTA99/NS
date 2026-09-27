// efkprobe: .efkefc をゲームと同じ Effekseer 1.80.7 の実行側で読み、次を行う
//   1. Effect::Create が通るか、依存するテクスチャ・モデル・マテリアル・カーブが全部読めたかを見る
//   2. 節の木を実行側が読んだ値で出す。種類・最大生成数・寿命・生成間隔・生成の遅れ・合成・色テクスチャ
//   3. 画面外の DX11 の描画先へ 1 フレームずつ描き、フレームごとのインスタンス数と描いた画素の数を出し、PNG に書く
// Manager と Renderer の組み方は Source/Runtime/Graphics/EffectScene.cpp と同じ
//
// 使い方: efkprobe <.efkefc> <PNG の出力先フォルダ> [--frames N] [--size 画素] [--distance 距離] [--height 目の高さ]
//                  [--bg r,g,b] [--start-frame フレーム数] [--no-png]
//   フレーム k の PNG は、Play の後に Update(1) を k 回呼んで描いた絵。ゲームの 60Hz の k フレーム目に当たる
//   --start-frame は Manager::Play の開始フレームに渡す。実行側は最初の更新でその数だけ先に進めてから描く
//   EffectScene::Play は開始フレームを渡さないので、ゲームと同じ形は 0

#pragma warning(push, 0)
#include <Effekseer.h>
#include <EffekseerRendererDX11.h>

#include "Effekseer/Effekseer.EffectNode.h"
#pragma warning(pop)

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    // EffectScene と同じ値
    constexpr std::int32_t k_MaxSprites = 8000;
    constexpr std::int32_t k_MaxInstances = 8000;

    struct Options
    {
        std::filesystem::path effectPath;
        std::filesystem::path outputDirectory;
        int frames = 40;
        int size = 512;
        float distance = 6.0f;
        float eyeHeight = 0.0f;
        std::array<float, 3> background{0.08f, 0.08f, 0.10f};
        std::int32_t startFrame = 0;
        bool writePng = true;
    };

    // PNG は無圧縮の deflate で書く。見るための絵なので大きさは問わない
    std::uint32_t Crc32(const std::uint8_t* data, std::size_t length, std::uint32_t crc)
    {
        static std::array<std::uint32_t, 256> table = [] {
            std::array<std::uint32_t, 256> result{};
            for (std::uint32_t n = 0; n < 256; ++n)
            {
                std::uint32_t c = n;
                for (int k = 0; k < 8; ++k)
                {
                    if ((c & 1u) != 0)
                    {
                        c = 0xEDB88320u ^ (c >> 1);
                    }
                    else
                    {
                        c = c >> 1;
                    }
                }
                result[n] = c;
            }
            return result;
        }();
        crc = ~crc;
        for (std::size_t i = 0; i < length; ++i)
        {
            crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
        }
        return ~crc;
    }

    void PutBigEndian32(std::vector<std::uint8_t>& out, std::uint32_t value)
    {
        out.push_back(static_cast<std::uint8_t>(value >> 24));
        out.push_back(static_cast<std::uint8_t>(value >> 16));
        out.push_back(static_cast<std::uint8_t>(value >> 8));
        out.push_back(static_cast<std::uint8_t>(value));
    }

    void PutChunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data)
    {
        PutBigEndian32(out, static_cast<std::uint32_t>(data.size()));
        std::vector<std::uint8_t> typed(type, type + 4);
        typed.insert(typed.end(), data.begin(), data.end());
        out.insert(out.end(), typed.begin(), typed.end());
        PutBigEndian32(out, Crc32(typed.data(), typed.size(), 0));
    }

    bool WritePngRgb(const std::filesystem::path& path, int width, int height, const std::vector<std::uint8_t>& rgb)
    {
        std::vector<std::uint8_t> raw;
        raw.reserve(static_cast<std::size_t>((width * 3 + 1) * height));
        for (int y = 0; y < height; ++y)
        {
            raw.push_back(0);
            const std::uint8_t* row = rgb.data() + static_cast<std::size_t>(y) * width * 3;
            raw.insert(raw.end(), row, row + width * 3);
        }

        std::vector<std::uint8_t> zlib{0x78, 0x01};
        std::size_t offset = 0;
        while (offset < raw.size() || raw.empty())
        {
            const std::size_t blockLength = std::min<std::size_t>(65535, raw.size() - offset);
            const bool last = offset + blockLength >= raw.size();
            if (last)
            {
                zlib.push_back(1);
            }
            else
            {
                zlib.push_back(0);
            }
            zlib.push_back(static_cast<std::uint8_t>(blockLength & 0xFF));
            zlib.push_back(static_cast<std::uint8_t>(blockLength >> 8));
            zlib.push_back(static_cast<std::uint8_t>(~blockLength & 0xFF));
            zlib.push_back(static_cast<std::uint8_t>((~blockLength >> 8) & 0xFF));
            zlib.insert(zlib.end(), raw.begin() + offset, raw.begin() + offset + blockLength);
            offset += blockLength;
            if (last)
            {
                break;
            }
        }
        std::uint32_t a = 1;
        std::uint32_t b = 0;
        for (std::uint8_t value : raw)
        {
            a = (a + value) % 65521u;
            b = (b + a) % 65521u;
        }
        PutBigEndian32(zlib, (b << 16) | a);

        std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        std::vector<std::uint8_t> header;
        PutBigEndian32(header, static_cast<std::uint32_t>(width));
        PutBigEndian32(header, static_cast<std::uint32_t>(height));
        header.insert(header.end(), {8, 2, 0, 0, 0});
        PutChunk(png, "IHDR", header);
        PutChunk(png, "IDAT", zlib);
        PutChunk(png, "IEND", {});

        FILE* file = nullptr;
        const errno_t opened = _wfopen_s(&file, path.wstring().c_str(), L"wb");
        if (opened != 0 || file == nullptr)
        {
            // 前の走行の PNG を上書きする所で一度だけ開けずに落ち、走らせ直すと通った。理由を引けるよう戻り値を出す
            std::printf("PNG を書く先を開けなかった: %s (_wfopen_s の戻り値 %d)\n",
                        reinterpret_cast<const char*>(path.u8string().c_str()),
                        static_cast<int>(opened));
            return false;
        }
        const std::size_t written = std::fwrite(png.data(), 1, png.size(), file);
        std::fclose(file);
        return written == png.size();
    }

    const char* YesNo(bool value)
    {
        if (value)
        {
            return "はい";
        }
        return "いいえ";
    }

    const char* NodeTypeName(Effekseer::EffectNodeType type)
    {
        switch (type)
        {
        case Effekseer::EffectNodeType::Root:
            return "Root";
        case Effekseer::EffectNodeType::NoneType:
            return "None";
        case Effekseer::EffectNodeType::Sprite:
            return "Sprite";
        case Effekseer::EffectNodeType::Ribbon:
            return "Ribbon";
        case Effekseer::EffectNodeType::Ring:
            return "Ring";
        case Effekseer::EffectNodeType::Model:
            return "Model";
        case Effekseer::EffectNodeType::Track:
            return "Track";
        }
        return "?";
    }

    const char* AlphaBlendName(Effekseer::AlphaBlendType blend)
    {
        switch (blend)
        {
        case Effekseer::AlphaBlendType::Opacity:
            return "Opacity";
        case Effekseer::AlphaBlendType::Blend:
            return "Blend";
        case Effekseer::AlphaBlendType::Add:
            return "Add";
        case Effekseer::AlphaBlendType::Sub:
            return "Sub";
        case Effekseer::AlphaBlendType::Mul:
            return "Mul";
        }
        return "?";
    }

    std::string ToUtf8(const char16_t* text)
    {
        std::string result;
        if (text == nullptr)
        {
            return result;
        }
        for (const char16_t* p = text; *p != 0; ++p)
        {
            const char16_t c = *p;
            if (c < 0x80)
            {
                result.push_back(static_cast<char>(c));
            }
            else if (c < 0x800)
            {
                result.push_back(static_cast<char>(0xC0 | (c >> 6)));
                result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
            else
            {
                result.push_back(static_cast<char>(0xE0 | (c >> 12)));
                result.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                result.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
        return result;
    }

    int CountNodes(const Effekseer::EffectNode* node)
    {
        int count = 1;
        for (int i = 0; i < node->GetChildrenCount(); ++i)
        {
            count += CountNodes(node->GetChild(i));
        }
        return count;
    }

    void PrintNode(const Effekseer::EffectRef& effect, Effekseer::EffectNode* node, int depth)
    {
        const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
        if (node->GetType() == Effekseer::EffectNodeType::Root)
        {
            std::printf("%s節 Root (子 %d)\n", indent.c_str(), node->GetChildrenCount());
        }
        else
        {
            // 実行側の中の値を見るため、公開の EffectNode から実装の型へ下ろす
            // 実行側の節の型は全部 EffectNodeImplemented の派生
            const Effekseer::EffectNodeImplemented* implemented =
                static_cast<const Effekseer::EffectNodeImplemented*>(node);
            const Effekseer::ParameterCommonValues& common = implemented->CommonValues;
            const Effekseer::ParameterRendererCommon& renderer = implemented->RendererCommon;
            std::string texture = "(無し)";
            const int32_t textureIndex = renderer.TextureIndexes[0];
            if (textureIndex >= 0 && textureIndex < effect->GetColorImageCount())
            {
                texture = ToUtf8(effect->GetColorImagePath(textureIndex));
            }
            std::printf(
                "%s節 %s: 最大生成数 %d / 寿命 %d〜%d フレーム / 生成間隔 %g〜%g / 生成の遅れ %g〜%g / 合成 %s / "
                "色テクスチャ %s / 描く %s\n",
                indent.c_str(),
                NodeTypeName(node->GetType()),
                common.MaxGeneration,
                common.life.min,
                common.life.max,
                common.Generation.Interval.min,
                common.Generation.Interval.max,
                common.Generation.Offset.min,
                common.Generation.Offset.max,
                AlphaBlendName(renderer.AlphaBlend),
                texture.c_str(),
                YesNo(implemented->IsRendered));
        }
        for (int i = 0; i < node->GetChildrenCount(); ++i)
        {
            PrintNode(effect, node->GetChild(i), depth + 1);
        }
    }

    // EffectScene::CountMissingResources と同じ数え方。Effect::Create は依存を読み損ねても成功を返す
    int CountMissingResources(const Effekseer::EffectRef& effect)
    {
        int missing = 0;
        for (int32_t i = 0; i < effect->GetColorImageCount(); ++i)
        {
            if (effect->GetColorImage(i) == nullptr)
            {
                std::printf("読めなかった: テクスチャ %s\n", ToUtf8(effect->GetColorImagePath(i)).c_str());
                ++missing;
            }
        }
        for (int32_t i = 0; i < effect->GetNormalImageCount(); ++i)
        {
            if (effect->GetNormalImage(i) == nullptr)
            {
                std::printf("読めなかった: 法線テクスチャ %s\n", ToUtf8(effect->GetNormalImagePath(i)).c_str());
                ++missing;
            }
        }
        for (int32_t i = 0; i < effect->GetDistortionImageCount(); ++i)
        {
            if (effect->GetDistortionImage(i) == nullptr)
            {
                std::printf("読めなかった: 歪みテクスチャ %s\n", ToUtf8(effect->GetDistortionImagePath(i)).c_str());
                ++missing;
            }
        }
        for (int32_t i = 0; i < effect->GetModelCount(); ++i)
        {
            if (effect->GetModel(i) == nullptr)
            {
                std::printf("読めなかった: モデル %s\n", ToUtf8(effect->GetModelPath(i)).c_str());
                ++missing;
            }
        }
        for (int32_t i = 0; i < effect->GetMaterialCount(); ++i)
        {
            if (effect->GetMaterial(i) == nullptr)
            {
                std::printf("読めなかった: マテリアル %s\n", ToUtf8(effect->GetMaterialPath(i)).c_str());
                ++missing;
            }
        }
        for (int32_t i = 0; i < effect->GetCurveCount(); ++i)
        {
            if (effect->GetCurve(i) == nullptr)
            {
                std::printf("読めなかった: カーブ %s\n", ToUtf8(effect->GetCurvePath(i)).c_str());
                ++missing;
            }
        }
        return missing;
    }

    bool ParseOptions(int argc, wchar_t** argv, Options& options)
    {
        if (argc < 3)
        {
            return false;
        }
        options.effectPath = argv[1];
        options.outputDirectory = argv[2];
        for (int i = 3; i < argc; ++i)
        {
            const std::wstring flag = argv[i];
            const bool hasValue = i + 1 < argc;
            if (flag == L"--frames" && hasValue)
            {
                options.frames = _wtoi(argv[++i]);
            }
            else if (flag == L"--size" && hasValue)
            {
                options.size = _wtoi(argv[++i]);
            }
            else if (flag == L"--distance" && hasValue)
            {
                options.distance = static_cast<float>(_wtof(argv[++i]));
            }
            else if (flag == L"--height" && hasValue)
            {
                options.eyeHeight = static_cast<float>(_wtof(argv[++i]));
            }
            else if (flag == L"--bg" && hasValue)
            {
                if (swscanf_s(argv[++i],
                              L"%f,%f,%f",
                              &options.background[0],
                              &options.background[1],
                              &options.background[2]) != 3)
                {
                    return false;
                }
            }
            else if (flag == L"--start-frame" && hasValue)
            {
                options.startFrame = _wtoi(argv[++i]);
            }
            else if (flag == L"--no-png")
            {
                options.writePng = false;
            }
            else
            {
                return false;
            }
        }
        return options.frames > 0 && options.size > 0 && options.startFrame >= 0;
    }
} // namespace

int wmain(int argc, wchar_t** argv)
{
    Options options;
    if (!ParseOptions(argc, argv, options))
    {
        std::printf("使い方: efkprobe <.efkefc> <PNG の出力先> [--frames N] [--size 画素] [--distance 距離] [--height "
                    "目の高さ] "
                    "[--bg r,g,b] [--start-frame フレーム数] [--no-png]\n");
        return 2;
    }

    // 画面は出さない。GPU が無ければ WARP
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &device, nullptr, &context);
    const char* driver = "ハードウェア";
    if (FAILED(hr))
    {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &device, nullptr, &context);
        driver = "WARP";
    }
    if (FAILED(hr))
    {
        std::printf("失敗: D3D11 の device を作れなかった (0x%08lX)\n", static_cast<unsigned long>(hr));
        return 1;
    }

    D3D11_TEXTURE2D_DESC targetDesc{};
    targetDesc.Width = static_cast<UINT>(options.size);
    targetDesc.Height = static_cast<UINT>(options.size);
    targetDesc.MipLevels = 1;
    targetDesc.ArraySize = 1;
    targetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    targetDesc.SampleDesc.Count = 1;
    targetDesc.Usage = D3D11_USAGE_DEFAULT;
    targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> targetView;
    D3D11_TEXTURE2D_DESC stagingDesc = targetDesc;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device->CreateTexture2D(&targetDesc, nullptr, &target)) ||
        FAILED(device->CreateRenderTargetView(target.Get(), nullptr, &targetView)) ||
        FAILED(device->CreateTexture2D(&stagingDesc, nullptr, &staging)))
    {
        std::printf("失敗: 描画先を作れなかった\n");
        return 1;
    }

    // EffectScene のコンストラクタと同じ組み方
    const Effekseer::Backend::GraphicsDeviceRef graphicsDevice =
        EffekseerRendererDX11::CreateGraphicsDevice(device.Get(), context.Get());
    const EffekseerRenderer::RendererRef renderer =
        EffekseerRendererDX11::Renderer::Create(graphicsDevice, k_MaxSprites);
    const Effekseer::ManagerRef manager = Effekseer::Manager::Create(k_MaxInstances);
    if (graphicsDevice == nullptr || renderer == nullptr || manager == nullptr)
    {
        std::printf("失敗: Effekseer の Renderer か Manager を作れなかった\n");
        return 1;
    }
    manager->SetCoordinateSystem(Effekseer::CoordinateSystem::LH);
    manager->SetSpriteRenderer(renderer->CreateSpriteRenderer());
    manager->SetRibbonRenderer(renderer->CreateRibbonRenderer());
    manager->SetRingRenderer(renderer->CreateRingRenderer());
    manager->SetTrackRenderer(renderer->CreateTrackRenderer());
    manager->SetModelRenderer(renderer->CreateModelRenderer());
    manager->SetTextureLoader(renderer->CreateTextureLoader());
    manager->SetModelLoader(renderer->CreateModelLoader());
    manager->SetMaterialLoader(renderer->CreateMaterialLoader());
    manager->SetCurveLoader(Effekseer::MakeRefPtr<Effekseer::CurveLoader>());

    const std::u16string pathU16 = options.effectPath.u16string();
    Effekseer::EffectRef effect = Effekseer::Effect::Create(manager, pathU16.c_str());
    if (effect == nullptr)
    {
        std::printf("失敗: Effect::Create が null を返した (%s)\n",
                    reinterpret_cast<const char*>(options.effectPath.u8string().c_str()));
        return 1;
    }
    std::printf("Effect::Create: 成功 (%s、描画 %s)\n",
                reinterpret_cast<const char*>(options.effectPath.filename().u8string().c_str()),
                driver);
    std::printf("形式の版: %d\n", effect->GetVersion());
    const int missing = CountMissingResources(effect);
    std::printf("依存: テクスチャ %d 枚・モデル %d・マテリアル %d・カーブ %d、読めなかった物 %d\n",
                effect->GetColorImageCount(),
                effect->GetModelCount(),
                effect->GetMaterialCount(),
                effect->GetCurveCount(),
                missing);
    std::printf("節の数: %d (Root を含む)\n", CountNodes(effect->GetRoot()));
    PrintNode(effect, effect->GetRoot(), 0);
    const Effekseer::EffectTerm term = effect->CalculateTerm();
    std::printf("存在しうる期間 (CalculateTerm): %d〜%d フレーム\n", term.TermMin, term.TermMax);

    // 描いた画素は、背景から色の成分の 1 つでも k_DrawnThreshold 以上離れた画素の数。幅は試しの画素の比べ方と同じ
    constexpr int k_DrawnThreshold = 16;
    if (options.writePng)
    {
        std::filesystem::create_directories(options.outputDirectory);
    }
    const float aspect = 1.0f;
    renderer->SetProjectionMatrix(
        Effekseer::Matrix44().PerspectiveFovLH(60.0f / 180.0f * 3.14159265f, aspect, 0.1f, 100.0f));
    renderer->SetCameraMatrix(
        Effekseer::Matrix44().LookAtLH(Effekseer::Vector3D(0.0f, options.eyeHeight, -options.distance),
                                       Effekseer::Vector3D(0.0f, 0.0f, 0.0f),
                                       Effekseer::Vector3D(0.0f, 1.0f, 0.0f)));

    const Effekseer::Handle handle = manager->Play(effect, Effekseer::Vector3D(0.0f, 0.0f, 0.0f), options.startFrame);
    if (options.startFrame > 0)
    {
        std::printf("Play の開始フレーム: %d\n", options.startFrame);
    }
    int lastAliveFrame = 0;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(options.size) * options.size * 3);
    std::array<int, 3> backgroundBytes{};
    for (std::size_t channel = 0; channel < backgroundBytes.size(); ++channel)
    {
        backgroundBytes[channel] = static_cast<int>(std::lround(options.background[channel] * 255.0f));
    }
    std::printf("フレーム\t生きている\tインスタンス数 (Root を含む)\t描いた画素\n");
    for (int frame = 1; frame <= options.frames; ++frame)
    {
        manager->Update(1.0f);
        const bool alive = manager->Exists(handle);
        int32_t instances = 0;
        if (alive)
        {
            instances = manager->GetInstanceCount(handle);
            lastAliveFrame = frame;
        }

        const float clear[4] = {options.background[0], options.background[1], options.background[2], 1.0f};
        context->ClearRenderTargetView(targetView.Get(), clear);
        ID3D11RenderTargetView* views[] = {targetView.Get()};
        context->OMSetRenderTargets(1, views, nullptr);
        D3D11_VIEWPORT viewport{
            0.0f, 0.0f, static_cast<float>(options.size), static_cast<float>(options.size), 0.0f, 1.0f};
        context->RSSetViewports(1, &viewport);

        Effekseer::Manager::LayerParameter layer;
        layer.ViewerPosition = Effekseer::Vector3D(0.0f, options.eyeHeight, -options.distance);
        manager->SetLayerParameter(0, layer);
        renderer->SetTime(static_cast<float>(frame) / 60.0f);
        renderer->BeginRendering();
        Effekseer::Manager::DrawParameter drawParameter;
        drawParameter.ZNear = 0.0f;
        drawParameter.ZFar = 1.0f;
        drawParameter.ViewProjectionMatrix = renderer->GetCameraProjectionMatrix();
        drawParameter.CameraPosition = renderer->GetCameraPosition();
        drawParameter.CameraFrontDirection = renderer->GetCameraFrontDirection();
        manager->Draw(drawParameter);
        renderer->EndRendering();

        context->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
        {
            std::printf("失敗: 描いた絵を読み戻せなかった (フレーム %d)\n", frame);
            return 1;
        }
        for (int y = 0; y < options.size; ++y)
        {
            const std::uint8_t* row =
                static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch;
            for (int x = 0; x < options.size; ++x)
            {
                std::uint8_t* out = rgb.data() + (static_cast<std::size_t>(y) * options.size + x) * 3;
                out[0] = row[x * 4 + 0];
                out[1] = row[x * 4 + 1];
                out[2] = row[x * 4 + 2];
            }
        }
        context->Unmap(staging.Get(), 0);

        int drawnPixels = 0;
        for (std::size_t pixel = 0; pixel < rgb.size(); pixel += 3)
        {
            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                if (std::abs(static_cast<int>(rgb[pixel + channel]) - backgroundBytes[channel]) >= k_DrawnThreshold)
                {
                    ++drawnPixels;
                    break;
                }
            }
        }
        std::printf("%d\t%s\t%d\t%d\n", frame, YesNo(alive), instances, drawnPixels);

        if (!options.writePng)
        {
            continue;
        }
        wchar_t name[32];
        swprintf_s(name, L"frame_%03d.png", frame);
        if (!WritePngRgb(options.outputDirectory / name, options.size, options.size, rgb))
        {
            std::printf("失敗: PNG を書けなかった (フレーム %d)\n", frame);
            return 1;
        }
    }
    std::printf("最後に生きていたフレーム: %d\n", lastAliveFrame);

    manager->StopAllEffects();
    effect.Reset();
    if (missing > 0)
    {
        return 1;
    }
    return 0;
}
