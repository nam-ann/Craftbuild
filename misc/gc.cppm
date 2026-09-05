export module misc.gc;

import std;
import misc.list;
import misc.dict;
import misc.number;

export namespace craftbuild {
    struct GCObject {
        void* __data__ = nullptr;
        void(*__deleter__)(void*) = nullptr;
        void(*__get_refs__)(void*, List<GCObject*>&) = nullptr;

        bool __marked__ = false;

        virtual ~GCObject() = default;
    };

    class NewQueue {
    public:
        struct Data {
            List<GCObject*> obj_queue;
            Dict<GCObject*, int64> root_queue;
        };

    private:
        Data __data__;
        std::mutex __mtx__;

    public:
        void register_object(GCObject* obj) {
            if (not obj) [[unlikely]] return;
            std::lock_guard<std::mutex> lock(__mtx__);
            __data__.obj_queue.append(obj);
        }

        void add_root(GCObject* obj) {
            if (not obj) [[unlikely]] return;
            std::lock_guard<std::mutex> lock(__mtx__);
            ++__data__.root_queue[obj];
        }
        void remove_root(GCObject* obj) {
            if (not obj) [[unlikely]] return;
            std::lock_guard<std::mutex> lock(__mtx__);
			--__data__.root_queue[obj];
        }

        Data flush() {
            Data result;
            {
                std::lock_guard<std::mutex> lock(__mtx__);
                result.obj_queue.swap(__data__.obj_queue);
                result.root_queue.swap(__data__.root_queue);
            }
            return result;
        }
    };

    class GarbageCollector {
		inline static NewQueue registration_queue;

        inline static List<GCObject*> all_objects;
        inline static Dict<GCObject*, usize> root_objects;

    public:
        static void register_object(GCObject* obj) { registration_queue.register_object(obj); }
         
        static void add_root(GCObject* obj) { registration_queue.add_root(obj); }
        static void remove_root(GCObject* obj) { registration_queue.remove_root(obj); }

        static void collect() {
            const NewQueue::Data new_objects = registration_queue.flush();
            all_objects.append(new_objects.obj_queue);

            for (auto const& [root, delta] : new_objects.root_queue) {
                int64 const current_count = root_objects[root] + delta;
                if (current_count <= 0) root_objects.erase(root);
                else root_objects[root] = usize(current_count);
            }

            List<GCObject*> gray_stack;
			gray_stack.expect(root_objects.size());

            for (auto const& [root, count] : root_objects) {
                if (root and not root->__marked__) gray_stack.append(root);
            }

            List<GCObject*> refs;

            while (gray_stack) {
                GCObject* obj = gray_stack[-1];
                gray_stack.pop();

                if (not obj or obj->__marked__) continue;
                obj->__marked__ = true;

                if (obj->__get_refs__) {
                    refs.clear();
                    obj->__get_refs__(obj->__data__, refs);
                    for (GCObject* ref : refs) {
                        if (ref and not ref->__marked__) gray_stack.append(ref);
                    }
                }
            }

            usize it = 0;
            while (it != len(all_objects)) {
                GCObject* obj = all_objects[it];

                if (not obj->__marked__) {
                    all_objects.pop(it);
                    if (obj->__deleter__ and obj->__data__) obj->__deleter__(obj->__data__);
                    delete obj; obj = nullptr;
                }
                else {
                    obj->__marked__ = false;
                    ++it;
                }
            }
        }
    };
}