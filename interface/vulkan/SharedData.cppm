export module vk_gltf_viewer:vulkan.SharedData;

import std;
export import vku;
export import :vulkan.ag.ImGuiSwapchain;
export import :vulkan.ag.Swapchain;
export import :vulkan.Gpu;
export import :vulkan.pipeline.AlphaMaskedDepthRenderer;
export import :vulkan.pipeline.AlphaMaskedFacetedPrimitiveRenderer;
export import :vulkan.pipeline.AlphaMaskedJumpFloodSeedRenderer;
export import :vulkan.pipeline.AlphaMaskedPrimitiveRenderer;
export import :vulkan.pipeline.DepthRenderer;
export import :vulkan.pipeline.FacetedPrimitiveRenderer;
export import :vulkan.pipeline.JumpFloodComputer;
export import :vulkan.pipeline.JumpFloodSeedRenderer;
export import :vulkan.pipeline.OutlineRenderer;
export import :vulkan.pipeline.PrimitiveRenderer;
export import :vulkan.pipeline.SkyboxRenderer;
export import :vulkan.rp.Scene;
import :vulkan.sampler.SingleTexelSampler;

namespace vk_gltf_viewer::vulkan {
    export class SharedData {
		const Gpu &gpu;

    public:
    	// Swapchain.
		vk::raii::SwapchainKHR swapchain;
		vk::Extent2D swapchainExtent;
		std::vector<vk::Image> swapchainImages = swapchain.getImages();

    	// Buffer, image and image views and samplers.
    	buffer::CubeIndices cubeIndices { gpu.allocator };
    	CubemapSampler cubemapSampler { gpu.device };
    	BrdfLutSampler brdfLutSampler { gpu.device };
		SingleTexelSampler singleTexelSampler { gpu.device };

    	// Descriptor set layouts.
    	dsl::Asset assetDescriptorSetLayout { gpu.device, 32 }; // TODO: set proper initial texture count.
    	dsl::ImageBasedLighting imageBasedLightingDescriptorSetLayout { gpu.device, cubemapSampler, brdfLutSampler };
    	dsl::Scene sceneDescriptorSetLayout { gpu.device };
    	dsl::Skybox skyboxDescriptorSetLayout { gpu.device, cubemapSampler };

    	// Render passes.
    	rp::Scene sceneRenderPass { gpu.device };

    	// Pipeline layouts.
    	pl::SceneRendering sceneRenderingPipelineLayout { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout, sceneDescriptorSetLayout) };

		// Pipelines.
		AlphaMaskedDepthRenderer alphaMaskedDepthRenderer { gpu.device, std::tie(sceneDescriptorSetLayout, assetDescriptorSetLayout) };
    	AlphaMaskedFacetedPrimitiveRenderer alphaMaskedFacetedPrimitiveRenderer { gpu.device, sceneRenderingPipelineLayout, sceneRenderPass };
    	AlphaMaskedJumpFloodSeedRenderer alphaMaskedJumpFloodSeedRenderer { gpu.device, std::tie(sceneDescriptorSetLayout, assetDescriptorSetLayout) };
    	AlphaMaskedPrimitiveRenderer alphaMaskedPrimitiveRenderer { gpu.device, sceneRenderingPipelineLayout, sceneRenderPass };
		DepthRenderer depthRenderer { gpu.device, sceneDescriptorSetLayout };
		FacetedPrimitiveRenderer facetedPrimitiveRenderer { gpu.device, sceneRenderingPipelineLayout, sceneRenderPass };
		JumpFloodComputer jumpFloodComputer { gpu.device };
    	JumpFloodSeedRenderer jumpFloodSeedRenderer { gpu.device, sceneDescriptorSetLayout };
		OutlineRenderer outlineRenderer { gpu.device };
		PrimitiveRenderer primitiveRenderer { gpu.device, sceneRenderingPipelineLayout, sceneRenderPass };
		SkyboxRenderer skyboxRenderer { gpu.device, skyboxDescriptorSetLayout, sceneRenderPass, cubeIndices };

    	// Attachment groups.
    	ag::Swapchain swapchainAttachmentGroup { gpu.device, swapchainExtent, swapchainImages };
    	ag::ImGuiSwapchain imGuiSwapchainAttachmentGroup { gpu.device, swapchainExtent, swapchainImages };

    	// Descriptor pools.
    	vk::raii::DescriptorPool textureDescriptorPool = createTextureDescriptorPool();
    	vk::raii::DescriptorPool descriptorPool = createDescriptorPool();

    	// Descriptor sets.
    	vku::DescriptorSet<dsl::Asset> assetDescriptorSet;
    	vku::DescriptorSet<dsl::Scene> sceneDescriptorSet;
    	vku::DescriptorSet<dsl::ImageBasedLighting> imageBasedLightingDescriptorSet;
    	vku::DescriptorSet<dsl::Skybox> skyboxDescriptorSet;

