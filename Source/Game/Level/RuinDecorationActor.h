#pragma once

#include "NSlib/CoreObject/SceneActor.h"

namespace GL
{
    namespace Level
    {
        class RuinDecorationActor : public NS::SceneActor
        {
        public:
            NS_REFLECT_NONE(RuinDecorationActor, NS::SceneActor)

        protected:
            void OnInit() override;
        };
    }
}
