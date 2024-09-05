export module vk_gltf_viewer:vulkan.pipeline.AlphaMaskedFacetedPrimitiveRenderer;

import vku;
export import :vulkan.pl.SceneRendering;
export import :vulkan.shader.FacetedPrimitiveVertex;
export import :vulkan.rp.Scene;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct AlphaMaskedFacetedPrimitiveRenderer : vk::raii::Pipeline {
        AlphaMaskedFacetedPrimitiveRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const pl::SceneRendering &layout [[clang::lifetimebound]],
            const shader::FacetedPrimitiveVertex &vertexShader,
            const rp::Scene &sceneRenderPass [[clang::lifetimebound]]
        ) : Pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
            createPipelineStages(
                device,
                vertexShader,
                vku::Shader { COMPILED_SHADER_DIR "/alpha_masked_faceted_primitive.frag.spv", vk::ShaderStageFlagBits::eFragment }).get(),
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
    };
}