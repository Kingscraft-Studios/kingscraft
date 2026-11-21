#pragma once

#include <KCLink/Connection.hpp>
#include <KCLink/Export.hpp>

#include <string>

namespace kc::link {

    class KCLINK_API Client {
    public:
        Connection connect(const std::string& socketPath) const;
    };

}
