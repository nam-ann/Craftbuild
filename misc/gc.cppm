module;

#include <includes.hpp>
#include <mutex>
#include <atomic>
#include <thread>
#include <algorithm>
#include <unordered_map>

export module misc.gc;

import misc.number;

export namespace craftbuild {
    struct GCObject {
        none* __data__ = nullptr;
        none(*__deleter__)(none*) = nullptr;
        none(*__get_refs__)(none*, std::vector<GCObject*>&) = nullptr;

        bool __marked__ = false;

        virtual ~GCObject() = default;
    };

    class NewQueue {
    public:
        struct Data {
            std::vector<GCObject*> obj_queue;
            std::unordered_map<GCObject*, int64> root_queue;
        };

    private:
        Data __data__;
        std::mutex __mtx__;

    public:
        none register_object(GCObject* obj) {
            if (not obj) [[unlikely]] return;
            std::lock_guard<std::mutex> lock(__mtx__);
            __data__.obj_queue.push_back(obj);
        }

        none add_root(GCObject* obj) {
            if (not obj) [[unlikely]] return;
            std::lock_guard<std::mutex> lock(__mtx__);
            ++__data__.root_queue[obj];
        }
        none remove_root(GCObject* obj) {
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

        inline static std::vector<GCObject*> all_objects;
        inline static std::unordered_map<GCObject*, usize> root_objects;

    public:
        static none register_object(GCObject* obj) { registration_queue.register_object(obj); }
         
        static none add_root(GCObject* obj) { registration_queue.add_root(obj); }
        static none remove_root(GCObject* obj) { registration_queue.remove_root(obj); }

        static none collect() {
            const NewQueue::Data new_objects = registration_queue.flush();
            all_objects.insert(all_objects.end(), new_objects.obj_queue.begin(), new_objects.obj_queue.end());

            for (auto const& [root, delta] : new_objects.root_queue) {
                const int64 current_count = root_objects[root] + delta;
                if (current_count <= 0) root_objects.erase(root);
                else root_objects[root] = (usize)current_count;
            }

            std::vector<GCObject*> gray_stack;
			gray_stack.reserve(root_objects.size());

            for (auto const& [root, count] : root_objects) {
                if (root and not root->__marked__) gray_stack.push_back(root);
            }

            std::vector<GCObject*> refs;

            while (not gray_stack.empty()) {
                GCObject* obj = gray_stack.back();
                gray_stack.pop_back();

                if (not obj or obj->__marked__) continue;
                obj->__marked__ = true;

                if (obj->__get_refs__) {
                    refs.clear();
                    obj->__get_refs__(obj->__data__, refs);
                    for (GCObject* ref : refs) {
                        if (ref and not ref->__marked__) gray_stack.push_back(ref);
                    }
                }
            }

            auto it = all_objects.begin();
            while (it != all_objects.end()) {
                GCObject* obj = *it;
                if (not obj->__marked__) {
                    it = all_objects.erase(it);
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