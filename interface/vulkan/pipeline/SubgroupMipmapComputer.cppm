module;

#include <version>

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.pipeline.SubgroupMipmapComputer;

import std;
import :helpers.ranges;
export import :vulkan.Gpu;
import :vulkan.shader.subgroup_mipmap_16_comp;
import :vulkan.shader.subgroup_mipmap_32_comp;
import :vulkan.shader.subgroup_mipmap_64_comp;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export class SubgroupMipmapComputer {
        struct PushConstant {
            std::uint32_t baseLevel;
            std::uint32_t remainingMipLevels;
        };

    public:
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> {
            DescriptorSetLayout(
                const vk::raii::Device &device [[clang::lifetimebound]],
                std::uint32_t mipImageCount
            ) : vku::DescriptorSetLayout<vk::DescriptorType::eStorageImage> {
                    device,
                    vk::StructureChain {
                        vk::DescriptorSetLayoutCreateInfo {
                            vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool,
                            vku::unsafeProxy(vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eStorageImage, mipImageCount, vk::ShaderStageFlagBits::eCompute }),
                        },
                        vk::DescriptorSetLayoutBindingFlagsCreateInfo {
                            vku::unsafeProxy(vk::Flags { vk::DescriptorBindingFlagBits::eUpdateAfterBind }),
                        },
                    }.get(),
                } { }
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        SubgroupMipmapComputer(
            const Gpu &gpu [[clang::lifetimebound]],
            std::uint32_t mipImageCount
        ) : descriptorSetLayout { gpu.device, mipImageCount },
            pipelineLayout { gpu.device, vk::PipelineLayoutCreateInfo {
                {},
                *descriptorSetLayout,
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eCompute,
                    0, sizeof(PushConstant),
                }),
            } },
            pipeline { gpu.device, nullptr, vk::ComputePipelineCreateInfo {
                {},
                createPipelineStages(
                    gpu.device,
                    vku::Shader {
                        [&]() {
                            switch (gpu.subgroupSize) {
                                case 16U: return shader::subgroup_mipmap_16_comp();
                                case 32U: return shader::subgroup_mipmap_32_comp();
                                case 64U: return shader::subgroup_mipmap_64_comp();
                            }
                            std::unreachable(); // Logic error! This situation must be handled from vulkan::GPU construction.
                        }(),
                        vk::ShaderStageFlagBits::eCompute,
                    }).get()[0],
                *pipelineLayout,
            } } { }

        auto compute(
            vk::CommandBuffer commandBuffer,
            vku::DescriptorSet<DescriptorSetLayout> descriptorSet,
            const vk::Extent2D &baseImageExtent,
            std::uint32_t mipLevels
        ) const -> void {
            // Base image size must be greater than or equal to 32. Therefore, the first execution may process less than 5 mip levels.
            // For example, if base extent is 4096x4096 (mipLevels=13),
            // Step 0 (4096 -> 1024)
            // Step 1 (1024 -> 32)
            // Step 2 (32 -> 1) (full processing required)

#if __cpp_lib_ranges_chunk >= 202202L
             const std::vector indexChunks
                 = std::views::iota(1U, mipLevels)                                         // [1, 2, ..., 11, 12]
                 | std::views::reverse                                                     // [12, 11, ..., 2, 1]
                 | std::views::chunk(5)                                                    // [[12, 11, 10, 9, 8], [7, 6, 5, 4, 3], [2, 1]]
                 | std::views::transform([](auto &&chunk) {
                      return chunk | std::views::reverse | std::ranges::to<std::vector>();
                 })                                                                        // [[8, 9, 10, 11, 12], [3, 4, 5, 6, 7], [1, 2]]
                 | std::views::reverse                                                     // [[1, 2], [3, 4, 5, 6, 7], [8, 9, 10, 11, 12]]
                 | std::ranges::to<std::vector>();
#else
            std::vector<std::vector<std::uint32_t>> indexChunks;
            for (int endMipLevel = mipLevels; endMipLevel > 1; endMipLevel -= 5) {
                indexChunks.emplace_back(
                    std::views::iota(
                        static_cast<std::uint32_t>(std::max(1, endMipLevel - 5)),
                        static_cast<std::uint32_t>(endMipLevel))
                    | std::ranges::to<std::vector>());
            }
            std::ranges::reverse(indexChunks);
#endif

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0, descriptorSet, {});
            for (const auto &[idx, mipIndices] : indexChunks | ranges::views::enumerate) {
                if (idx != 0) {
                    commandBuffer.pipelineBarrier(
                        vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
                        {},
                        vk::MemoryBarrier {
                            vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead,
                        },
                        {}, {});
                }

                commandBuffer.pushConstants<PushConstant>(*pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, PushConstant {
                    mipIndices.front() - 1U,
                    static_cast<std::uint32_t>(mipIndices.size()),
                });
                commandBuffer.dispatch(
                    (baseImageExtent.width >> mipIndices.front()) / 16U,
                    (baseImageExtent.height >> mipIndices.front()) / 16U,
                    6);
            }
        }
    };
}