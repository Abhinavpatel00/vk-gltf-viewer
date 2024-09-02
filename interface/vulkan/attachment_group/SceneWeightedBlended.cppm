export module vk_gltf_viewer:vulkan.ag.SceneWeightedBlended;

export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct SceneWeightedBlended final : vku::MsaaAttachmentGroup {
        SceneWeightedBlended(
            const Gpu &gpu [[clang::lifetimebound]],
            const vk::Extent2D &extent,
            const vku::Image &depthImage
        ) : MsaaAttachmentGroup { extent, vk::SampleCountFlagBits::e4 } {
            addColorAttachment(
                gpu.device,
                storeImage(createColorImage(gpu.allocator, vk::Format::eR16G16B16A16Sfloat)),
                storeImage(createResolveImage(
                    gpu.allocator, vk::Format::eR16G16B16A16Sfloat,
                    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eTransientAttachment)));
            addColorAttachment(
                gpu.device,
                storeImage(createColorImage(gpu.allocator, vk::Format::eR16Unorm)),
                storeImage(createResolveImage(
                    gpu.allocator, vk::Format::eR16Unorm,
                    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eTransientAttachment)));
            setDepthStencilAttachment(gpu.device, depthImage);
        }
    };
}