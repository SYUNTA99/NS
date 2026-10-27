#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Types.h"

#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/Gfx/D3DMaterial.h"
#include "NSlib/Gfx/D3DMesh.h"
#include "NSlib/Gfx/Render/RenderContext.h"
#include <algorithm>
#include <cmath>

namespace
{
    // 描く時だけの倍率の既定。この値の間は描く行列にも境界にも何も掛けない
    const NS::Vector3 k_NoDrawScale{1.0f, 1.0f, 1.0f};

    // 0 以下と非数・無限大の成分を弾き、全部が有限の正の時だけ真を返す
    [[nodiscard]] bool IsPositiveFiniteScale(const NS::Vector3& scale) noexcept
    {
        return NS::IsPositiveFinite(scale.x) && NS::IsPositiveFinite(scale.y) && NS::IsPositiveFinite(scale.z);
    }

    // 形の原点で局所の倍率を掛けてから matrix を掛ける。倍率が 1 の間は、掛け算が下の桁を丸めないように掛けない
    [[nodiscard]] NS::Matrix WithLocalScale(const NS::Vector3& localScale, const NS::Matrix& matrix) noexcept
    {
        if (localScale == k_NoDrawScale)
        {
            return matrix;
        }
        return NS::Matrix::CreateScale(localScale) * matrix;
    }
} // namespace

namespace NS
{
    Model::Model()
    {
        LoadAssetField(m_mesh, "");
        LoadAssetField(m_material, "");
        ChooseDrawnMaterial();
    }

    void Model::SetMeshRef(std::string_view ref)
    {
        LoadAssetField(m_mesh, ref);
        ChooseDrawnMaterial();
    }

    void Model::SetMaterialRef(std::string_view ref)
    {
        LoadAssetField(m_material, ref);
        ChooseDrawnMaterial();
    }

    void Model::ChooseDrawnMaterial()
    {
        if (!m_material->Path().empty())
        {
            m_drawnMaterial = m_material;
            return;
        }
        Mesh* mesh = m_shownMesh;
        if (mesh == nullptr)
        {
            mesh = m_mesh;
        }
        m_drawnMaterial = mesh->UseUnsetMaterial();
    }

    const Mesh* Model::GetMesh() const noexcept
    {
        if (m_shownMesh != nullptr)
        {
            return m_shownMesh;
        }
        return m_mesh;
    }

    void Model::SetPerObjectVsConstant(const Gfx::D3DBuffer* cb,
                                       const void* cpuData,
                                       std::size_t cpuDataSize,
                                       uint slot) noexcept
    {
        m_perObjectVsCb = cb;
        m_perObjectVsData = cpuData;
        m_perObjectVsSize = cpuDataSize;
        m_perObjectVsSlot = slot;
    }

    void Model::Snapshot() noexcept
    {
        m_previousLocalRotation = m_localRotation;
        m_previousDrawScale = GetDrawScale();
    }

    bool Model::SetDrawScale(const Vector3& scale) noexcept
    {
        if (!IsPositiveFiniteScale(scale))
        {
            return false;
        }
        m_drawScale = scale;
        return true;
    }

    bool Model::SnapDrawScale(const Vector3& scale) noexcept
    {
        if (!SetDrawScale(scale))
        {
            return false;
        }
        m_previousDrawScale = GetDrawScale();
        return true;
    }

    void Model::StartDrawShake(const DrawShakeDesc& desc) noexcept
    {
        m_shake = ShakeRun{.desc = desc, .frame = 0, .active = true};
    }

    void Model::StopDrawShake() noexcept
    {
        m_shake.active = false;
        m_drawOffset = Vector3{0.0f, 0.0f, 0.0f};
    }

    void Model::StartDrawShape(const DrawShapeDesc& desc) noexcept
    {
        m_shape = ShapeRun{.desc = desc, .frame = std::max(desc.firstFrame, 0) - 1, .active = true};
    }

    void Model::StopDrawShape() noexcept
    {
        m_shape.active = false;
        m_drawShapeScale = k_NoDrawScale;
    }

    void Model::StartDrawTremor(const DrawTremorDesc& desc) noexcept
    {
        m_tremor = TremorRun{.desc = desc, .elapsed = -1, .active = true};
    }

    void Model::StopDrawTremor() noexcept
    {
        m_tremor.active = false;
        m_drawTremor = Gfx::TremorCB{};
    }

    void Model::ScaleDrawShaking(float scale) noexcept
    {
        m_shake.desc.amplitude *= scale;
        m_tremor.desc.amplitudePixels *= scale;
        m_tremor.desc.amplitudeRatio *= scale;
    }

