module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.pipeline.JumpFloodComputer;

import std;
import vku;
import :helpers.extended_arithmetic;

vk_gltf_viewer::vulkan::pipeline::JumpFloodComputer::DescriptorSetLayouts::DescriptorSetLayouts(
    const vk::raii::Device &device
) : vku::DescriptorSetLayouts<1> {
        device,
        vk::DescriptorSetLayoutCreateInfo {
            {},
            vku::unsafeProxy({
                vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, 2, vk::ShaderStageFlagBits::eCompute },
            }),
        },
    } { }

vk_gltf_viewer::vulkan::pipeline::JumpFloodComputer::JumpFloodComputer(
    const vk::raii::Device &device
) : descriptorSetLayouts { device },
    pipelineLayout { device, vk::PipelineLayoutCreateInfo {
        {},
        vku::unsafeProxy(descriptorSetLayouts.getHandles()),
        vku::unsafeProxy({
            vk::PushConstantRange {
                vk::ShaderStageFlagBits::eCompute,
                0, sizeof(PushConstant),
            },
        }),
    } },
    pipeline { device, nullptr, vk::ComputePipelineCreateInfo {
        {},
        get<0>(vku::createPipelineStages(
            device,
            vku::Shader { COMPILED_SHADER_DIR "/jump_flood.comp.spv", vk::ShaderStageFlagBits::eCompute }).get()),
        *pipelineLayout,
    } } { }

auto vk_gltf_viewer::vulkan::pipeline::JumpFloodComputer::compute(
    vk::CommandBuffer commandBuffer,
    const DescriptorSets &descriptorSets,
    std::uint32_t initialSampleOffset,
    const vk::Extent2D &imageExtent
) const -> vk::Bool32 {
    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSets, {});

    PushConstant pushConstant {
        .forward = true,
        .sampleOffset = initialSampleOffset,
    };
    for (; pushConstant.sampleOffset > 0U; pushConstant.forward = !pushConstant.forward, pushConstant.sampleOffset >>= 1U) {
        commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pushConstant);
        commandBuffer.dispatch(
            divCeil(imageExtent.width, 16U),
            divCeil(imageExtent.height, 16U),
            1);

        if (pushConstant.sampleOffset != 1U) {
            commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                {},
                vk::MemoryBarrier { vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead },
                {}, {});
        }
    }
    return pushConstant.forward;
}