#include "Editor/GridMath.h"

#include "Runtime/Math/Math.h"

#include <cstdint>

#if NS_EDITOR_ENABLED
#include <imgui.h>
#endif

namespace NS::Editor
{

    bool ViewRectContains(const ViewRect& rect, int screenX, int screenY) noexcept
    {
        return screenX >= rect.x && screenX < rect.x + rect.width && screenY >= rect.y &&
               screenY < rect.y + rect.height;
    }

    NS::Math::Size2D ViewRectSize(const ViewRect& rect) noexcept
    {
        return NS::Math::Size2D{rect.width, rect.height};
    }

    void ViewRectToLocal(const ViewRect& rect, int screenX, int screenY, int& outX, int& outY) noexcept
    {
        outX = screenX - rect.x;
        outY = screenY - rect.y;
    }

    void WindowMouseToViewSpace(int mouseX, int mouseY, int& outX, int& outY) noexcept
    {
        outX = mouseX;
        outY = mouseY;
#if NS_EDITOR_ENABLED
        if (ImGui::GetCurrentContext() == nullptr)
            return;
        const ImGuiViewport* main = ImGui::GetMainViewport();
        if (main == nullptr)
            return;
        // 別窓を出さない設定なら Pos は原点なので、この足し算は何も動かさない
        outX += static_cast<int>(main->Pos.x);
        outY += static_cast<int>(main->Pos.y);
#endif
    }

    NS::Math::Ray ScreenToWorldRay(const NS::Math::Matrix& viewProjection,
                                   NS::Math::Size2D viewport,
                                   int mouseX,
                                   int mouseY) noexcept
    {
        const float ndcX = (2.0f * static_cast<float>(mouseX)) / static_cast<float>(viewport.width) - 1.0f;
        const float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / static_cast<float>(viewport.height);

        NS::Math::Matrix inv = viewProjection.Invert();

        const NS::Math::Vector4 nearH{ndcX, ndcY, 0.0f, 1.0f};
        const NS::Math::Vector4 farH{ndcX, ndcY, 1.0f, 1.0f};

        const NS::Math::Vector4 wNearH = NS::Math::Vector4::Transform(nearH, inv);
        const NS::Math::Vector4 wFarH = NS::Math::Vector4::Transform(farH, inv);

        const float invWNear = [&]() -> float {
            if (std::abs(wNearH.w) > 1e-6f)
                return 1.0f / wNearH.w;
            return 0.0f;
        }();
        const float invWFar = [&]() -> float {
            if (std::abs(wFarH.w) > 1e-6f)
                return 1.0f / wFarH.w;
            return 0.0f;
        }();

        NS::Math::Vector3 wNear{wNearH.x * invWNear, wNearH.y * invWNear, wNearH.z * invWNear};
        NS::Math::Vector3 wFar{wFarH.x * invWFar, wFarH.y * invWFar, wFarH.z * invWFar};
        NS::Math::Vector3 dir = wFar - wNear;
        dir.Normalize();
        return NS::Math::Ray{wNear, dir};
    }

    NS::Math::Vector3 SnapWorldPointToGrid(NS::Math::Vector3 p, float g) noexcept
    {
        // 最も近い整数に丸める
        const float gx = std::floor(p.x / g + 0.5f) * g;
        const float gy = std::floor(p.y / g + 0.5f) * g;
        const float gz = std::floor(p.z / g + 0.5f) * g;
        return {gx, gy, gz};
    }

    NS::Math::Vector3 SnapHitToPlacementCell(NS::Math::Vector3 hit, NS::Math::Vector3 normal, float g) noexcept
    {
        const NS::Math::Vector3 base = SnapWorldPointToGrid(hit, g);
        return {base.x + normal.x * g, base.y + normal.y * g, base.z + normal.z * g};
    }

    bool TryGroundPlaneFallback(const NS::Math::Ray& ray, NS::Math::Vector3& outCenter, float g) noexcept
    {
        // 水平または上向きのレイは交差対象外とする
        if (ray.direction.y > -1e-4f)
            return false;
        const float t = -ray.position.y / ray.direction.y;
        if (t < 0.0f)
            return false;
        const NS::Math::Vector3 hit{
            ray.position.x + ray.direction.x * t,
            0.0f,
            ray.position.z + ray.direction.z * t,
        };
        outCenter = SnapWorldPointToGrid(hit, g);
        outCenter.y = 0.0f;
        return true;
    }

    NS::Math::Quaternion RotationToQuaternion(std::uint8_t rotation) noexcept
    {
        const std::uint8_t r = static_cast<std::uint8_t>(rotation & 0x03);
        // 90 度刻み = π/2 ラジアン
        const float angle = static_cast<float>(r) * (NS::Math::k_Pi * 0.5f);
        return NS::Math::Quaternion::CreateFromAxisAngle({0.0f, 1.0f, 0.0f}, angle);
    }

} // namespace NS::Editor
