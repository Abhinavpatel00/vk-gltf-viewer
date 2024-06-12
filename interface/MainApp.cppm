module;

#include <compare>

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:MainApp;

export import vulkan_hpp; // have to be exported for initializing DispatchLoader.
import :control.AppWindow;
import :GlobalState;
import :vulkan.Gpu;

namespace vk_gltf_viewer {
	export class MainApp {
	public:
	    GlobalState &globalState = GlobalState::getInstance();

		fastgltf::GltfDataBuffer gltfDataBuffer{};
		fastgltf::Expected<fastgltf::Asset> assetExpected = loadAsset(std::getenv("GLTF_PATH"));

		vk::raii::Context context;
		vk::raii::Instance instance = createInstance();
		control::AppWindow window { instance, globalState };
		vulkan::Gpu gpu { instance, *window.surface };

		auto run() -> void;

	private:
    	[[nodiscard]] auto loadAsset(const std::filesystem::path &path) -> decltype(assetExpected);

		[[nodiscard]] auto createInstance() const -> decltype(instance);
	};
}