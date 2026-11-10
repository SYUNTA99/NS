#include "NSlib/Gfx/Render/EffectScene.h"
#include "NSlib/Core/Types.h"

#include "NSlib/Core/CameraData.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Gfx/D3DEffect.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DTexture.h"

#include <EffekseerRendererDX11.h>

#include <cmath>
#include <format>

namespace NS
{
    namespace Gfx
    {
        namespace
        {
            // Effekseer の Update は 1 を 1/60 秒として数える
            constexpr float k_EffekseerFramesPerSecond = 60.0f;

            // TODO: 仮の値で、Effekseer のサンプルと同じ 8000。衝突のエフェクトを作ったら同時に出る最大数を測って決める
            // 超えた分のインスタンスは作られず、スプライトは描かれない
            constexpr int32 k_MaxSprites = 8000;
            constexpr int32 k_MaxInstances = 8000;

            // 平行移動を 4 行目に置く並びが DirectXMath と Effekseer で同じなので、転置しない
            Effekseer::Matrix44 ToEffekseer(const Matrix& matrix) noexcept
            {
                Effekseer::Matrix44 result;
                for (int row = 0; row < 4; ++row)
                {
                    for (int column = 0; column < 4; ++column)
                    {
                        result.Values[row][column] = matrix.m[row][column];
                    }
                }
                return result;
            }

            constexpr int k_DynamicInputCount = 4;

            Effekseer::Matrix43 ToEffekseerTransform(const Vector3& position,
                                                     const Quaternion& rotation,
                                                     const Vector3& scale) noexcept
            {
                const Matrix world = Matrix::CreateScale(scale) * Matrix::CreateFromQuaternion(rotation) *
                                     Matrix::CreateTranslation(position);
                Effekseer::Matrix43 result;
                for (int row = 0; row < 4; ++row)
                {
                    for (int column = 0; column < 3; ++column)
                    {
                        result.Value[row][column] = world.m[row][column];
                    }
                }
                return result;
            }

            uint8 ToColorByte(float value) noexcept
            {
                if (!IsPositive(value))
                {
                    return 0;
                }
                if (value >= 1.0f)
                {
                    return 255;
                }
                return static_cast<uint8>(std::lround(value * 255.0f));
            }
        } // namespace

        EffectScene::EffectScene() noexcept
        {
            const D3DDevice* gpu = D3DDevice::Current();
            if (gpu == nullptr || gpu->Native() == nullptr || gpu->NativeContext() == nullptr)
            {
                NS_LOG_WARN(Graphics, "EffectScene: D3DDevice が未構築のため無効のまま作る");
                return;
            }

            const Effekseer::Backend::GraphicsDeviceRef graphicsDevice =
                EffekseerRendererDX11::CreateGraphicsDevice(gpu->Native(), gpu->NativeContext());
            if (graphicsDevice == nullptr)
            {
                NS_LOG_ERROR(Graphics, "EffectScene: Effekseer の GraphicsDevice を作れなかった");
                return;
            }

            const EffekseerRenderer::RendererRef renderer =
                EffekseerRendererDX11::Renderer::Create(graphicsDevice, k_MaxSprites);
            if (renderer == nullptr)
            {
                NS_LOG_ERROR(
                    Graphics, "EffectScene: Effekseer の Renderer を作れなかった (maxSprites={})", k_MaxSprites);
                return;
            }

            const Effekseer::ManagerRef manager = Effekseer::Manager::Create(k_MaxInstances);
            if (manager == nullptr)
            {
                NS_LOG_ERROR(
                    Graphics, "EffectScene: Effekseer の Manager を作れなかった (maxInstances={})", k_MaxInstances);
                return;
            }

            manager->SetCoordinateSystem(Effekseer::CoordinateSystem::LH);

            manager->SetSpriteRenderer(renderer->CreateSpriteRenderer());
            manager->SetRibbonRenderer(renderer->CreateRibbonRenderer());
            manager->SetRingRenderer(renderer->CreateRingRenderer());
            manager->SetTrackRenderer(renderer->CreateTrackRenderer());
            manager->SetModelRenderer(renderer->CreateModelRenderer());

            m_renderer = renderer;
            m_manager = manager;
        }

        EffectScene::~EffectScene()
        {
            // Sprite・Ribbon・Ring・Track の描画は Renderer を生ポインタで持つ
            // ModelRenderer の参照で Renderer が残るのに頼らず、Manager を先に放す
            if (m_renderer != nullptr)
            {
                m_renderer->SetDepth(nullptr, EffekseerRenderer::DepthReconstructionParameter{});
            }
            m_depthTextures = {};
            m_manager.Reset();
            m_renderer.Reset();
        }

