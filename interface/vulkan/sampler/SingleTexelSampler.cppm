export module vk_gltf_viewer:vulkan.sampler.SingleTexelSampler;

export import vulkan_hpp;

namespace vk_gltf_viewer::vulkan::inline sampler {
    export struct SingleTexelSampler : vk::raii::Sampler {
        explicit SingleTexelSampler(
            const vk::raii::Device &device [[clang::lifetimebound]]
        ) : Sampler { device, vk::SamplerCreateInfo {
                {},
                {}, {}, {},
                {}, {}, {},
                {},
                false, {},
                {}, {},
                {}, vk::LodClampNone,
            } } {}
    };
}