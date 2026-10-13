#pragma once

#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace NS::Obj
{
    //! @brief 切る演出の名前の一覧。名前はエフェクトの名前と、Game が決める演出の名前
    //! @details プロセスに 1 つで、場面にも保存しない。立ち上げ直すと全部入りに戻る
    class EffectSwitches
    {
    public:
        [[nodiscard]] static EffectSwitches& Get();

        //! @brief エフェクトの名前を、切れる名前に足す
        //! @details EffectKeeper::Preload が、先に読む名前を知らせる
        void AddEffectName(std::string_view name);

        //! @brief 名前を切る
        //! @details 名前を確かめずに切る。知らない名前を断るのは呼ぶ側
        void TurnOff(std::string_view name);

        //! 名前を入れ直す。切っていない名前なら何もしない
        void TurnOn(std::string_view name);

        void TurnOnAll() noexcept;

        //! name を切っている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsOff(std::string_view name) const;

        //! AddEffectName で知らせたエフェクトの名前。名前の順
        [[nodiscard]] const std::set<std::string, std::less<>>& EffectNames() const noexcept { return m_effectNames; }

    private:
        EffectSwitches() = default;

        std::set<std::string, std::less<>> m_off;
        std::set<std::string, std::less<>> m_effectNames;
    };
} // namespace NS::Obj
