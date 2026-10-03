#pragma once

#include <Effekseer.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

//! @brief 書き出したエフェクトの絵を読み、節を描く順に並べる試し用の補助
//! @details 絵の定義の並びや欄が書き出しで残ったかを、実行側が読む形で確かめる
//! 依存: Effekseer::Effect

//! 書き出した絵を読む。読めない時は空
inline Effekseer::EffectRef LoadEffectFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (bytes.empty())
    {
        return nullptr;
    }
    const Effekseer::SettingRef setting = Effekseer::Setting::Create();
    return Effekseer::Effect::Create(setting, bytes.data(), static_cast<int32_t>(bytes.size()));
}

//! 根から深さ優先で節を並べる。Effekseer は並べ替えずにこの順で描く
inline void CollectEffectNodesInDrawOrder(Effekseer::EffectNode* node, std::vector<Effekseer::EffectNode*>& out)
{
    out.push_back(node);
    for (int i = 0; i < node->GetChildrenCount(); ++i)
    {
        CollectEffectNodesInDrawOrder(node->GetChild(i), out);
    }
}

//! 節の色のテクスチャのファイル名を返す。テクスチャを持たない節は空
inline std::u16string EffectNodeColorTexture(const Effekseer::EffectRef& effect, Effekseer::EffectNode* node)
{
    const int32_t index = node->GetBasicRenderParameter().TextureIndexes[0];
    if (index < 0 || index >= effect->GetColorImageCount())
    {
        return std::u16string();
    }
    return std::u16string(effect->GetColorImagePath(index));
}
