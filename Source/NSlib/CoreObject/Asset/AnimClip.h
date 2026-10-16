#pragma once

#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/CoreObject/Object.h"

#include <memory>
#include <string>

namespace NS
{
    //! @brief 欄の名前で選んだ動き 1 本
    //! @details 名前は「ContentRoot からの glTF の道筋」か「道筋#動きの名前」で、欄に保存する字面そのまま
    //! 読めない名前の時は動きが空で、名前だけを持つ。トラックは骨を名前で持ち、どのモデルにも依らない
    class AnimClip : public Object
    {
    public:
        //! clip が nullptr なら中身の空のクリップ
        AnimClip(std::string path, std::unique_ptr<Anim::AnimClip> clip) noexcept;
        ~AnimClip() noexcept override;

        [[nodiscard]] const std::string& Path() const noexcept;
        //! 読んだ動き。中身の空のクリップは nullptr
        [[nodiscard]] const Anim::AnimClip* Clip() const noexcept;

        NS_REFLECT_NONE(AnimClip, Object)

    private:
        std::string m_path;
        std::unique_ptr<Anim::AnimClip> m_clip;
    };
} // namespace NS
