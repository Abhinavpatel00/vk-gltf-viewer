export module vk_gltf_viewer:vulkan.pipeline.SphericalHarmonicsRenderer;

import std;
export import glm;
export import :vulkan.buffer.CubeIndices;

namespace vk_gltf_viewer::vulkan::pipeline {
    export class SphericalHarmonicsRenderer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1>{
            explicit DescriptorSetLayouts(const vk::raii::Device &device [[clang::lifetimebound]]);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                const vk::DescriptorBufferInfo &cubemapSphericalHarmonicsBufferInfo [[clang::lifetimebound]]
            ) const -> vk::WriteDescriptorSet;
        };

        struct PushConstant {
            glm::mat4 projectionView;
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SphericalHarmonicsRenderer(const vk::raii::Device &device [[clang::lifetimebound]], const buffer::CubeIndices &cubeIndices [[clang::lifetimebound]]);

        auto draw(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, const PushConstant &pushConstant) const -> void;

    private:
        const buffer::CubeIndices &cubeIndices;
    };
}