module;

#include <thread>
#include <includes.hpp>
#include <mutex>
#include <functional>
#include <queue>

export module game.thread;

import misc.str;
import misc.dict;
import misc.number;

export namespace craftbuild {
	struct ThreadRegistry {
		inline static std::unordered_map<std::thread::id, Str> threads;
		inline static std::mutex threads_mutex;

		static none register_thread(const Str& thread_name) {
			std::lock_guard lock(threads_mutex);
			threads[std::this_thread::get_id()] = thread_name;
		}

		static Str get_name(const std::thread::id& thread_id) {
			std::lock_guard lock(threads_mutex);
			auto it = threads.find(thread_id);
			if (it == threads.end()) return "Main Thread";
			return it->second;
		}
	};

    class ThreadPool {
        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;

        std::mutex mutex;
        std::condition_variable cv;
        bool stop;

    public:
        ThreadPool(size_t n) : stop(false) {
            for (size_t i = 0; i < n; ++i) {
                workers.emplace_back([this]() {
                    while (true) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(mutex);
                            cv.wait(lock, [this] { return stop or not tasks.empty(); });

                            if (stop and tasks.empty()) return;

                            task = std::move(tasks.front());
                            tasks.pop();
                        }
                        task();
                    }
                });
            }
        }

        ~ThreadPool() {
            {
                std::lock_guard<std::mutex> lock(mutex);
                stop = true;
            }
            cv.notify_all();

            for (auto& w : workers) w.join();
        }

        template<class F>
        void enqueue(F&& f) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                tasks.emplace(std::forward<F>(f));
            }
            cv.notify_one();
        }
    };
}
