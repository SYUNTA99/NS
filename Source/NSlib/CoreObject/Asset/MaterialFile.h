#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace NS
{
    struct MaterialFileDesc;

    //! JSON 文字列を MaterialFileDesc へ解析する。成功で true、失敗時は outError に理由を入れる
    //! vs / ps は必須、他は欠落時に既定値。綴りの合わない混ぜ方は Opaque。GPU を使わないので装置無しで試せる
    //! params の壊れた値は名前を WARN に出して 1 つずつ捨て、ほかの値は読む
    [[nodiscard]] bool ParseMaterialJson(std::string_view jsonText, MaterialFileDesc& out, std::string& outError);

    //! MaterialFileDesc を .mat の JSON 文字列にする。ParseMaterialJson で読み直すと同じ値になる
    //! テクスチャが k_MaterialTextureSlots 枚より多い時、基本の色が数でない時、
    //! ParseMaterialJson が読めない材質の値がある時は nullopt
    //! 鍵の並びと書き方は今の .mat と同じで、値を変えずに書くと同じ文字列になる。params は空なら書かない
    [[nodiscard]] std::optional<std::string> MakeMaterialJson(const MaterialFileDesc& file);
} // namespace NS
