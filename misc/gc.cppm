export module misc.gc;

import std;

import misc.list;
import misc.dict;
import misc.number;

namespace craftbuild {
    export struct GCObject {
        bool __marked__ = false;
		void* __data__ = nullptr;

        explicit GCObject(void* data) noexcept : __data__(data) {}

        virtual ~GCObject() noexcept = default;
        virtual void get_refs(List<GCObject*>&) noexcept {}
    };

    class NewQueue final {
        struct Data final {
            List<GCObject*> obj_queue;
            Dict<GCObject*, i64> root_queue;
        };

        Data __data__;
        mutable std::mutex __mtx__;

    public:
        void register_object(GCObject* obj) {
            if (not obj) [[unlikely]] return;
            std::lock_guard lock(__mtx__);
            __data__.obj_queue.append(obj);
			__data__.root_queue[obj];
        }

        bool is_registered(GCObject* obj) const {
            if (not obj) [[unlikely]] return false;
            std::lock_guard lock(__mtx__);
            return __data__.root_queue.contains(obj);
        }

        void add_root(GCObject* obj) {
            if (not obj) [[unlikely]] return;
            std::lock_guard lock(__mtx__);
            ++__data__.root_queue[obj];
        }
        void remove_root(GCObject* obj) {
            if (not obj) [[unlikely]] return;
            std::lock_guard lock(__mtx__);
            --__data__.root_queue[obj];
        }

        Data flush() {
            Data result;
            {
                std::lock_guard lock(__mtx__);
                result.obj_queue.swap(__data__.obj_queue);
                result.root_queue.swap(__data__.root_queue);
            }
            return result;
        }
    };

    namespace garbage_collector {
        inline static NewQueue registration_queue;

        inline static List<GCObject*> all_objects;
        inline static Dict<GCObject*, usize> root_objects;

        export void register_object(GCObject* obj) {
			if (root_objects.contains(obj) or registration_queue.is_registered(obj)) [[unlikely]] return;
            registration_queue.register_object(obj);
        }

        export void add_root(GCObject* obj) { registration_queue.add_root(obj); }
        export void remove_root(GCObject* obj) { registration_queue.remove_root(obj); }

        export void collect() {
            auto const new_objects = registration_queue.flush();
            all_objects.append(new_objects.obj_queue);

            for (auto const& [root, delta] : new_objects.root_queue) {
                i64 const current_count = root_objects[root] + delta;
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

                refs.clear();
                obj->get_refs(refs);
                for (GCObject* ref : refs) {
                    if (ref and not ref->__marked__) gray_stack.append(ref);
                }
            }

            usize idx = 0;
            while (idx != len(all_objects)) {
                GCObject* obj = all_objects[idx];

                if (not obj->__marked__) {
                    all_objects.pop(idx);
                    delete obj;
                }
                else {
                    obj->__marked__ = false;
                    ++idx;
                }
            }
        }
    }
}