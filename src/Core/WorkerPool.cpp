#include "Core/WorkerPool.hpp"

#include "Core/Runtime.hpp"
#include "Util/LogUtils.hpp"

namespace kc {
    void WorkerPool::start() {
        running = true;
        mailbox = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::WorkerPool, mailbox);
        mailboxThread = std::thread([this](){ run();});
    }

    void WorkerPool::run() {            // existing loop, on mailboxThread
        Message msg;
        while (running) {
            if (mailbox->try_pop(msg)) {
                if (msg.payload) msg.payload();
            } else {
                reapFinishedWorkers();  // ← continuous cleanup, no dependency on startWorker()
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    void WorkerPool::stop() {
        running = false;
        mailbox->stop();
        MessageBus::Get().unsubscribe(ThreadName::WorkerPool);

        while (!persistentThread.empty()) {
            stopPersistentWorker(persistentThread.begin()->first);
        }

        {
            std::lock_guard lock(transientMutex);
            for (auto& w : transientWorkers) {
                w.done.wait();
                w.thread.join();
            }
            transientWorkers.clear();
        }

        if (mailboxThread.joinable())
            mailboxThread.join();
    }

    void WorkerPool::stopPersistentWorker(Workers worker) {
        auto it = persistentThread.find(worker);
        if (it == persistentThread.end()) {
            LogUtils::warn(ThreadName::WorkerPool, "Thread isn't Present!");
            return;
        }

        BaseThread* instance = it->second.instance.get();
        MessageBus::Get().send(workerToThreadName(worker), [instance]() {
            // Call interface stop method
            instance->signalQuit();
        });

        // Join native thread
        if (it->second.nativeThread.joinable()) {
            it->second.nativeThread.join();
        }

        // Clear the Runtime pointer
        switch (worker) {
            case Workers::Renderer: Runtime::get().renderThread = nullptr; break;
            case Workers::Kingscraft: Runtime::get().kingscraft = nullptr; break;
            case Workers::Input: Runtime::get().inputThread = nullptr; break;
        }

        persistentThread.erase(it);
    }

    std::shared_future<void> WorkerPool::startWorker(std::function<void()> task) {
        std::promise<void> p;
        std::shared_future<void> shared = p.get_future().share();

        std::lock_guard lock(transientMutex);

        transientWorkers.emplace_back(TransientWorker{
            std::thread([task = std::move(task), p = std::move(p)]() mutable {
                try {
                    task();
                } catch (const std::exception& e) {
                    LogUtils::error(ThreadName::WorkerPool, "Worker task threw: " + std::string(e.what()));
                } catch (...) {
                    LogUtils::error(ThreadName::WorkerPool, "Worker task threw an unknown exception");
                }
                p.set_value();
            }),
            shared
        });

        return shared;
    }

    void WorkerPool::reapFinishedWorkers() {          // caller holds transientMutex_
        for (auto it = transientWorkers.begin(); it != transientWorkers.end();) {
            if (it->done.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                it->thread.join();      // finished → instant
                it = transientWorkers.erase(it);
            } else ++it;
        }
    }

    WorkerPool::PersistentWorkerEntry* WorkerPool::findWorker(Workers worker) {
        auto it = persistentThread.find(worker);
        if (it != persistentThread.end()) {
            return &it->second;
        }
        return nullptr;
    }
}