        bool EffectScene::IsValid() const noexcept
        {
            return m_manager != nullptr && m_renderer != nullptr;
        }

        EffectHandle EffectScene::Play(const D3DEffect* effect, const EffectPlayDesc& desc) noexcept
        {
            if (!IsValid() || effect == nullptr || !effect->IsValid())
            {
                return EffectHandle{};
            }

            for (const std::optional<float>& input : desc.dynamicInputs)
            {
                if (input.has_value() && !std::isfinite(input.value()))
                {
                    NS_LOG_WARN(Graphics, "エフェクトの動的入力が有限でないため再生しない");
                    return EffectHandle{};
                }
            }

            const Effekseer::Handle handle =
                m_manager->Play(effect->Native(), desc.position.x, desc.position.y, desc.position.z);
            // Play が std::rand から引いた種を上書きする。節が生まれる最初の Update より前なので、この種で生まれる
            uint32& playCount = m_playCounts[effect];
            m_manager->SetRandomSeed(handle,
                                     static_cast<int32>((effect->Seed() ^ (playCount * 2654435761u)) & 0x7FFFFFFFu));
            ++playCount;
            // Play は節を作らず、最初の Update で作る。ここで渡した姿勢・色・入力は生まれる時に読まれる
            m_manager->SetMatrix(handle, ToEffekseerTransform(desc.position, desc.rotation, desc.scale));
            m_manager->SetAllColor(handle,
                                   Effekseer::Color(ToColorByte(desc.color.R()),
                                                    ToColorByte(desc.color.G()),
                                                    ToColorByte(desc.color.B()),
                                                    ToColorByte(desc.color.A())));
            for (std::size_t i = 0; i < desc.dynamicInputs.size(); ++i)
            {
                if (desc.dynamicInputs[i].has_value())
                {
                    m_manager->SetDynamicInput(handle, static_cast<int32>(i), desc.dynamicInputs[i].value());
                }
            }
            return EffectHandle{handle};
        }

        void EffectScene::SetTransform(EffectHandle handle,
                                       const Vector3& position,
                                       const Quaternion& rotation,
                                       const Vector3& scale) noexcept
        {
            if (!IsValid() || !handle.IsValid())
            {
                return;
            }
            m_manager->SetMatrix(handle.value, ToEffekseerTransform(position, rotation, scale));
        }

        void EffectScene::SetDynamicInput(EffectHandle handle, int index, float value) noexcept
        {
            if (!IsValid() || !handle.IsValid())
            {
                return;
            }
            // Effekseer は範囲の外を黙って捨てる。書いたつもりの値が効かない理由をログに残す
            if (index < 0 || index >= k_DynamicInputCount)
            {
                NS_LOG_WARN(Graphics, "EffectScene::SetDynamicInput: 動的入力の番号 {} は 0〜3 の外", index);
                return;
            }
            if (!std::isfinite(value))
            {
                NS_LOG_WARN(Graphics, "エフェクトの動的入力 {} の値が有限でないため変更しない", index);
                return;
            }
            m_manager->SetDynamicInput(handle.value, index, value);
        }

        void EffectScene::StopRoot(EffectHandle handle) noexcept
        {
            if (!IsValid() || !handle.IsValid())
            {
                return;
            }
            m_manager->StopRoot(handle.value);
        }

        void EffectScene::Stop(EffectHandle handle) noexcept
        {
            if (!IsValid() || !handle.IsValid())
            {
                return;
            }
            m_manager->StopEffect(handle.value);
        }

        void EffectScene::StopAll() noexcept
        {
            if (!IsValid())
            {
                return;
            }
            m_manager->StopAllEffects();
            m_playCounts.clear();
        }

        bool EffectScene::Exists(EffectHandle handle) const noexcept
        {
            if (!IsValid() || !handle.IsValid())
            {
                return false;
            }
            return m_manager->Exists(handle.value);
        }

        void EffectScene::Update(float deltaSeconds) noexcept
        {
            if (!IsValid() || deltaSeconds < 0.0f)
            {
                return;
            }
            // float の Update は UpdateInterval を 0 にするので、1/60 秒刻みに丸めずに進む
            m_manager->Update(deltaSeconds * k_EffekseerFramesPerSecond);
        }

