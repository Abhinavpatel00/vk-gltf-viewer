export module vk_gltf_viewer:vulkan.pipeline.OutlineRenderer;

import std;
export import glm;
export import vku;

namespace vk_gltf_viewer::vulkan::pipeline {
    export struct OutlineRenderer {
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1>{
            explicit DescriptorSetLayouts(const vk::raii::Device &device [[clang::lifetimebound]]);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                vk::ImageView jumpFloodImageView
            ) const {
                return vku::RefHolder {
                    [this](const vk::DescriptorImageInfo &jumpFloodImageInfo) {
                        return getDescriptorWrite<0, 0>().setImageInfo(jumpFloodImageInfo);
                    },
                    vk::DescriptorImageInfo { {}, jumpFloodImageView, vk::ImageLayout::eGeneral },
                };
            }
        };

        struct PushConstant {
            glm::vec4 outlineColor;
            glm::i32vec2 passthruOffset;
            float outlineThickness;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit OutlineRenderer(const vk::raii::Device &device [[clang::lifetimebound]]);

        auto bindPipeline(vk::CommandBuffer commandBuffer) const -> void;
        auto bindDescriptorSets(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets) const -> void;
        auto pushConstants(vk::CommandBuffer commandBuffer, const PushConstant &pushConstant) const -> void;
        auto draw(vk::CommandBuffer commandBuffer) const -> void;
    };
}