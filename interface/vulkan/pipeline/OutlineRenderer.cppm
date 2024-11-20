export module vk_gltf_viewer:vulkan.pipeline.OutlineRenderer;

import std;
export import glm;
export import vku;
import :vulkan.shader.screen_quad_vert;
import :vulkan.shader.outline_frag;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct OutlineRenderer {
        struct DescriptorSetLayout : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage>{
            explicit DescriptorSetLayout(const vk::raii::Device &device [[clang::lifetimebound]])
                : vku::DescriptorSetLayout<vk::DescriptorType::eSampledImage> {
                    device,
                    vk::DescriptorSetLayoutCreateInfo {
                        {},
                        vku::unsafeProxy(vk::DescriptorSetLayoutBinding { 0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eFragment }),
                    },
                } { }
        };

        struct PushConstant {
            glm::vec4 outlineColor;
            glm::i32vec2 passthruOffset;
            float outlineThickness;
        };

        DescriptorSetLayout descriptorSetLayout;
        vk::raii::PipelineLayout pipelineLayout;
        vk::raii::Pipeline pipeline;

        OutlineRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]]
        ) : descriptorSetLayout { device },
            pipelineLayout { device, vk::PipelineLayoutCreateInfo{
                {},
                *descriptorSetLayout,
                vku::unsafeProxy(vk::PushConstantRange {
                    vk::ShaderStageFlagBits::eFragment,
                    0, sizeof(PushConstant),
                }),
            } },
            pipeline { device, nullptr, vk::StructureChain {
                vku::getDefaultGraphicsPipelineCreateInfo(
                    createPipelineStages(
                        device,
                        vku::Shader { shader::screen_quad_vert(), vk::ShaderStageFlagBits::eVertex },
                        vku::Shader { shader::outline_frag(), vk::ShaderStageFlagBits::eFragment }).get(),
                    *pipelineLayout,
                    1)
                    .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                        {},
                        false, false,
                        vk::PolygonMode::eFill,
                        vk::CullModeFlagBits::eNone, {},
                        {}, {}, {}, {},
                        1.0f,
                    }))
                    .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
                        {},
                        false, {},
                        vku::unsafeProxy(vk::PipelineColorBlendAttachmentState {
                            true,
                            vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                            vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd,
                            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                        }),
                    })),
                vk::PipelineRenderingCreateInfo {
                    {},
                    vku::unsafeProxy(vk::Format::eB8G8R8A8Srgb),
                },
            }.get() } { }
    };
}