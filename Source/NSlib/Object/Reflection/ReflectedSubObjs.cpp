#include "NSlib/Object/Reflection/ReflectedSubObjs.h"

#include "NSlib/Object/SceneActor.h"

namespace NS::Obj
{
    namespace
    {
        void VisitReflectedSubObjects(const SceneActor& actor,
                                      const ReflectionInfo* info,
                                      const SubObjFieldVisitor& visit)
        {
            if (info == nullptr)
            {
                return;
            }
            VisitReflectedSubObjects(actor, info->base, visit);
            for (std::size_t i = 0; i < info->subObjectCount; ++i)
            {
                const SubObjectDesc& field = info->subObjects[i];
                if (SubObject* part = field.get(&actor))
                {
                    visit(field, part);
                }
            }
        }
    } // namespace

    void ForEachReflectedSubObj(const SceneActor& actor, const SubObjFieldVisitor& visit)
    {
        VisitReflectedSubObjects(actor, actor.GetReflection(), visit);
    }

} // namespace NS::Obj
