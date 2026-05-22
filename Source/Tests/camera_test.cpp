#include <gtest/gtest.h>

#include <Framework/Core/Math.h>
#include <Framework/Graphics/Camera.h>

#include <DirectXMath.h>

#include <cmath>

namespace
{
    using NS::Core::Deg2Rad;
    using NS::Core::Matrix;
    using NS::Core::Vector3;
    using NS::Graphics::Camera;

    bool MatricesNear(const Matrix& a, const Matrix& b, float eps = 1e-4f)
    {
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                if (std::abs(a.m[row][col] - b.m[row][col]) > eps)
                {
                    return false;
                }
            }
        }
        return true;
    }

    Matrix ExpectedViewLH(const Vector3& pos, const Vector3& tgt, const Vector3& up)
    {
        const DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&pos);
        const DirectX::XMVECTOR target = DirectX::XMLoadFloat3(&tgt);
        const DirectX::XMVECTOR upv = DirectX::XMLoadFloat3(&up);
        Matrix m;
        DirectX::XMStoreFloat4x4(&m, DirectX::XMMatrixLookAtLH(eye, target, upv));
        return m;
    }

    Matrix ExpectedProjectionLH(float fovY, float aspect, float nearPlane, float farPlane)
    {
        Matrix m;
        DirectX::XMStoreFloat4x4(&m, DirectX::XMMatrixPerspectiveFovLH(fovY, aspect, nearPlane, farPlane));
        return m;
    }
} // namespace

TEST(CameraTest, DefaultConstructorHasFiniteMatrices)
{
    Camera camera;
    const auto& view = camera.View();
    const auto& proj = camera.Projection();

    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            EXPECT_TRUE(std::isfinite(view.m[row][col]));
            EXPECT_TRUE(std::isfinite(proj.m[row][col]));
        }
    }
}

TEST(CameraTest, SetPositionUpdatesView)
{
    Camera camera;
    const Matrix viewBefore = camera.View();

    camera.SetPosition(Vector3(10.0f, 5.0f, -10.0f));
    const Matrix viewAfter = camera.View();

    EXPECT_FALSE(MatricesNear(viewBefore, viewAfter));
}

TEST(CameraTest, RepeatedViewCallIsStable)
{
    Camera camera;
    camera.SetPosition(Vector3(2.0f, 3.0f, -8.0f));

    const Matrix v1 = camera.View();
    const Matrix v2 = camera.View();

    EXPECT_TRUE(MatricesNear(v1, v2));
}

TEST(CameraTest, ViewUsesLeftHanded)
{
    Camera camera;
    const Vector3 pos(1.5f, 2.0f, -7.0f);
    const Vector3 tgt(0.0f, 0.5f, 0.0f);
    const Vector3 up(0.0f, 1.0f, 0.0f);

    camera.SetPosition(pos);
    camera.SetTarget(tgt);
    camera.SetUp(up);

    const Matrix expected = ExpectedViewLH(pos, tgt, up);
    EXPECT_TRUE(MatricesNear(camera.View(), expected));
}

TEST(CameraTest, ProjectionUsesLeftHanded)
{
    Camera camera;
    camera.SetFovY(NS::Core::ToRadians(NS::Core::Degrees{45.0f}));
    camera.SetAspectRatio(1.6f);
    camera.SetNearPlane(0.5f);
    camera.SetFarPlane(500.0f);

    const Matrix expected = ExpectedProjectionLH(Deg2Rad(45.0f), 1.6f, 0.5f, 500.0f);
    EXPECT_TRUE(MatricesNear(camera.Projection(), expected));
}

TEST(CameraTest, SetAspectRatioRebuildsProjection)
{
    Camera camera;
    camera.SetAspectRatio(16.0f / 9.0f);
    const Matrix projBefore = camera.Projection();

    camera.SetAspectRatio(4.0f / 3.0f);
    const Matrix projAfter = camera.Projection();

    EXPECT_FALSE(MatricesNear(projBefore, projAfter));
}

TEST(CameraTest, ViewProjectionIsViewTimesProjection)
{
    Camera camera;
    camera.SetPosition(Vector3(0.0f, 1.0f, -3.0f));
    camera.SetTarget(Vector3(0.0f, 0.0f, 0.0f));
    camera.SetAspectRatio(1.0f);

    const Matrix vp = camera.ViewProjection();
    const Matrix manual = camera.View() * camera.Projection();
    EXPECT_TRUE(MatricesNear(vp, manual));
}

TEST(CameraTest, DegenerateEyeEqualsTargetReturnsIdentity)
{
    Camera camera;
    camera.SetPosition(Vector3(2.0f, 2.0f, 2.0f));
    camera.SetTarget(Vector3(2.0f, 2.0f, 2.0f));

    const Matrix view = camera.View();
    for (int row = 0; row < 4; ++row)
    {
        for (int col = 0; col < 4; ++col)
        {
            EXPECT_TRUE(std::isfinite(view.m[row][col]));
        }
    }
    EXPECT_TRUE(MatricesNear(view, Matrix::Identity));
}

TEST(CameraTest, AccessorsReturnSetValues)
{
    Camera camera;
    camera.SetPosition(Vector3(1.0f, 2.0f, 3.0f));
    camera.SetTarget(Vector3(4.0f, 5.0f, 6.0f));
    camera.SetUp(Vector3(0.0f, 0.0f, 1.0f));
    camera.SetFovY(NS::Core::Radians{0.5f});
    camera.SetAspectRatio(2.0f);
    camera.SetNearPlane(0.25f);
    camera.SetFarPlane(750.0f);

    EXPECT_FLOAT_EQ(camera.Position().x, 1.0f);
    EXPECT_FLOAT_EQ(camera.Target().y, 5.0f);
    EXPECT_FLOAT_EQ(camera.Up().z, 1.0f);
    EXPECT_FLOAT_EQ(camera.FovY().value, 0.5f);
    EXPECT_FLOAT_EQ(camera.AspectRatio(), 2.0f);
    EXPECT_FLOAT_EQ(camera.NearPlane(), 0.25f);
    EXPECT_FLOAT_EQ(camera.FarPlane(), 750.0f);
}
