#include "Threads/Engine.hpp"
#include "Threads/Logger.hpp"
#include "Threads/IO.hpp"
#include "Bus/MessageBus.hpp"
#include "Util/TimeUtil.hpp"
#include "../../include/Core/Bootstrapper.hpp"

#include <chrono>

#include "Core/Constants.hpp"
#include "Core/Registries.hpp"
#include "Threads/InputThread.hpp"
#include "Util/LogUtils.hpp"

namespace lve {

    std::unique_ptr<Engine> Engine::instance_ = nullptr;

    Engine::~Engine() {
        if (!shutdownComplete_.load(std::memory_order_acquire)) {
            mailbox_->stop();
            resourceLoader_.stop();
            if (resLoaderThread_.joinable()) resLoaderThread_.join();
            GameLogicThread::getInstance().stop();
            if (gameLogicThread_.joinable()) gameLogicThread_.join();
            RenderThread::getInstance().shutdown();
            if (rendererThread_.joinable()) rendererThread_.join();
            InputThread::getInstance().Shutdown();
            if (inputThread.joinable()) inputThread.join();
        }
        // Stop Any Threads which are temporary and supposed to exits when the function returns
        if (regThread_.joinable()) regThread_.join();
    }

    void Engine::Init() {
        instance_ = std::make_unique<Engine>();
    }

    void Engine::Shutdown() {
        instance_.reset();
    }

    Engine& Engine::Get() {
        return *instance_;
    }

    void Engine::run() {
        // TODO: Call a Thread Init in its own Thread NOT on Engine Thread
        TimeUtil::Init();
        IO::Init();
        Logger::Init();

        InputThread::getInstance().Init({DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, "Kingscraft"});
        GameLogicThread::getInstance().init();

        mailbox_ = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::Engine, mailbox_);


        Bootstrapper::Init();
        Bootstrapper::Get().loadAll();

        // Threads Start up
        resLoaderThread_ = std::thread([this]() { resourceLoader_.run(); });
        regThread_ = std::thread([]() { Registries::build(); });
        inputThread = std::thread([]() {InputThread::getInstance().run(); });
        RenderThread::getInstance().setQuitCallback([this]() { stop(); });
        rendererThread_ = std::thread([]() { RenderThread::getInstance().run(); });
        gameLogicThread_ = std::thread([]() { GameLogicThread::getInstance().run(); });

        while (running_) {
            Message msg;
            while (mailbox_->pop_for(msg, std::chrono::milliseconds(100))) {
                if (msg.payload) msg.payload();
            }
        }


        LogUtils::info(ThreadName::Engine, "Shutting down");
        GameLogicThread::getInstance().stop();
        if (gameLogicThread_.joinable()) gameLogicThread_.join();
        RenderThread::getInstance().shutdown();
        if (rendererThread_.joinable()) rendererThread_.join();
        InputThread::getInstance().Shutdown();
        if (inputThread.joinable()) inputThread.join();
        resourceLoader_.stop();
        if (resLoaderThread_.joinable()) resLoaderThread_.join();

        // Sync Everything
        Message msg;
        while (mailbox_->try_pop(msg)) {
            if (msg.payload) msg.payload();
        }

        MessageBus::Get().unsubscribe(ThreadName::Engine);
        Logger::Shutdown();
        IO::Shutdown();
        MessageBus::Get().signalQuit();
        shutdownComplete_.store(true, std::memory_order_release);
    }

    void Engine::stop() {
        running_ = false;
        mailbox_->stop();
    }

} // namespace lve
