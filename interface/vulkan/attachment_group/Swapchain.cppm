export module vk_gltf_viewer:vulkan.ag.Swapchain;

export import vku;
export import :vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::ag {
    export struct Swapchain final : vku::AttachmentGroup {
        Swapchain(
            const vk::raii::Device &device [[clang::lifetimebound]],
            const vku::Image &swapchainImage
        ) : AttachmentGroup { vku::toExtent2D(swapchainImage.extent) } {
            addColorAttachment(device, swapchainImage);
        }
    };
}