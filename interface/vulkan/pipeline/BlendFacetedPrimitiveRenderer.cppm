export module vk_gltf_viewer:vulkan.pipeline.BlendFacetedPrimitiveRenderer;

import vku;
export import :vulkan.pipeline.tag;
export import :vulkan.pl.Primitive;
export import :vulkan.rp.Scene;
import :vulkan.shader.blend_faceted_primitive_frag;
import :vulkan.shader.blend_primitive_frag;
import :vulkan.shader.faceted_primitive_tesc;
import :vulkan.shader.faceted_primitive_tese;
import :vulkan.shader.faceted_primitive_vert;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct BlendFacetedPrimitiveRenderer : vk::raii::Pipeline {
        BlendFacetedPrimitiveRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const pl::Primitive &layout [[clang::lifetimebound]],
            const rp::Scene &sceneRenderPass [[clang::lifetimebound]]
        ) : Pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader { shader::faceted_primitive_vert(), vk::ShaderStageFlagBits::eVertex },
                    vku::Shader { shader::blend_faceted_primitive_frag(), vk::ShaderStageFlagBits::eFragment }).get(),
                *layout, 1, true, vk::SampleCountFlagBits::e4)
            .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                {},
                false, false,
                vk::PolygonMode::eFill,
                // Translucent objects' back faces shouldn't be culled.
                vk::CullModeFlagBits::eNone, {},
                false, {}, {}, {},
                1.f,
            }))
            .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                {},
                // Translucent objects shouldn't interfere with the pre-rendered depth buffer. Use reverse Z.
                true, false, vk::CompareOp::eGreater,
            }))
            .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
                {},
                false, {},
                vku::unsafeProxy({
                    vk::PipelineColorBlendAttachmentState {
                        true,
                        vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                        vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                    },
                    vk::PipelineColorBlendAttachmentState {
                        true,
                        vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcColor, vk::BlendOp::eAdd,
                        vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                        vk::ColorComponentFlagBits::eR,
                    },
                }),
                { 1.f, 1.f, 1.f, 1.f },
            }))
            .setRenderPass(*sceneRenderPass)
            .setSubpass(1)
        } { }

        /**
         * Construct the pipeline with tessellation shader based TBN matrix generation support.
         * @param device
         * @param layout
         * @param sceneRenderPass
         */
        BlendFacetedPrimitiveRenderer(
            use_tessellation_t,
            const vk::raii::Device &device [[clang::lifetimebound]],
            const pl::Primitive &layout [[clang::lifetimebound]],
            const rp::Scene &sceneRenderPass [[clang::lifetimebound]]
        ) : Pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader { shader::faceted_primitive_vert(), vk::ShaderStageFlagBits::eVertex },
                    vku::Shader { shader::faceted_primitive_tesc(), vk::ShaderStageFlagBits::eTessellationControl },
                    vku::Shader { shader::faceted_primitive_tese(), vk::ShaderStageFlagBits::eTessellationEvaluation },
                    vku::Shader { shader::blend_primitive_frag(), vk::ShaderStageFlagBits::eFragment }).get(),
                *layout, 1, true, vk::SampleCountFlagBits::e4)
            .setPTessellationState(vku::unsafeAddress(vk::PipelineTessellationStateCreateInfo {
                {},
                3,
            }))
            .setPInputAssemblyState(vku::unsafeAddress(vk::PipelineInputAssemblyStateCreateInfo {
                {},
                vk::PrimitiveTopology::ePatchList,
            }))
            .setPRasterizationState(vku::unsafeAddress(vk::PipelineRasterizationStateCreateInfo {
                {},
                false, false,
                vk::PolygonMode::eFill,
                // Translucent objects' back faces shouldn't be culled.
                vk::CullModeFlagBits::eNone, {},
                false, {}, {}, {},
                1.f,
            }))
            .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                {},
                // Translucent objects shouldn't interfere with the pre-rendered depth buffer. Use reverse Z.
                true, false, vk::CompareOp::eGreater,
            }))
            .setPColorBlendState(vku::unsafeAddress(vk::PipelineColorBlendStateCreateInfo {
                {},
                false, {},
                vku::unsafeProxy({
                    vk::PipelineColorBlendAttachmentState {
                        true,
                        vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                        vk::BlendFactor::eOne, vk::BlendFactor::eOne, vk::BlendOp::eAdd,
                        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
                    },
                    vk::PipelineColorBlendAttachmentState {
                        true,
                        vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcColor, vk::BlendOp::eAdd,
                        vk::BlendFactor::eZero, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd,
                        vk::ColorComponentFlagBits::eR,
                    },
                }),
                { 1.f, 1.f, 1.f, 1.f },
            }))
            .setRenderPass(*sceneRenderPass)
            .setSubpass(1)
        } { }
    };
}