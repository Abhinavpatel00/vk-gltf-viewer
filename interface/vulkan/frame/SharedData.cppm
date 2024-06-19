module;

#include <compare>
#include <optional>
#include <vector>

#include <fastgltf/core.hpp>
#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.frame.SharedData;

export import vku;
import :gltf.AssetResources;
import :gltf.SceneResources;
export import :vulkan.Gpu;
export import :vulkan.pipelines.DepthRenderer;
export import :vulkan.pipelines.JumpFloodComputer;
export import :vulkan.pipelines.OutlineRenderer;
export import :vulkan.pipelines.PrimitiveRenderer;
export import :vulkan.pipelines.Rec709Renderer;
export import :vulkan.pipelines.SkyboxRenderer;

struct ImageBasedLightingResources {
	vku::AllocatedImage cubemapImage; vk::raii::ImageView cubemapImageView;
	vku::MappedBuffer cubemapSphericalHarmonicsBuffer;
	vku::AllocatedImage prefilteredmapImage; vk::raii::ImageView prefilteredmapImageView;
};

namespace vk_gltf_viewer::vulkan::inline frame {
    export class SharedData {
    public:
		// CPU resources.
    	const fastgltf::Asset &asset;
		gltf::AssetResources assetResources;
    	gltf::SceneResources sceneResources;

    	// Swapchain.
		vk::raii::SwapchainKHR swapchain;
		vk::Extent2D swapchainExtent;
		std::vector<vk::Image> swapchainImages = swapchain.getImages();

    	// Buffer, image and image views.
    	vku::AllocatedImage brdfmapImage;
    	vk::raii::ImageView brdfmapImageView;
    	std::optional<ImageBasedLightingResources> imageBasedLightingResources = std::nullopt;

    	// Render passes.
    	vk::raii::RenderPass compositionRenderPass;

		// Pipelines.
		pipelines::DepthRenderer depthRenderer;
		pipelines::JumpFloodComputer jumpFloodComputer;
		pipelines::PrimitiveRenderer primitiveRenderer;
		pipelines::SkyboxRenderer skyboxRenderer;
    	pipelines::Rec709Renderer rec709Renderer;
		pipelines::OutlineRenderer outlineRenderer;

    	// Attachment groups.
    	std::vector<vku::AttachmentGroup> swapchainAttachmentGroups;

    	// Descriptor/command pools.
    	vk::raii::CommandPool graphicsCommandPool, transferCommandPool;

    	SharedData(const fastgltf::Asset &asset, const std::filesystem::path &assetDir, const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &swapchainExtent, const shaderc::Compiler &compiler = {});

    	auto handleSwapchainResize(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto createSwapchain(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &extent, vk::SwapchainKHR oldSwapchain = {}) const -> decltype(swapchain);
    	[[nodiscard]] auto createCompositionRenderPass(const vk::raii::Device &device) const -> decltype(compositionRenderPass);
    	[[nodiscard]] auto createSwapchainAttachmentGroups(const vk::raii::Device &device) const -> decltype(swapchainAttachmentGroups);

    	auto generateAssetResourceMipmaps(vk::CommandBuffer commandBuffer) const -> void;
    	auto initAttachmentLayouts(vk::CommandBuffer commandBuffer) const -> void;
    };
}
