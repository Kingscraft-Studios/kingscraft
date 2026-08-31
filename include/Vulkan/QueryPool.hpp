#pragma once

#include "Device.hpp"

namespace kc {

    class QueryPool {
    public:
        QueryPool(Device& device, const VkQueryPoolCreateInfo& info);
        ~QueryPool();

        QueryPool(const QueryPool&) = delete;
        QueryPool& operator=(const QueryPool&) = delete;

        QueryPool(QueryPool&& other) noexcept;
        QueryPool& operator=(QueryPool&& other) noexcept;

        VkQueryPool getHandle() const { return pool_; }
        void reset();

    private:
        Device& device_;
        VkQueryPool pool_ = VK_NULL_HANDLE;
    };

} // namespace kc