    	SharedData(const Gpu &gpu [[clang::lifetimebound]], vk::SurfaceKHR surface, const vk::Extent2D &swapchainExtent)
    		: gpu { gpu }
			, swapchain { createSwapchain(surface, swapchainExtent) }
			, swapchainExtent { swapchainExtent } {
    		std::tie(assetDescriptorSet)
				= vku::allocateDescriptorSets(*gpu.device, *textureDescriptorPool, std::tie(
					assetDescriptorSetLayout));
    		std::tie(sceneDescriptorSet, imageBasedLightingDescriptorSet, skyboxDescriptorSet)
				= vku::allocateDescriptorSets(*gpu.device, *descriptorPool, std::tie(
					sceneDescriptorSetLayout,
					imageBasedLightingDescriptorSetLayout,
					skyboxDescriptorSetLayout));
    	}

    	// --------------------
    	// The below public methods will modify the GPU resources, therefore they MUST be called before the command buffer
    	// submission.
    	// --------------------

    	auto handleSwapchainResize(vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void {
    		swapchain = createSwapchain(surface, newExtent, *swapchain);
    		swapchainExtent = newExtent;
    		swapchainImages = swapchain.getImages();

    		swapchainAttachmentGroup = { gpu.device, swapchainExtent, swapchainImages };
    		imGuiSwapchainAttachmentGroup = { gpu.device, swapchainExtent, swapchainImages };
    	}

    	auto updateTextureCount(std::uint32_t textureCount) -> void {
    		assetDescriptorSetLayout = { gpu.device, textureCount };
    		sceneRenderingPipelineLayout = { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout, sceneDescriptorSetLayout) };

    		// Following pipelines are dependent to the assetDescriptorSetLayout or sceneRenderingPipelineLayout.
    		alphaMaskedDepthRenderer = { gpu.device, std::tie(sceneDescriptorSetLayout, assetDescriptorSetLayout) };
			alphaMaskedFacetedPrimitiveRenderer = { gpu.device, sceneRenderingPipelineLayout, sceneRenderPass };
			alphaMaskedJumpFloodSeedRenderer = { gpu.device, std::tie(sceneDescriptorSetLayout, assetDescriptorSetLayout) };
			alphaMaskedPrimitiveRenderer = { gpu.device, sceneRenderingPipelineLayout, sceneRenderPass };
			facetedPrimitiveRenderer = { gpu.device, sceneRenderingPipelineLayout, sceneRenderPass };
			primitiveRenderer = { gpu.device, sceneRenderingPipelineLayout, sceneRenderPass };

    		textureDescriptorPool = createTextureDescriptorPool();
    		std::tie(assetDescriptorSet) = vku::allocateDescriptorSets(*gpu.device, *textureDescriptorPool, std::tie(
    			assetDescriptorSetLayout));
    	}

    private:
    	[[nodiscard]] auto createSwapchain(vk::SurfaceKHR surface, const vk::Extent2D &extent, vk::SwapchainKHR oldSwapchain = {}) const -> decltype(swapchain) {
			const vk::SurfaceCapabilitiesKHR surfaceCapabilities = gpu.physicalDevice.getSurfaceCapabilitiesKHR(surface);
			return { gpu.device, vk::StructureChain {
				vk::SwapchainCreateInfoKHR{
					vk::SwapchainCreateFlagBitsKHR::eMutableFormat,
					surface,
					std::min(surfaceCapabilities.minImageCount + 1, surfaceCapabilities.maxImageCount),
					vk::Format::eB8G8R8A8Srgb,
					vk::ColorSpaceKHR::eSrgbNonlinear,
					extent,
					1,
					vk::ImageUsageFlagBits::eColorAttachment,
					{}, {},
					surfaceCapabilities.currentTransform,
					vk::CompositeAlphaFlagBitsKHR::eOpaque,
					vk::PresentModeKHR::eFifo,
					true,
					oldSwapchain,
				},
				vk::ImageFormatListCreateInfo {
					vku::unsafeProxy({
						vk::Format::eB8G8R8A8Srgb,
						vk::Format::eB8G8R8A8Unorm,
					}),
				},
			}.get() };
		}

    	[[nodiscard]] auto createTextureDescriptorPool() const -> vk::raii::DescriptorPool {
    		return { gpu.device, getPoolSizes(assetDescriptorSetLayout).getDescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind) };
    	}

    	[[nodiscard]] auto createDescriptorPool() const -> vk::raii::DescriptorPool {
    		return { gpu.device, getPoolSizes(imageBasedLightingDescriptorSetLayout, sceneDescriptorSetLayout, skyboxDescriptorSetLayout).getDescriptorPoolCreateInfo() };
    	}
    };
}
