export module vk_gltf_viewer:vulkan.pipeline.JumpFloodComputer;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan::pipeline {
    export class JumpFloodComputer {
    public:
        struct DescriptorSetLayouts : vku::DescriptorSetLayouts<1> {
            explicit DescriptorSetLayouts(const vk::raii::Device &device [[clang::lifetimebound]]);
        };

        struct DescriptorSets : vku::DescriptorSets<DescriptorSetLayouts> {
            using vku::DescriptorSets<DescriptorSetLayouts>::DescriptorSets;

            [[nodiscard]] auto getDescriptorWrites0(
                vk::ImageView pingImageView,
                vk::ImageView pongImageView
            ) const {
                return vku::RefHolder {
                    [this](std::span<const vk::DescriptorImageInfo, 2> pingPongImageInfos) {
                        // TODO: Use following line causes C++ module error in MSVC, looks like related to
                        // https://developercommunity.visualstudio.com/t/error-C2028:-structunion-member-must-be/10488679?sort=newest&topics=Fixed-in%3A+Visual+Studio+2017+version+15.2.
                        // Use setPImageInfo method when available in MSVC.
                        // getDescriptorWrite<0, 0>().setImageInfo(pingPongImageInfos),
                        return getDescriptorWrite<0, 0>().setDescriptorCount(pingPongImageInfos.size()).setPImageInfo(pingPongImageInfos.data());
                    },
                    std::array {
                        vk::DescriptorImageInfo { {}, pingImageView, vk::ImageLayout::eGeneral },
                        vk::DescriptorImageInfo { {}, pongImageView, vk::ImageLayout::eGeneral },
                    },
                };
            }
        };

        DescriptorSetLayouts descriptorSetLayouts;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        explicit JumpFloodComputer(const vk::raii::Device &device [[clang::lifetimebound]]);

        [[nodiscard]] auto compute(vk::CommandBuffer commandBuffer, const DescriptorSets &descriptorSets, std::uint32_t initialSampleOffset, const vk::Extent2D &imageExtent) const -> vk::Bool32;

    private:
        struct PushConstant {
            vk::Bool32 forward;
            std::uint32_t sampleOffset;
        };
    };
}