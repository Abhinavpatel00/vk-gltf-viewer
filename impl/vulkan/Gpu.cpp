module;

#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.Gpu;

import std;
import vku;

vk_gltf_viewer::vulkan::Gpu::QueueFamilies::QueueFamilies(
	vk::PhysicalDevice physicalDevice,
	vk::SurfaceKHR surface
) {
	const std::vector queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
	compute = vku::getComputeSpecializedQueueFamily(queueFamilyProperties)
		.or_else([&] { return vku::getComputeQueueFamily(queueFamilyProperties); })
		.value();
	graphicsPresent = vku::getGraphicsPresentQueueFamily(physicalDevice, surface, queueFamilyProperties).value();
	transfer = vku::getTransferQueueFamily(queueFamilyProperties).value_or(compute);
}

auto vk_gltf_viewer::vulkan::Gpu::QueueFamilies::getUniqueIndices() const noexcept -> std::vector<std::uint32_t> {
	std::vector indices { compute, graphicsPresent, transfer };
	std::ranges::sort(indices);

	const auto ret = std::ranges::unique(indices);
	indices.erase(ret.begin(), ret.end());

	return indices;
}

vk_gltf_viewer::vulkan::Gpu::Queues::Queues(
	vk::Device device,
	const QueueFamilies& queueFamilies
) noexcept : compute { device.getQueue(queueFamilies.compute, 0) },
			 graphicsPresent{ device.getQueue(queueFamilies.graphicsPresent, 0) },
	         transfer { device.getQueue(queueFamilies.transfer, 0) } { }

vk_gltf_viewer::vulkan::Gpu::Gpu(
    const vk::raii::Instance &instance,
    vk::SurfaceKHR surface
) : physicalDevice { selectPhysicalDevice(instance, surface) },
	queueFamilies { *physicalDevice, surface },
	allocator { createAllocator(instance) } { }

vk_gltf_viewer::vulkan::Gpu::~Gpu() {
	allocator.destroy();
}

auto vk_gltf_viewer::vulkan::Gpu::selectPhysicalDevice(
	const vk::raii::Instance &instance,
	vk::SurfaceKHR surface
) const -> decltype(physicalDevice) {
    auto adequatePhysicalDevices
        = instance.enumeratePhysicalDevices()
        | std::views::filter([&](const vk::raii::PhysicalDevice &physicalDevice) {
            try {
                std::ignore = QueueFamilies { physicalDevice, surface };
                return true;
            }
            catch (const std::runtime_error&) {
                return false;
            }
        });
    if (adequatePhysicalDevices.empty()) {
        throw std::runtime_error { "No suitable GPU for the application" };
    }

    return *std::ranges::max_element(adequatePhysicalDevices, {}, [&](const vk::raii::PhysicalDevice &physicalDevice) {
        std::uint32_t score = 0;

        const vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
        if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            score += 1000;
        }

        score += properties.limits.maxImageDimension2D;

        return score;
    });
}

auto vk_gltf_viewer::vulkan::Gpu::createDevice() const -> decltype(device) {
	constexpr std::array queuePriorities{ 1.0f };
	vk::raii::Device device { physicalDevice, vk::StructureChain {
		vk::DeviceCreateInfo{
			{},
			vku::unsafeProxy(queueFamilies.getUniqueIndices()
				| std::views::transform([&](std::uint32_t queueFamilyIndex) {
					return vk::DeviceQueueCreateInfo{
						{},
						queueFamilyIndex,
						queuePriorities,
					};
				})
				| std::ranges::to<std::vector>()),
			{},
			vku::unsafeProxy({
#if __APPLE__
				vk::KHRPortabilitySubsetExtensionName,
#endif
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
				vk::KHRDynamicRenderingExtensionName,
				vk::KHRSynchronization2ExtensionName,
				vk::EXTExtendedDynamicStateExtensionName,
				vk::KHRTimelineSemaphoreExtensionName,
#pragma clang diagnostic pop
				vk::KHRSwapchainExtensionName,
				vk::KHRSwapchainMutableFormatExtensionName, // For ImGui gamma correction.
			}),
			vku::unsafeAddress(vk::PhysicalDeviceFeatures{}
				.setSamplerAnisotropy(true)
				.setShaderInt64(true)
				.setMultiDrawIndirect(true)
				.setDepthBiasClamp(true)
				.setShaderStorageImageWriteWithoutFormat(true)),
		},
		vk::PhysicalDeviceVulkan11Features{}
			.setShaderDrawParameters(true)
			.setStorageBuffer16BitAccess(true)
			.setUniformAndStorageBuffer16BitAccess(true),
		vk::PhysicalDeviceVulkan12Features{}
			.setBufferDeviceAddress(true)
			.setDescriptorIndexing(true)
			.setDescriptorBindingSampledImageUpdateAfterBind(true)
			.setDescriptorBindingStorageImageUpdateAfterBind(true)
			.setImagelessFramebuffer(true)
			.setRuntimeDescriptorArray(true)
			.setStorageBuffer8BitAccess(true)
			.setUniformAndStorageBuffer8BitAccess(true)
            .setStoragePushConstant8(true)
			.setScalarBlockLayout(true)
			.setTimelineSemaphore(true),
		vk::PhysicalDeviceDynamicRenderingFeatures { true },
		vk::PhysicalDeviceSynchronization2Features { true },
		vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT { true },
	}.get() };
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
	return device;
}

auto vk_gltf_viewer::vulkan::Gpu::createAllocator(
	const vk::raii::Instance &instance
) const -> decltype(allocator) {
	return vma::createAllocator(vma::AllocatorCreateInfo{
		vma::AllocatorCreateFlagBits::eBufferDeviceAddress,
		*physicalDevice,
		*device,
		{}, {}, {}, {},
		vku::unsafeAddress(vma::VulkanFunctions{
			instance.getDispatcher()->vkGetInstanceProcAddr,
			device.getDispatcher()->vkGetDeviceProcAddr,
		}),
		*instance,
		vk::makeApiVersion(0, 1, 2, 0),
	});
}