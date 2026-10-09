#pragma once

#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace GL::Level
{
    //! @brief 切る演出の名前の一覧。名前は事象の種類の名前と層の名前
    //! @details プロセスに 1 つで、場面にもタイムラインにも保存しない。立ち上げ直すと全部入りに戻る
    class EffectSwitches
    {
    public:
        [[nodiscard]] static EffectSwitches& Get();

        //! @brief 層の名前を、切れる名前に足す
        //! @details 層を出す部品が、出す層の名前を OnStart で知らせる
        void AddLayerName(std::string_view name);

        //! @brief 名前を切る
        //! @param[in] names 事象の種類の名前か、AddLayerName で知らせた層の名前
        //! @return 知らないので切らなかった名前。全部切れた場合は空
        [[nodiscard]] std::vector<std::string> TurnOff(const std::vector<std::string>& names);

        //! 名前を入れ直す。切っていない名前なら何もしない
        void TurnOn(std::string_view name);

        void TurnOnAll() noexcept;

        //! name を切っている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsOff(std::string_view name) const;

        //! AddLayerName で知らせた層の名前。名前の順
        [[nodiscard]] const std::set<std::string, std::less<>>& LayerNames() const noexcept { return m_layerNames; }

    private:
        EffectSwitches() = default;

        std::set<std::string, std::less<>> m_off;
        std::set<std::string, std::less<>> m_layerNames;
    };
} // namespace GL::Level
