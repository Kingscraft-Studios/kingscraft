#pragma once

#include "Device.hpp"
#include <vector>

namespace lve {

    struct PipelineConfigInfo {
        PipelineConfigInfo() = default;

        PipelineConfigInfo(const PipelineConfigInfo &) = default;

        PipelineConfigInfo &operator=(const PipelineConfigInfo &) = delete;

        // 1. ADD THESE TWO LINES (The missing "contract" for vertex data)
        std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

        VkPipelineViewportStateCreateInfo viewportInfo;
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
        VkPipelineRasterizationStateCreateInfo rasterizationInfo;
        VkPipelineMultisampleStateCreateInfo multisampleInfo;
        VkPipelineColorBlendAttachmentState blendAttachmentState;
        VkPipelineColorBlendStateCreateInfo colorBlendInfo;
        VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
        std::vector<VkDynamicState> dynamicStatesEnables;
        VkPipelineDynamicStateCreateInfo dynamicStateInfo;
        VkPipelineLayout pipelineLayout = nullptr;
        VkRenderPass renderPass = nullptr;
        uint32_t subpass = 0;

        std::vector<VkDescriptorSetLayout> descriptorSetLayouts;

        // Specialization constants for fragment shader
        std::vector<VkSpecializationMapEntry> specMapEntries;
        std::vector<uint32_t> specData;
    };

    class Pipeline {
    public:
        Pipeline(
                Device &device,
                const std::vector<char>& vertCode,
                const std::vector<char>& fragCode,
                const PipelineConfigInfo& configInfo);

        ~Pipeline();

        Pipeline(const Pipeline &) = delete;

        Pipeline &operator=(const Pipeline &) = delete;

        void bind(VkCommandBuffer commandBuffer);

        VkPipeline getHandle() const { return graphicsPipeline; }

        static void defaultPipelineConfigInfo(PipelineConfigInfo &configInfo);

    private:
        void createGraphicsPipeline(const std::vector<char>& vertCode, const std::vector<char>& fragCode,
                                    const PipelineConfigInfo &configInfo);


        void createShaderModule(const std::vector<char> &code, VkShaderModule *shaderModule);

        Device &device;
        VkPipeline graphicsPipeline;
        VkShaderModule vertShaderModule;
        VkShaderModule fragShaderModule;

    };
}