#include <gtest/gtest.h>
#include <memory>
#include <Runtime/App/Layer.h>
#include <Runtime/App/Layers.h>
#include <string>

namespace
{
    using NS::App::Layer;
    using NS::App::Layers;

    class TrackingLayer : public Layer
    {
    public:
        explicit TrackingLayer(std::string name, std::string* log) : Layer(std::move(name)), m_log(log) {}

        void OnAttach() override { m_log->append(std::string{Name()} + ":Attach;"); }
        void OnDetach() override { m_log->append(std::string{Name()} + ":Detach;"); }
        void OnUpdate() override { m_log->append(std::string{Name()} + ":Update;"); }
        void OnRender() override { m_log->append(std::string{Name()} + ":Render;"); }

    private:
        std::string* m_log;
    };
} // namespace

TEST(NsAppLayer, DefaultActiveAndNameAccessor)
{
    Layer l("MyLayer");
    EXPECT_EQ(l.Name(), "MyLayer");
    EXPECT_TRUE(l.IsActive());

    l.SetActive(false);
    EXPECT_FALSE(l.IsActive());

    l.SetActive(true);
    EXPECT_TRUE(l.IsActive());
}

TEST(NsAppLayers, EmptyOnConstruction)
{
    Layers layers;
    EXPECT_TRUE(layers.Empty());
    EXPECT_EQ(layers.Size(), 0u);
}

TEST(NsAppLayers, AddLayerInsertsBeforeOverlays)
{
    Layers layers;
    std::string log;

    layers.AddLayer(std::make_unique<TrackingLayer>("A", &log));
    layers.AddLayer(std::make_unique<TrackingLayer>("B", &log));

    EXPECT_EQ(layers.Size(), 2u);

    for (auto& l : layers)
        l->OnUpdate();
    EXPECT_EQ(log, "A:Update;B:Update;");
}

TEST(NsAppLayers, AddOverlayGoesAfterRegularLayers)
{
    Layers layers;
    std::string log;

    layers.AddLayer(std::make_unique<TrackingLayer>("Game", &log));
    layers.AddOverlay(std::make_unique<TrackingLayer>("HUD", &log));

    for (auto& l : layers)
        l->OnUpdate();
    EXPECT_EQ(log, "Game:Update;HUD:Update;");
}

TEST(NsAppLayers, IterationOrderRegularThenOverlay)
{
    Layers layers;
    std::string log;

    layers.AddLayer(std::make_unique<TrackingLayer>("A", &log));
    layers.AddOverlay(std::make_unique<TrackingLayer>("O1", &log));
    layers.AddLayer(std::make_unique<TrackingLayer>("B", &log));
    layers.AddOverlay(std::make_unique<TrackingLayer>("O2", &log));

    for (auto& l : layers)
        l->OnUpdate();
    EXPECT_EQ(log, "A:Update;B:Update;O1:Update;O2:Update;");
}

TEST(NsAppLayers, RemoveRegularLayerKeepsOverlayOrder)
{
    Layers layers;
    std::string log;

    auto a = std::make_unique<TrackingLayer>("A", &log);
    Layer* aPtr = a.get();
    layers.AddLayer(std::move(a));
    layers.AddLayer(std::make_unique<TrackingLayer>("B", &log));
    layers.AddOverlay(std::make_unique<TrackingLayer>("O", &log));

    auto removed = layers.Remove(aPtr);
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(removed->Name(), "A");
    EXPECT_EQ(layers.Size(), 2u);

    log.clear();
    for (auto& l : layers)
        l->OnUpdate();
    EXPECT_EQ(log, "B:Update;O:Update;");
}

TEST(NsAppLayers, RemoveOverlayLeavesRegularUntouched)
{
    Layers layers;
    std::string log;

    layers.AddLayer(std::make_unique<TrackingLayer>("Game", &log));

    auto overlay = std::make_unique<TrackingLayer>("Pause", &log);
    Layer* overlayPtr = overlay.get();
    layers.AddOverlay(std::move(overlay));

    auto removed = layers.Remove(overlayPtr);
    ASSERT_NE(removed, nullptr);
    EXPECT_EQ(layers.Size(), 1u);
}

TEST(NsAppLayers, RemoveUnknownLayerReturnsNull)
{
    Layers layers;
    std::string log;

    layers.AddLayer(std::make_unique<TrackingLayer>("A", &log));

    TrackingLayer stranger("Stranger", &log);
    auto removed = layers.Remove(&stranger);
    EXPECT_EQ(removed, nullptr);
    EXPECT_EQ(layers.Size(), 1u);
}

TEST(NsAppLayers, RemoveNullPtrIsNoOp)
{
    Layers layers;
    std::string log;

    layers.AddLayer(std::make_unique<TrackingLayer>("A", &log));
    auto removed = layers.Remove(nullptr);
    EXPECT_EQ(removed, nullptr);
    EXPECT_EQ(layers.Size(), 1u);
}

TEST(NsAppLayers, AddNullPtrIsNoOp)
{
    Layers layers;
    layers.AddLayer(nullptr);
    layers.AddOverlay(nullptr);
    EXPECT_TRUE(layers.Empty());
}

TEST(NsAppLayers, ReverseIterationForDetach)
{
    Layers layers;
    std::string log;

    layers.AddLayer(std::make_unique<TrackingLayer>("A", &log));
    layers.AddLayer(std::make_unique<TrackingLayer>("B", &log));
    layers.AddOverlay(std::make_unique<TrackingLayer>("O", &log));

    for (auto it = layers.rbegin(); it != layers.rend(); ++it)
        (*it)->OnDetach();

    EXPECT_EQ(log, "O:Detach;B:Detach;A:Detach;");
}
