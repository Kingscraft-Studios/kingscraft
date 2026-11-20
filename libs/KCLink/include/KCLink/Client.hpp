#pragma once

#include <KCLink/Connection.hpp>

#include <string>

namespace kc::link {

    class Client {
    public:
        Connection connect(const std::string& socketPath) const;
    };

}
