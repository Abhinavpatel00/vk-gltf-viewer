export module vk_gltf_viewer:vulkan.pipeline.MaskFacetedPrimitiveRenderer;

import vku;
export import :vulkan.pipeline.tag;
export import :vulkan.pl.Primitive;
export import :vulkan.rp.Scene;
import :vulkan.shader.faceted_primitive_tesc;
import :vulkan.shader.faceted_primitive_tese;
import :vulkan.shader.faceted_primitive_vert;
import :vulkan.shader.mask_faceted_primitive_frag;
import :vulkan.shader.mask_primitive_frag;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct MaskFacetedPrimitiveRenderer : vk::raii::Pipeline {
        MaskFacetedPrimitiveRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const pl::Primitive &layout [[clang::lifetimebound]],
            const rp::Scene &sceneRenderPass [[clang::lifetimebound]]
        ) : Pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vku::Shader { shader::faceted_primitive_vert(), vk::ShaderStageFlagBits::eVertex },
                vku::Shader { shader::mask_faceted_primitive_frag(), vk::ShaderStageFlagBits::eFragment }).get(),
            *layout, 1, true, vk::SampleCountFlagBits::e4)
            .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                {},
                true, true, vk::CompareOp::eGreater, // Use reverse Z.
            }))
            .setPMultisampleState(vku::unsafeAddress(vk::PipelineMultisampleStateCreateInfo {
                {},
                vk::SampleCountFlagBits::e4,
                {}, {}, {},
                true,
            }))
            .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                {},
                vku::unsafeProxy({
                    vk::DynamicState::eViewport,
                    vk::DynamicState::eScissor,
                    vk::DynamicState::eCullMode,
                }),
            }))
            .setRenderPass(*sceneRenderPass)
            .setSubpass(0)
        } { }

        /**
         * Construct the pipeline with tessellation shader based TBN matrix generation support.
         * @param device
         * @param layout
         * @param sceneRenderPass
         */
        MaskFacetedPrimitiveRenderer(
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
                    vku::Shader { shader::mask_primitive_frag(), vk::ShaderStageFlagBits::eFragment }).get(),
                *layout, 1, true, vk::SampleCountFlagBits::e4)
            .setPTessellationState(vku::unsafeAddress(vk::PipelineTessellationStateCreateInfo {
                {},
                3,
            }))
            .setPInputAssemblyState(vku::unsafeAddress(vk::PipelineInputAssemblyStateCreateInfo {
                {},
                vk::PrimitiveTopology::ePatchList,
            }))
            .setPDepthStencilState(vku::unsafeAddress(vk::PipelineDepthStencilStateCreateInfo {
                {},
                true, true, vk::CompareOp::eGreater, // Use reverse Z.
            }))
            .setPMultisampleState(vku::unsafeAddress(vk::PipelineMultisampleStateCreateInfo {
                {},
                vk::SampleCountFlagBits::e4,
                {}, {}, {},
                true,
            }))
            .setPDynamicState(vku::unsafeAddress(vk::PipelineDynamicStateCreateInfo {
                {},
                vku::unsafeProxy({
                    vk::DynamicState::eViewport,
                    vk::DynamicState::eScissor,
                    vk::DynamicState::eCullMode,
                }),
            }))
            .setRenderPass(*sceneRenderPass)
            .setSubpass(0)
        } { }
    };
}