#include "Editor/MaterialParamFields.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/Asset/Material.h"

#include <array>
#include <charconv>
#include <format>
#include <string>

#include <imgui.h>

namespace NS::Editor
{
        namespace
        {
            // float の一番短い表記を double に読み直す。0.3 と打った値が .mat に 0.3 と残る
            double ToShortestDouble(float number)
            {
                const std::string text = std::format("{}", number);
                double parsed = 0.0;
                const std::from_chars_result read = std::from_chars(text.data(), text.data() + text.size(), parsed);
                if (read.ec != std::errc{})
                {
                    return static_cast<double>(number);
                }
                return parsed;
            }
        } // namespace

        bool DragParamFloats(MaterialParamValue& value)
        {
            const int count = static_cast<int>(value.numbers.size());
            NS_ASSERT(App, count >= 1 && count <= 4, "浮動小数の項目の成分の数が 1〜4 でない");
            std::array<float, 4> before{};
            for (int i = 0; i < count; ++i)
            {
                before[i] = static_cast<float>(value.numbers[i]);
            }
            std::array<float, 4> numbers = before;
            const bool changed = ImGui::DragScalarN("##value", ImGuiDataType_Float, numbers.data(), count, 0.01f,
                                                    nullptr, nullptr, k_MaterialFloatFormat);
            // 触っていない成分を float へ丸めて書き戻すと .mat の値が変わる
            for (int i = 0; i < count; ++i)
            {
                if (numbers[i] != before[i])
                {
                    value.numbers[i] = ToShortestDouble(numbers[i]);
                }
            }
            return changed;
        }

        bool DragParamInteger(int64 lowest, int64 highest, MaterialParamValue& value)
        {
            NS_ASSERT(App, lowest <= highest, "整数の項目の下限が上限より大きい");
            // 符号なしの型で受けると、打ち込んだ負の数が大きな数へ回り込んでから範囲で止めるので、
            // uint32 と int32 のどちらの範囲も入る符号のある 64 ビットで受けて止める
            int64 number = static_cast<int64>(value.numbers[0]);
            const bool changed = ImGui::DragScalar("##value", ImGuiDataType_S64, &number, 0.2f, &lowest, &highest,
                                                   "%lld", ImGuiSliderFlags_AlwaysClamp);
            value.numbers[0] = static_cast<double>(number);
            return changed;
        }

        bool DrawParamBool(MaterialParamValue& value)
        {
            bool flag = value.numbers[0] != 0.0;
            const bool changed = ImGui::Checkbox("##value", &flag);
            value.numbers[0] = static_cast<double>(flag);
            return changed;
        }
} // namespace NS::Editor
