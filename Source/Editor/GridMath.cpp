#include "Editor/GridMath.h"

#include "NSlib/Core/Math.h"

#include "NSlib/Core/Types.h"
#include <algorithm>

#include <imgui.h>

namespace NS
{
    namespace Ed
    {
        bool ViewRectContains(const ViewRect& rect, int screenX, int screenY) noexcept
        {
            return screenX >= rect.x && screenX < rect.x + rect.width && screenY >= rect.y &&
                   screenY < rect.y + rect.height;
        }

        Size2D ViewRectSize(const ViewRect& rect) noexcept
        {
            return Size2D{rect.width, rect.height};
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
            if (ImGui::GetCurrentContext() == nullptr)
            {
                return;
            }
            const ImGuiViewport* main = ImGui::GetMainViewport();
            if (main == nullptr)
            {
                return;
            }
            // 別窓を出さない設定なら Pos は原点なので、この足し算は何も動かさない
            outX += static_cast<int>(main->Pos.x);
            outY += static_cast<int>(main->Pos.y);
        }

        Ray ScreenToWorldRay(const Matrix& viewProjection, Size2D viewport, int mouseX, int mouseY) noexcept
        {
            const float ndcX = (2.0f * static_cast<float>(mouseX)) / static_cast<float>(viewport.width) - 1.0f;
            const float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / static_cast<float>(viewport.height);

            Matrix inv = viewProjection.Invert();

            const Vector4 nearH{ndcX, ndcY, 0.0f, 1.0f};
            const Vector4 farH{ndcX, ndcY, 1.0f, 1.0f};

            const Vector4 wNearH = Vector4::Transform(nearH, inv);
            const Vector4 wFarH = Vector4::Transform(farH, inv);

            const auto inverseW = [](const Vector4& h) -> float {
                if (std::abs(h.w) > 1e-6f)
                {
                    return 1.0f / h.w;
                }
                return 0.0f;
            };
            const float invWNear = inverseW(wNearH);
            const float invWFar = inverseW(wFarH);

            Vector3 wNear{wNearH.x * invWNear, wNearH.y * invWNear, wNearH.z * invWNear};
            Vector3 wFar{wFarH.x * invWFar, wFarH.y * invWFar, wFarH.z * invWFar};
            Vector3 dir = wFar - wNear;
            dir.Normalize();
            return Ray{wNear, dir};
        }

        float ScreenConstantScale(const Vector3& point, const Matrix& viewProjection) noexcept
        {
            // 見かけの寸法は世界の長さ / clip.w に比例するので、長さを clip.w に比例させると相殺される
            const Vector4 clip =
                Vector4::Transform(Vector4{point.x, point.y, point.z, 1.0f}, viewProjection);
            // カメラ至近や背面では伸ばさない
            if (clip.w <= 1.0e-3f)
            {
                return 1.0f;
            }
            return std::max(clip.w / 10.0f, 1.0f);
        }
    } // namespace Ed
} // namespace NS
