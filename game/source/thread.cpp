module;

#include <includes.hpp>
#include <mutex>
#include <queue>
#include <thread>
#include <functional>

module game.thread;

namespace craftbuild {
	void ThreadRegistry::register_thread(Str const& thread_name) {
		std::lock_guard lock(threads_mutex);
		threads[std::this_thread::get_id()] = thread_name;
	}

	Str ThreadRegistry::get_name(std::thread::id const& thread_id) {
		std::lock_guard lock(threads_mutex);
		auto it = threads.find(thread_id);
		if (it == threads.end()) return "Main";
		return it->second;
	}

    ThreadPool::ThreadPool(size_t n) : stop(false) {
        for (size_t i = 0; i < n; ++i) {
            workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex);
                        cv.wait(lock, [this] { return stop or not tasks.empty(); });

                        if (stop and tasks.empty()) return;

                        task.swap(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    ThreadPool::~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        cv.notify_all();

        for (auto& w : workers) w.join();
    }
}