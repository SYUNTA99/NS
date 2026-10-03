#include "TestEffectFiles.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// 溜まる光の外側の濃い青 (塗り) は、重なる加算の光より先に描く。Effekseer は節の並びのとおりに描き並べ替えないので、
// 加算の後に塗ると、内側の白い光の上を青で塗りつぶす
TEST(EffectChargeContrast, AuraPaintPartnersAreDrawnBeforeEveryAdditiveLayer)
{
    const Effekseer::EffectRef effect = LoadEffectFile("Assets/Effects/charge.gather.efkefc");
    ASSERT_NE(effect, nullptr);
    std::vector<Effekseer::EffectNode*> nodes;
    CollectEffectNodesInDrawOrder(effect->GetRoot(), nodes);

    const std::u16string rimTexture = u"charge_aura_rim.png";
    int partners = 0;
    int lastPartner = -1;
    int firstAdditive = -1;
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
    {
        const Effekseer::EffectBasicRenderParameter& render = nodes[i]->GetBasicRenderParameter();
        const std::u16string texture = EffectNodeColorTexture(effect, nodes[i]);
        if (texture.ends_with(rimTexture))
        {
            EXPECT_EQ(render.AlphaBlend, Effekseer::AlphaBlendType::Blend) << "相方の " << i << " 番が塗りでない";
            ++partners;
            lastPartner = i;
        }
        if (render.AlphaBlend == Effekseer::AlphaBlendType::Add && firstAdditive < 0)
        {
            firstAdditive = i;
        }
    }
    // 包む光の加算 9 枚のうち、白いはじけの 2 枚を除く 7 枚に相方を付ける
    EXPECT_EQ(partners, 7);
    ASSERT_GE(firstAdditive, 0);
    EXPECT_LT(lastPartner, firstAdditive);
}
