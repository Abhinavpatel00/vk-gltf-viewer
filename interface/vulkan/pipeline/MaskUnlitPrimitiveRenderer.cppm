export module vk_gltf_viewer:vulkan.pipeline.MaskUnlitPrimitiveRenderer;

import vku;
export import :vulkan.pl.Primitive;
export import :vulkan.rp.Scene;
import :vulkan.shader.unlit_primitive_vert;
import :vulkan.shader.mask_unlit_primitive_frag;

namespace vk_gltf_viewer::vulkan::inline pipeline {
    export struct MaskUnlitPrimitiveRenderer : vk::raii::Pipeline {
        MaskUnlitPrimitiveRenderer(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const pl::Primitive &layout [[clang::lifetimebound]],
            const rp::Scene &sceneRenderPass [[clang::lifetimebound]]
        ) : Pipeline { device, nullptr, vku::getDefaultGraphicsPipelineCreateInfo(
                createPipelineStages(
                    device,
                    vku::Shader { shader::unlit_primitive_vert(), vk::ShaderStageFlagBits::eVertex },
                    vku::Shader { shader::mask_unlit_primitive_frag(), vk::ShaderStageFlagBits::eFragment }).get(),
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