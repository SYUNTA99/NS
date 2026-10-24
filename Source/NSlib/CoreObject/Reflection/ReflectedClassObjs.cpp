#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "NSlib/CoreObject/SceneActor.h"

namespace NS
{
    namespace
    {
        void VisitReflectedClassObjs(const SceneActor& actor,
                                     const ReflectionInfo* info,
                                     const ClassObjFieldVisitor& visit)
        {
            if (info == nullptr)
            {
                return;
            }
            VisitReflectedClassObjs(actor, info->base, visit);
            for (std::size_t i = 0; i < info->classObjCount; ++i)
            {
                const ClassObjInfo& field = info->classObjs[i];
                if (Object* part = field.get(&actor))
                {
                    visit(field, part);
                }
            }
        }
    } // namespace

    void ForEachReflectedClassObj(const SceneActor* actor, const ClassObjFieldVisitor& visit)
    {
        NS_ASSERT(Scene, actor != nullptr, "ForEachReflectedClassObj: アクターが null");
        VisitReflectedClassObjs(*actor, actor->GetReflection(), visit);
    }

} // namespace NS
