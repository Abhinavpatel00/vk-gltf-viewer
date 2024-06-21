module;

#include <optional>

#include <imgui_internal.h>

export module vk_gltf_viewer:GlobalState;

import :control.Camera;

namespace vk_gltf_viewer {
    export class GlobalState {
    public:
        control::Camera camera;
        glm::uvec2 framebufferCursorPosition;
        std::optional<std::uint32_t> hoveringNodeIndex, selectedNodeIndex;
        ImRect imGuiPassthruRect;

        [[nodiscard]] static auto getInstance() noexcept -> GlobalState&;

    private:
        GlobalState() noexcept;
    };
}