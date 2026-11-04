#pragma once

#include "NSlib/CoreObject/SceneActor.h"

namespace GL
{
    namespace Level
    {
        //! 海や湖の水面。空の後に描くので、画面の深度と空のキューブマップを読める
        //! 上に立たせないため、地形の当たりは持たない
        class WaterActor : public NS::SceneActor
        {
        public:
            NS_REFLECT_NONE(WaterActor, NS::SceneActor)

        protected:
            void OnInit() override;
        };
    } // namespace Level
} // namespace GL
