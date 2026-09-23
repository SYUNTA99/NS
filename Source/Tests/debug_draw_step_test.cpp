#include <gtest/gtest.h>

#include "Runtime/Graphics/DebugDraw.h"

namespace
{
    using NS::Core::Color;
    using NS::Core::Vector3;
    namespace Debug = NS::Gfx::DebugDraw;

    void PushOneLine()
    {
        Debug::Line(Vector3{0.0f, 0.0f, 0.0f}, Vector3{1.0f, 0.0f, 0.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f});
    }
} // namespace

// 捨てないと、描画 1 回の間に固定ステップが 2 回進んだ時に前の回の線まで映る
TEST(NsDebugDrawStep, KeepsOnlyTheLinesOfTheLatestStep)
{
    Debug::Clear();
    Debug::BeginStep();
    PushOneLine();
    const std::size_t afterFirst = Debug::VertexCount();

    Debug::BeginStep();
    PushOneLine();

    EXPECT_EQ(Debug::VertexCount(), afterFirst);
}

// 何も積まない固定ステップに前の回の線が残ると、描画 1 回の間に何回進んだかで絵が変わる
TEST(NsDebugDrawStep, DropsTheLinesWhenAStepDrawsNothing)
{
    Debug::Clear();
    Debug::BeginStep();
    PushOneLine();

    Debug::BeginStep();

    EXPECT_EQ(Debug::VertexCount(), 0u);
}

TEST(NsDebugDrawStep, StacksWhatOneStepDrawsSeveralTimes)
{
    Debug::Clear();
    Debug::BeginStep();
    PushOneLine();
    PushOneLine();

    EXPECT_EQ(Debug::VertexCount(), 4u);
}
