#pragma once

#include <KCLink/Message.hpp>

namespace kc::link {

    // A blocking, framed stream. A Connection is movable but not copyable.
    // Calls on the same Connection must be externally synchronized.
    class Connection {
    public:
        Connection() noexcept = default;
        ~Connection();

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection&& other) noexcept;
        Connection& operator=(Connection&& other) noexcept;

        bool isOpen() const noexcept;
        void send(const Message& message);

        // Returns false when the peer has cleanly disconnected before a new
        // message starts. A truncated frame is reported as an exception.
        bool receive(Message& message);

        void close() noexcept;

    private:
        explicit Connection(int socket) noexcept;
        int socket = -1;

        friend class Client;
        friend class Server;
    };

}
