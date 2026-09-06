#pragma once
#include <future>
#include <thread>
#include <unordered_map>

#include "Threads/BaseThread.hpp"
#include "Util/LogUtils.hpp"

namespace kc {

    enum class Workers {
        Kingscraft,
        Renderer,
        Input
    };

    class WorkerPool {
    public:

        static WorkerPool& get() { static WorkerPool pool; return pool; }

        // Lifecycle
        void start();
        void stop();
        void run();

        // Persistent Thread API
        template <typename T, typename... Args>
        T* startPersistentWorker(Workers worker, Args&&... args) {
            static_assert(std::is_base_of_v<BaseThread, T>, "T must derive from BaseThread");

            if (findWorker(worker) != nullptr) {
                LogUtils::error(ThreadName::WorkerPool, "Thread Already Exists!");
                return nullptr;
            }

            std::promise<std::unique_ptr<BaseThread>> promise;
            std::future<std::unique_ptr<BaseThread>> future = promise.get_future();

            std::thread t([p = std::move(promise), ...capturedArgs = std::forward<Args>(args)]() mutable {
                auto instance = std::make_unique<T>(std::forward<decltype(capturedArgs)>(capturedArgs)...);
                BaseThread* rawPtr = instance.get();
                p.set_value(std::move(instance));

                rawPtr->start();
                rawPtr->run();
            });

            auto instance = future.get();

            // Cache a raw pointer to the exact instance before moving it into the map
            T* typedRawPtr = static_cast<T*>(instance.get());

            persistentThread.try_emplace(worker, PersistentWorkerEntry{std::move(instance), std::move(t)});

            return typedRawPtr;
        }
        void stopPersistentWorker(Workers worker);

        std::shared_future<void> startWorker(std::function<void()> task);

    private:
        struct PersistentWorkerEntry {
            std::unique_ptr<BaseThread> instance;
            std::thread nativeThread;
        };

        struct TransientWorker {
            std::thread thread;
            std::shared_future<void> done;    // completion signal, so stop() never over-joins
        };

        PersistentWorkerEntry* findWorker(Workers worker);
        void reapFinishedWorkers();
        ThreadName workerToThreadName(Workers worker) {
            switch (worker) {
                case Workers::Input: return ThreadName::Input;
                case Workers::Kingscraft: return ThreadName::GameLogic;
                case Workers::Renderer: return ThreadName::Renderer;
            }
            return ThreadName::Engine;
        }

        std::unordered_map<Workers, PersistentWorkerEntry> persistentThread;

        std::vector<TransientWorker> transientWorkers;
        std::mutex transientMutex;

        std::shared_ptr<Mailbox> mailbox;
        std::thread mailboxThread;

        std::atomic<bool> running = false;
    };
}
