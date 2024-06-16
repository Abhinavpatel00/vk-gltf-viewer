module;

#include <array>
#include <compare>
#include <string_view>

#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.pipelines.OutlineRenderer;

export import glm;
export import vku;

namespace vk_gltf_viewer::vulkan::pipelines {
    export class OutlineRenderer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1>{
            explicit DescriptorSetLayouts(const vk::raii::Device &device);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                vk::ImageView jumpFloodImageView
            ) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorImageInfo &jumpFloodImageInfo) {
                        return std::array {
                            getDescriptorWrite<0, 0>().setImageInfo(jumpFloodImageInfo),
                        };
                    },
                    vk::DescriptorImageInfo { {}, jumpFloodImageView, vk::ImageLayout::eShaderReadOnlyOptimal },
                };
            }
        };

        struct PushConstant {
            glm::vec3 outlineColor;
            float lineWidth;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        OutlineRenderer(const vk::raii::Device &device, vk::RenderPass renderPass, std::uint32_t subpass, const shaderc::Compiler &compiler);

        auto draw(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, const PushConstant &pushConstant) const -> void;

    private:
        static std::string_view vert, frag;

        [[nodiscard]] auto createPipelineLayout(const vk::raii::Device &device) const -> decltype(pipelineLayout);
        [[nodiscard]] auto createPipeline(const vk::raii::Device &device, vk::RenderPass renderPass, std::uint32_t subpass, const shaderc::Compiler &compiler) const -> decltype(pipeline);
    };
}