        void EffectScene::Draw(const CameraData& camera, float timeSeconds) noexcept
        {
            if (!IsValid())
            {
                return;
            }

            const Vector3& eye = camera.Position();
            Effekseer::Manager::LayerParameter layer;
            layer.ViewerPosition = Effekseer::Vector3D(eye.x, eye.y, eye.z);
            m_manager->SetLayerParameter(0, layer);

            m_renderer->SetTime(timeSeconds);
            m_renderer->SetProjectionMatrix(ToEffekseer(camera.Projection()));
            m_renderer->SetCameraMatrix(ToEffekseer(camera.View()));
            PassDepth(camera);

            m_renderer->BeginRendering();
            Effekseer::Manager::DrawParameter drawParameter;
            // 射影後の手前と奥の深度。既定の 0 と 0 では視錐台カリングが働かない
            drawParameter.ZNear = 0.0f;
            drawParameter.ZFar = 1.0f;
            drawParameter.ViewProjectionMatrix = m_renderer->GetCameraProjectionMatrix();
            // SetCameraMatrix の後に取る。深度クリップはこの位置と向きからの距離で決まり、既定の 0 では働かない
            drawParameter.CameraPosition = m_renderer->GetCameraPosition();
            drawParameter.CameraFrontDirection = m_renderer->GetCameraFrontDirection();
            m_manager->Draw(drawParameter);
            m_renderer->EndRendering();
        }

        bool EffectScene::PassesDepth() const noexcept
        {
            if (!IsValid())
            {
                return false;
            }
            Effekseer::Backend::TextureRef texture;
            EffekseerRenderer::DepthReconstructionParameter parameter;
            m_renderer->GetDepth(texture, parameter);
            return texture != nullptr;
        }

        void EffectScene::PassDepth(const CameraData& camera) noexcept
        {
            const SceneDepthCopy::CaptureResult capture = m_depth.Capture();
            // 奥行きを結ばない描画先は奥行き無しで描く。前の描画先の奥行きを残すと、別の絵の物で薄くなる
            if (capture.texture == nullptr && capture.failure.empty())
            {
                m_renderer->SetDepth(nullptr, EffekseerRenderer::DepthReconstructionParameter{});
                m_depthFallback = false;
                return;
            }
            if (capture.texture == nullptr)
            {
                FallBackWithoutDepth(capture.failure, capture.width, capture.height);
                return;
            }
            const Effekseer::Backend::TextureRef depthTexture = DepthTextureFor(capture);
            if (depthTexture == nullptr)
            {
                FallBackWithoutDepth("奥行きの写し先を作れなかった", capture.width, capture.height);
                return;
            }

            // 投影行列から奥行きを視点からの距離へ戻す値。NS は左手系の透視投影を転置せずに渡し、深度は 0 が手前で 1
            // が奥
            const Effekseer::Matrix44 projection = ToEffekseer(camera.Projection());
            EffekseerRenderer::DepthReconstructionParameter parameter;
            parameter.DepthBufferScale = 1.0f;
            parameter.DepthBufferOffset = 0.0f;
            parameter.ProjectionMatrix33 = projection.Values[2][2];
            parameter.ProjectionMatrix43 = projection.Values[2][3];
            parameter.ProjectionMatrix34 = projection.Values[3][2];
            parameter.ProjectionMatrix44 = projection.Values[3][3];
            m_renderer->SetDepth(depthTexture, parameter);
            m_depthFallback = false;
            m_failedDepthKey.clear();
        }

        Effekseer::Backend::TextureRef EffectScene::DepthTextureFor(const SceneDepthCopy::CaptureResult& capture) noexcept
        {
            for (const DepthTextureRef& entry : m_depthTextures)
            {
                if (entry.textureId == capture.textureId)
                {
                    return entry.effekseerTexture;
                }
            }
            const Effekseer::Backend::TextureRef created =
                EffekseerRendererDX11::CreateTexture(m_renderer->GetGraphicsDevice(), capture.texture->Srv(), nullptr, nullptr);
            if (created == nullptr)
            {
                return nullptr;
            }
            m_depthTextures[m_nextDepthTexture] = DepthTextureRef{capture.textureId, created};
            m_nextDepthTexture = (m_nextDepthTexture + 1u) % static_cast<uint32>(m_depthTextures.size());
            return created;
        }

        void EffectScene::FallBackWithoutDepth(std::string_view reason, uint32 width, uint32 height) noexcept
        {
            // 前の奥行きを残すと、別の描画先の物で薄くなる
            m_renderer->SetDepth(nullptr, EffekseerRenderer::DepthReconstructionParameter{});
            m_depthFallback = true;
            std::string key = std::format("{} {}x{}", reason, width, height);
            if (key == m_failedDepthKey)
            {
                return;
            }
            NS_LOG_ERROR(Graphics, "EffectScene: {} ({}x{})。奥行き無しでエフェクトを描く", reason, width, height);
            m_failedDepthKey = std::move(key);
        }
    } // namespace Gfx
} // namespace NS
