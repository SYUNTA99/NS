#include "Editor/AssetPreview.h"

#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Gfx/D3DCommandList.h"
#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/D3DRenderTarget.h"
#include "NSlib/Gfx/D3DTexture.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include "NSlib/Core/Windows/Filesystem.h"

#include <cmath>

namespace NS::Editor
{
        namespace
        {
            std::optional<Matrix> FitPreviewWorld(const AABB& localBounds, const Matrix& world,
                                                  const AssetPreviewPose& pose)
            {
                AABB bounds{};
                localBounds.Transform(bounds, world);
                const Vector3 center{bounds.Center.x, bounds.Center.y, bounds.Center.z};
                const Vector3 extents{bounds.Extents.x, bounds.Extents.y, bounds.Extents.z};
                const float radius = extents.Length();
                if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
                    !std::isfinite(radius) || radius <= 0.0f || !std::isfinite(1.0f / radius))
                {
                    return std::nullopt;
                }
                return world * Matrix::CreateTranslation(-center) * Matrix::CreateScale(pose.zoom / radius) *
                       Matrix::CreateRotationY(pose.yaw) * Matrix::CreateRotationX(pose.pitch);
            }
        }

        std::optional<Gfx::DrawItem> MakeAssetPreviewItem(const Object& asset, const AssetPreviewPose& pose)
        {
            Gfx::DrawItem item{};
            float distance = 1.8f;
            if (const Material* material = Cast<Material>(&asset))
            {
                // 球には骨の頂点入力と行列がないため、骨付き専用シェーダーでは描かない
                if (material->IsEmpty() || OS::FileSystem::FileName(material->VertexShaderPath()) == "skinned.vs.hlsl")
                {
                    return std::nullopt;
                }
                item.mesh = AssetManager::Get().LoadMesh("sphere")->D3D();
                item.material = material->D3D();
                item.constants.world = Matrix::Identity;
                item.constants.baseColor = material->BaseColor().ToVector3();
                if (item.material != nullptr)
                {
                    item.blend = item.material->Blend();
                }
            }
            else if (const SceneActor* actor = Cast<SceneActor>(&asset))
            {
                const Model* model = actor->ModelClassObj();
                if (model == nullptr || !model->CanDraw() || model->DrawnLocalBounds() == nullptr)
                {
                    return std::nullopt;
                }
                const AABB& localBounds = *model->DrawnLocalBounds();
                const Matrix actorWorld = model->DrawWorldMatrix(actor->Root().WorldMatrix(), 1.0f);
                const std::optional<Matrix> world = FitPreviewWorld(localBounds, actorWorld, pose);
                if (!world)
                {
                    return std::nullopt;
                }
                const Gfx::RenderContext context{};
                item = model->MakeDrawItem(context, *world);
                item.twoSided = true;
                item.constants.ambientColor = Vector3{0.8f, 0.8f, 0.8f};
                item.constants.groundColor = Vector3{0.6f, 0.6f, 0.6f};
                distance = 3.2f;
            }
            else if (const Mesh* mesh = Cast<Mesh>(&asset))
            {
                const AABB* bounds = mesh->LocalBounds();
                if (bounds == nullptr)
                {
                    return std::nullopt;
                }
                const std::optional<Matrix> world = FitPreviewWorld(*bounds, Matrix::Identity, pose);
                if (!world)
                {
                    return std::nullopt;
                }
                item.mesh = mesh->D3D();
                item.material = AssetManager::Get().LoadMaterial(AssetManager::DefaultMaterialPath())->D3D();
                item.twoSided = true;
                item.constants.world = *world;
                item.constants.baseColor = Vector3{0.85f, 0.85f, 0.85f};
                item.constants.ambientColor = Vector3{0.8f, 0.8f, 0.8f};
                item.constants.groundColor = Vector3{0.6f, 0.6f, 0.6f};
                distance = 3.2f;
            }
            else
            {
                return std::nullopt;
            }
            item.constants.viewProj =
                Matrix::CreateLookAt(Vector3{0.0f, 0.0f, -distance}, Vector3::Zero, Vector3::Up) *
                Matrix::CreatePerspectiveFieldOfView(0.7853982f, 1.0f, 0.1f, 10.0f);
            item.constants.lightDir.Normalize();
            return item;
        }

        bool RenderAssetPreview(Gfx::D3DDevice& device, Gfx::D3DRenderTarget& target,
                                const Gfx::DrawItem& item) noexcept
        {
            if (!device.IsValid() || !target.IsValid() || item.material == nullptr || !item.material->IsValid() ||
                item.material->IsUsingFallback() || item.mesh == nullptr || !item.mesh->IsValid())
            {
                return false;
            }
            Gfx::D3DCommandList& commands = device.Commands();
            commands.SetRenderTarget(target.Color()->Rtv(), target.Depth()->Dsv());
            const Size2D size = target.Size();
            commands.SetViewport(static_cast<float>(size.width), static_cast<float>(size.height));
            commands.ClearRenderTarget(target.Color()->Rtv(), 0.12f, 0.12f, 0.14f, 1.0f);
            commands.ClearDepth(target.Depth()->Dsv());
            Gfx::IssueDrawItem(device, item);
            device.BindBackbuffer();
            return true;
        }
} // namespace NS::Editor
