export module game.thread;

import std;

import misc.str;
import misc.dict;
import misc.number;

export namespace craftbuild {
	struct ThreadRegistry {
		inline static std::unordered_map<std::thread::id, Str> threads;
		inline static std::mutex threads_mutex;

        static void register_thread(Str const& thread_name);
        static Str get_name(std::thread::id const& thread_id);
	};

    class ThreadPool {
        std::vector<std::thread> workers;
        std::queue<std::function<void()>> tasks;

        std::mutex mutex;
        std::condition_variable cv;
        bool stop;

    public:
        ThreadPool(size_t n);
        ~ThreadPool();

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
