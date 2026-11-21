#include <KCLink/Client.hpp>

#include "internal/ConnectionCore.hpp"
#include "internal/Unix/UnixSocket.hpp"

#include <memory>
#include <utility>

namespace kc::link {

    Connection Client::connect(const std::string& socketPath) const {
        return Connection(std::make_unique<detail::ConnectionCore>(
            detail::connectLocal(socketPath)));
    }

}