    void Model::AdvanceDraw(const IGetCamera* camera, const Vector3& rootPosition)
    {
        NS_ASSERT(Scene, camera != nullptr, "Model::AdvanceDraw: カメラの窓口が null");
        if (m_shake.active)
        {
            ++m_shake.frame;
            const Vector3 offset = m_shake.desc.OffsetAt(m_shake.frame);
            // 有限でないずれは描く形を消すので控えず、前のずれのまま描く
            if (IsFinite(offset))
            {
                m_drawOffset = offset;
            }
            // 奥揺れは長さのフレームまで揺れるので、長さの次のフレームで 0 を書いてから止める
            if (m_shake.frame > m_shake.desc.length)
            {
                m_shake.active = false;
            }
        }
        if (m_shape.active)
        {
            ++m_shape.frame;
            if (m_shape.frame >= m_shape.desc.length)
            {
                StopDrawShape();
            }
            else
            {
                const Vector3 scale = m_shape.desc.ScaleAt(m_shape.frame);
                // 有限の正でない倍率は描く形を消すので控えず、前の倍率のまま描く
                if (IsPositiveFiniteScale(scale))
                {
                    m_drawShapeScale = scale;
                }
            }
        }
        if (m_tremor.active)
        {
            ++m_tremor.elapsed;
            Gfx::TremorCB tremor{};
            // 振れ幅はカメラとの距離で決まる
            const std::optional<CameraPose> pose = GetCameraViewPose(camera);
            if (pose.has_value())
            {
                tremor = MakeTremor(m_tremor.desc, m_tremor.elapsed, rootPosition, *pose);
            }
            // 有限でない震えは描く形を消すので控えず、前の震えのまま描く
            if (IsFiniteTremor(tremor))
            {
                m_drawTremor = tremor;
            }
            if (m_tremor.elapsed >= m_tremor.desc.length)
            {
                m_tremor.active = false;
            }
        }
    }

    bool Model::SetLocalScale(const Vector3& scale) noexcept
    {
        if (!IsPositiveFiniteScale(scale))
        {
            return false;
        }
        m_localScale = scale;
        return true;
    }

    const AABB* Model::DrawnLocalBounds() const noexcept
    {
        if (m_hasLocalBoundsOverride)
        {
            return &m_localBoundsOverride;
        }
        return GetMesh()->LocalBounds();
    }

    Matrix Model::DrawWorldMatrix(const Matrix& root, float alpha) const noexcept
    {
        return DrawWorldMatrixWithOffset(root, alpha, m_drawOffset);
    }

    Matrix Model::DrawWorldMatrixWithOffset(const Matrix& root, float alpha, const Vector3& offset) const noexcept
    {
        const Quaternion local = Quaternion::Slerp(m_previousLocalRotation, m_localRotation, alpha);
        const Matrix localMatrix = WithLocalScale(m_localScale, Matrix::CreateFromQuaternion(local));
        // 回転を先に掛ける。根のスケールは根の軸に残り、局所の回転と一緒に回らない
        Matrix drawn = localMatrix * root;
        // ずれは倍率の後に世界で足す。倍率の中心 (下端の真ん中) は根から測るので、ずれと一緒に動いて形を変えない
        // ずれが 0 の間は掛けない。0 の平行移動でも掛け算が下の桁を丸めることがある
        Matrix shift = Matrix::Identity;
        const bool shifted = offset.x != 0.0f || offset.y != 0.0f || offset.z != 0.0f;
        if (shifted)
        {
            shift = Matrix::CreateTranslation(offset);
        }
        const Vector3 scale = Vector3::Lerp(m_previousDrawScale, GetDrawScale(), alpha);
        // 倍率が 1 でも、下端の真ん中へ移して戻す足し引きが下の桁を丸めることがある。倍率の無い間は掛けずに返す
        if (scale == k_NoDrawScale)
        {
            if (shifted)
            {
                drawn = drawn * shift;
            }
            return drawn;
        }

        // 中心は描く形の下端の真ん中。局所の回転は含めない。回した箱を包む箱は玉の下端より下へ出るので、
        // 中心が床より下に来て、潰すと玉が床へめり込む
        Vector3 pivot = root.Translation();
        if (const AABB* localBounds = DrawnLocalBounds())
        {
            AABB worldBounds{};
            localBounds->Transform(worldBounds, WithLocalScale(m_localScale, root));
            pivot =
                Vector3{worldBounds.Center.x, worldBounds.Center.y - worldBounds.Extents.y, worldBounds.Center.z};
        }
        // 倍率は世界の軸で掛ける。局所の回転と根の回転に依らず世界の縦に潰れる
        Matrix scaled = drawn * Matrix::CreateTranslation(-pivot) * Matrix::CreateScale(scale) *
                            Matrix::CreateTranslation(pivot);
        if (shifted)
        {
            scaled = scaled * shift;
        }
        return scaled;
    }

