#pragma once

//! @brief マテリアルのシェーダーの値 (MaterialParams の 1 項目) を ImGui の欄で直す関数
//! どれも欄の ID を "##value" で作るので、呼び手が項目ごとに PushID する

#include "NSlib/Core/Types.h"

namespace NS
{
    struct MaterialParamValue;
} // namespace NS

namespace NS
{
    namespace Editor
    {
        //! 浮動小数の項目の書式。表の最小の幅もこの書式で測る
        inline constexpr const char* k_MaterialFloatFormat = "%.3f";

        //! @brief 1〜4 成分の浮動小数の欄。値を変えたフレームに true
        //! @details 触った成分だけを float の一番短い表記で書き戻し、触っていない成分は元の値のまま残す
        bool DragParamFloats(MaterialParamValue& value);

        //! @brief 整数の欄。lowest〜highest の外の値は、ドラッグでも打ち込みでも端で止める。値を変えたフレームに true
        bool DragParamInteger(int64 lowest, int64 highest, MaterialParamValue& value);

        //! 真偽の欄。値を変えたフレームに true
        bool DrawParamBool(MaterialParamValue& value);
    } // namespace Editor
} // namespace NS
