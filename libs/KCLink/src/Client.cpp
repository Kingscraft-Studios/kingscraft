#include <KCLink/Client.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace kc::link {

    Connection Client::connect(const std::string& socketPath) const {
        if (socketPath.empty() || socketPath.size() >= sizeof(sockaddr_un::sun_path))
            throw std::invalid_argument("KCLink: socket path is empty or too long");

        const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (descriptor < 0) throw std::system_error(errno, std::generic_category(), "socket");

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, socketPath.c_str(), socketPath.size() + 1);
        if (::connect(descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            const int error = errno;
            ::close(descriptor);
            throw std::system_error(error, std::generic_category(), "connect");
        }
        return Connection(descriptor);
    }

}