    void Model::Collect(const Gfx::RenderContext& context,
                        const RenderSource& source,
                        std::vector<Gfx::DrawItem>& out)
    {
        if (!CanDraw(m_drawnMaterial))
        {
            return;
        }

        out.push_back(MakeDrawItem(context, m_drawnMaterial, DrawWorldMatrix(source.root, context.alpha)));
    }

    Color Model::DrawColor() const noexcept
    {
        if (m_colorOverride)
        {
            return *m_colorOverride;
        }
        return m_drawnMaterial->BaseColor();
    }

    bool Model::CanDraw() const noexcept
    {
        return CanDraw(m_drawnMaterial);
    }

    Gfx::DrawItem Model::MakeDrawItem(const Gfx::RenderContext& context, const Matrix& world) const noexcept
    {
        return MakeDrawItem(context, m_drawnMaterial, world);
    }

    bool Model::CanDraw(const Material* material) const noexcept
    {
        return GetMesh()->D3D() != nullptr && material->D3D() != nullptr;
    }

    Gfx::DrawItem Model::MakeDrawItem(const Gfx::RenderContext& context,
                                          const Material* material,
                                          const Matrix& world) const noexcept
    {
        // context.resolvedSettings は project 既定に配置された平行光まで解決済
        const Gfx::RenderSettings& settings = context.resolvedSettings;

        Gfx::DrawItem item{};
        item.mesh = GetMesh()->D3D();
        item.material = material->D3D();
        item.blend = item.material->Blend();
        item.constants.world = world;
        item.constants.viewProj = context.viewProjection;
        item.constants.lightDir = settings.lightDir;
        item.constants.lightDir.Normalize();
        item.constants.baseColor = DrawColor().ToVector3();
        item.constants.lightColor = settings.lightColor;
        item.constants.ambientColor = settings.ambientColor;
        item.constants.groundColor = settings.groundColor;
        item.constants.exposure = settings.exposure;
        item.constants.groundWaveCenterX = context.groundWave.centerX;
        item.constants.groundWaveCenterZ = context.groundWave.centerZ;
        item.constants.groundWaveRadius = context.groundWave.radius;
        item.constants.groundWaveStrength = context.groundWave.strength;
        item.constants.tremor = m_drawTremor;
        item.extraVsCb = m_perObjectVsCb;
        item.extraVsData = m_perObjectVsData;
        item.extraVsSize = m_perObjectVsSize;
        item.extraVsSlot = m_perObjectVsSlot;
        return item;
    }

    RenderBucket Model::Bucket() const noexcept
    {
        const Gfx::D3DMaterial* material = m_drawnMaterial->D3D();
        if (material == nullptr || material->Blend() == Gfx::BlendMode::Opaque)
        {
            return RenderBucket::Opaque;
        }

        return RenderBucket::Transparent;
    }

    Vector3 Model::SortCenter(const RenderSource& source) const noexcept
    {
        return source.root.Translation();
    }

    int Model::SortPriority() const noexcept
    {
        if (const Gfx::D3DMaterial* material = m_drawnMaterial->D3D())
        {
            return material->RenderPriority();
        }
        return 0;
    }

    AABB Model::WorldBounds(const RenderSource& source) const noexcept
    {
        if (GetMesh()->D3D() == nullptr)
        {
            return {}; // 描くものが無い。間引かれても Collect が何も積まず結果は変わらない
        }
        AABB out{};
        // m_mesh は確認済みで、描画の境界を取得できる
        DrawnLocalBounds()->Transform(out, WithLocalScale(m_localScale, source.root));

        // 描く時は前と今の倍率の間を補間するので、成分ごとの大きい方で包む。下端はそのまま
        const Vector3 scale = Vector3::Max(m_previousDrawScale, GetDrawScale());
        if (scale != k_NoDrawScale)
        {
            const float bottom = out.Center.y - out.Extents.y;
            out.Extents.x *= scale.x;
            out.Extents.y *= scale.y;
            out.Extents.z *= scale.z;
            out.Center.y = bottom + out.Extents.y;
        }
        out.Center.x += m_drawOffset.x;
        out.Center.y += m_drawOffset.y;
        out.Center.z += m_drawOffset.z;
        return out;
    }
} // namespace NS
