#include "Threads/Engine.hpp"
#include "Threads/Logger.hpp"
#include "Threads/IO.hpp"
#include "Bus/MessageBus.hpp"
#include "Util/TimeUtil.hpp"
#include "Util/Preloader.hpp"

#include <chrono>

#include "Core/Constants.hpp"
#include "Core/Registries.hpp"
#include "Threads/InputThread.hpp"
#include "Util/LogUtils.hpp"

namespace lve {

    std::unique_ptr<Engine> Engine::instance_ = nullptr;

    Engine::~Engine() {
        mailbox_->stop();
        resourceLoader_.stop();
        if (resLoaderThread_.joinable()) resLoaderThread_.join();
        if (regThread_.joinable()) regThread_.join();
        InputThread::getInstance().Shutdown();
        if (inputThread.joinable()) inputThread.join();
        if (rendererThread_.joinable()) rendererThread_.join();
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
        TimeUtil::Init();
        IO::Init();
        Logger::Init();

        InputThread::getInstance().Init({DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, "Kingscraft"});


        mailbox_ = std::make_shared<Mailbox>();
        MessageBus::Get().subscribe(ThreadName::Engine, mailbox_);


        Preloader::Init();
        Preloader::Get().loadAll();

        resLoaderThread_ = std::thread([this]() { resourceLoader_.run(); });

        regThread_ = std::thread([]() { Registries::build(); });

        inputThread = std::thread([]() {InputThread::getInstance().run(); });

        renderer_.setQuitCallback([this]() { stop(); });
        rendererThread_ = std::thread([this]() { renderer_.run(); });

        while (running_) {
            Message msg;
            while (mailbox_->pop_for(msg, std::chrono::milliseconds(100))) {
                if (msg.payload) msg.payload();
            }
        }

        LogUtils::info(ThreadName::Engine, "Shutting down");
        if (rendererThread_.joinable()) rendererThread_.join();

        InputThread::getInstance().Shutdown();
        if (inputThread.joinable()) inputThread.join();

        resourceLoader_.stop();
        if (resLoaderThread_.joinable()) resLoaderThread_.join();

        Message msg;
        while (mailbox_->try_pop(msg)) {
            if (msg.payload) msg.payload();
        }

        MessageBus::Get().unsubscribe(ThreadName::Engine);

        Logger::Shutdown();
        IO::Shutdown();

        MessageBus::Get().signalQuit();
    }

    void Engine::stop() {
        running_ = false;
        mailbox_->stop();
    }

} // namespace lve
