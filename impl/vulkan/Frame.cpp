module;

#include <boost/container/static_vector.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :vulkan.Frame;

import std;
import imgui.vulkan;
import :helpers.concepts;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.ranges;
import :vulkan.ag.DepthPrepass;

constexpr auto NO_INDEX = std::numeric_limits<std::uint16_t>::max();

vk_gltf_viewer::vulkan::Frame::Frame(const Gpu &gpu, const SharedData &sharedData)
    : gpu { gpu }
    , hoveringNodeIndexBuffer { gpu.allocator, NO_INDEX, vk::BufferUsageFlagBits::eTransferDst, vku::allocation::hostRead }
    , sharedData { sharedData } {
    // Change initial attachment layouts.
    const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
    vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
        recordSwapchainExtentDependentImageLayoutTransitionCommands(cb);
    }, *fence);
    std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

    // Allocate descriptor sets.
    std::tie(hoveringNodeJumpFloodSet, selectedNodeJumpFloodSet, hoveringNodeOutlineSet, selectedNodeOutlineSet, weightedBlendedCompositionSet)
        = allocateDescriptorSets(*gpu.device, *descriptorPool, std::tie(
            sharedData.jumpFloodComputer.descriptorSetLayout,
            sharedData.jumpFloodComputer.descriptorSetLayout,
            sharedData.outlineRenderer.descriptorSetLayout,
            sharedData.outlineRenderer.descriptorSetLayout,
            sharedData.weightedBlendedCompositionRenderer.descriptorSetLayout));

    // Update descriptor set.
    gpu.device.updateDescriptorSets(
        weightedBlendedCompositionSet.getWrite<0>(vku::unsafeProxy({
            vk::DescriptorImageInfo { {}, *sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).resolveView, vk::ImageLayout::eShaderReadOnlyOptimal },
            vk::DescriptorImageInfo { {}, *sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).resolveView, vk::ImageLayout::eShaderReadOnlyOptimal },
        })),
        {});

    // Allocate per-frame command buffers.
    std::tie(jumpFloodCommandBuffer) = vku::allocateCommandBuffers<1>(*gpu.device, *computeCommandPool);
    std::tie(scenePrepassCommandBuffer, sceneRenderingCommandBuffer, compositionCommandBuffer)
        = vku::allocateCommandBuffers<3>(*gpu.device, *graphicsCommandPool);
}

auto vk_gltf_viewer::vulkan::Frame::update(const ExecutionTask &task) -> UpdateResult {
    UpdateResult result{};

    // --------------------
    // Update CPU resources.
    // --------------------

    if (task.handleSwapchainResize) {
        // Attachment images that have to be matched to the swapchain extent must be recreated.
        sceneOpaqueAttachmentGroup = { gpu, sharedData.swapchainExtent, sharedData.swapchainImages };
        sceneWeightedBlendedAttachmentGroup = { gpu, sharedData.swapchainExtent, sceneOpaqueAttachmentGroup.depthStencilAttachment->image };
        framebuffers = createFramebuffers();

        gpu.device.updateDescriptorSets(
            weightedBlendedCompositionSet.getWrite<0>(vku::unsafeProxy({
                vk::DescriptorImageInfo { {}, *sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).resolveView, vk::ImageLayout::eShaderReadOnlyOptimal },
                vk::DescriptorImageInfo { {}, *sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).resolveView, vk::ImageLayout::eShaderReadOnlyOptimal },
            })),
            {});

        // Change initial attachment layouts.
        // TODO: can this operation be non-blocking?
        const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
        vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
            recordSwapchainExtentDependentImageLayoutTransitionCommands(cb);
        }, *fence);
        std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling
    }

    // Get node index under the cursor from hoveringNodeIndexBuffer.
    // If it is not NO_INDEX (i.e. node index is found), update hoveringNodeIndex.
    if (auto value = std::exchange(hoveringNodeIndexBuffer.asValue<std::uint16_t>(), NO_INDEX); value != NO_INDEX) {
        result.hoveringNodeIndex = value;
    }

    // If passthru extent is different from the current's, dependent images have to be recreated.
    if (!passthruResources || passthruResources->extent != task.passthruRect.extent) {
        // TODO: can this operation be non-blocking?
        const vk::raii::Fence fence { gpu.device, vk::FenceCreateInfo{} };
        vku::executeSingleCommand(*gpu.device, *graphicsCommandPool, gpu.queues.graphicsPresent, [&](vk::CommandBuffer cb) {
            passthruResources.emplace(gpu, task.passthruRect.extent, cb);
        }, *fence);
        std::ignore = gpu.device.waitForFences(*fence, true, ~0ULL); // TODO: failure handling

        gpu.device.updateDescriptorSets({
            hoveringNodeJumpFloodSet.getWriteOne<0>({ {}, *passthruResources->hoveringNodeOutlineJumpFloodResources.imageView, vk::ImageLayout::eGeneral }),
            selectedNodeJumpFloodSet.getWriteOne<0>({ {}, *passthruResources->selectedNodeOutlineJumpFloodResources.imageView, vk::ImageLayout::eGeneral }),
        }, {});
    }

    projectionViewMatrix = task.camera.projection * task.camera.view;
    viewPosition = inverse(task.camera.view)[3];
    translationlessProjectionViewMatrix = task.camera.projection * glm::mat4 { glm::mat3 { task.camera.view } };
    passthruRect = task.passthruRect;
    cursorPosFromPassthruRectTopLeft = task.cursorPosFromPassthruRectTopLeft;

    // If there is a glTF scene to be rendered, related resources have to be updated.
    if (task.gltf) {
        indexBuffers
            = task.gltf->assetGpuBuffers.indexBuffers
            | ranges::views::value_transform([](vk::Buffer buffer) { return buffer; })
            | std::ranges::to<std::unordered_map>();

        const auto criteriaGetter = [&](const gltf::AssetPrimitiveInfo &primitiveInfo) {
            CommandSeparationCriteria result {
                .strategy = primitiveInfo.normalInfo.has_value() ? RenderingStrategy::Opaque : RenderingStrategy::OpaqueFaceted,
                .indexType = primitiveInfo.indexInfo.transform([](const auto &info) { return info.type; }),
                .doubleSided = false,
            };
            if (primitiveInfo.materialIndex) {
                const fastgltf::Material &material = task.gltf->asset.materials[*primitiveInfo.materialIndex];
                switch (material.alphaMode) {
                case fastgltf::AlphaMode::Opaque:
                    if (material.unlit) {
                        result.strategy = RenderingStrategy::OpaqueUnlit;
                    }
                    break;
                case fastgltf::AlphaMode::Mask:
                    if (material.unlit) {
                        result.strategy = RenderingStrategy::MaskUnlit;
                    }
                    else {
                        result.strategy = primitiveInfo.normalInfo.has_value() ? RenderingStrategy::Mask : RenderingStrategy::MaskFaceted;
                    }
                    break;
                case fastgltf::AlphaMode::Blend:
                    if (material.unlit) {
                        result.strategy = RenderingStrategy::BlendUnlit;
                    }
                    else {
                        result.strategy = primitiveInfo.normalInfo.has_value() ? RenderingStrategy::Blend : RenderingStrategy::BlendFaceted;
                    }
                    break;
                }

                result.doubleSided = material.doubleSided;
            }
            return result;
        };

        if (!task.gltf->renderingNodes.indices.empty()) {
            if (!renderingNodes ||
                task.gltf->renderingNodes.shouldRegenerateDrawCommands ||
                (renderingNodes && renderingNodes->indices != task.gltf->renderingNodes.indices)) {
                renderingNodes.emplace(
                    task.gltf->renderingNodes.indices,
                    task.gltf->sceneGpuBuffers.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, task.gltf->renderingNodes.indices, [&](const fastgltf::Primitive &primitive) -> decltype(auto) { return task.gltf->assetGpuBuffers.primitiveInfos.at(&primitive); }));
            }

            if (task.frustum) {
                for (auto &buffer : renderingNodes->indirectDrawCommandBuffers | std::views::values) {
                    visit([&]<bool Indexed>(buffer::IndirectDrawCommands<Indexed> &indirectDrawCommands) -> void {
                        indirectDrawCommands.partition([&](const buffer::IndirectDrawCommands<Indexed>::command_t &command) {
                            if (command.instanceCount > 1) {
                                // Do not perform frustum culling for instanced mesh.
                                return true;
                            }

                            const std::uint16_t nodeIndex = command.firstInstance >> 16U;
                            const std::uint16_t primitiveIndex = command.firstInstance & 0xFFFFU;
                            const fastgltf::Primitive &primitive = task.gltf->assetGpuBuffers.getPrimitiveByOrder(primitiveIndex);

                            const gltf::AssetPrimitiveInfo &primitiveInfo = task.gltf->assetGpuBuffers.primitiveInfos.at(&primitive);

                            const glm::mat4 nodeWorldTransform = glm::make_mat4(task.gltf->sceneHierarchy.nodeWorldTransforms[nodeIndex].data());
                            const glm::vec3 transformedMin { nodeWorldTransform * glm::vec4 { primitiveInfo.min, 1.f } };
                            const glm::vec3 transformedMax { nodeWorldTransform * glm::vec4 { primitiveInfo.max, 1.f } };

                            const glm::vec3 halfDisplacement = (transformedMax - transformedMin) / 2.f;
                            const glm::vec3 center = transformedMin + halfDisplacement;
                            const float radius = length(halfDisplacement);

                            return task.frustum->isOverlapApprox(center, radius);
                        });
                    }, buffer);
                }
            }
            else {
                for (auto &buffer : renderingNodes->indirectDrawCommandBuffers | std::views::values) {
                    visit([&]<bool Indexed>(buffer::IndirectDrawCommands<Indexed> &indirectDrawCommands) {
                        indirectDrawCommands.resetDrawCount();
                    }, buffer);
                }
            }
        }
        else {
            renderingNodes.reset();
        }

        if (task.gltf->selectedNodes) {
            if (selectedNodes) {
                if (task.gltf->selectedNodes->shouldRegenerateDrawCommands || selectedNodes->indices != task.gltf->selectedNodes->indices) {
                    selectedNodes->indices = task.gltf->selectedNodes->indices;
                    selectedNodes->indirectDrawCommandBuffers = task.gltf->sceneGpuBuffers.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, task.gltf->selectedNodes->indices, [&](const fastgltf::Primitive &primitive) -> decltype(auto) { return task.gltf->assetGpuBuffers.primitiveInfos.at(&primitive); });
                }
                selectedNodes->outlineColor = task.gltf->selectedNodes->outlineColor;
                selectedNodes->outlineThickness = task.gltf->selectedNodes->outlineThickness;
            }
            else {
                selectedNodes.emplace(
                    task.gltf->selectedNodes->indices,
                    task.gltf->sceneGpuBuffers.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, task.gltf->selectedNodes->indices, [&](const fastgltf::Primitive &primitive) -> decltype(auto) { return task.gltf->assetGpuBuffers.primitiveInfos.at(&primitive); }),
                    task.gltf->selectedNodes->outlineColor,
                    task.gltf->selectedNodes->outlineThickness);
            }
        }
        else {
            selectedNodes.reset();
        }

        if (task.gltf->hoveringNode &&
            // If selectedNodeIndices == hoveringNodeIndex, hovering node outline doesn't have to be drawn.
            !(task.gltf->selectedNodes && task.gltf->selectedNodes->indices.size() == 1 && *task.gltf->selectedNodes->indices.begin() == task.gltf->hoveringNode->index)) {
            if (hoveringNode) {
                if (task.gltf->hoveringNode->shouldRegenerateDrawCommands ||
                    hoveringNode->index != task.gltf->hoveringNode->index) {
                    hoveringNode->index = task.gltf->hoveringNode->index;
                    hoveringNode->indirectDrawCommandBuffers = task.gltf->sceneGpuBuffers.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, { task.gltf->hoveringNode->index }, [&](const fastgltf::Primitive &primitive) -> decltype(auto) { return task.gltf->assetGpuBuffers.primitiveInfos.at(&primitive); });
                }
                hoveringNode->outlineColor = task.gltf->hoveringNode->outlineColor;
                hoveringNode->outlineThickness = task.gltf->hoveringNode->outlineThickness;
            }
            else {
                hoveringNode.emplace(
                    task.gltf->hoveringNode->index,
                    task.gltf->sceneGpuBuffers.createIndirectDrawCommandBuffers<decltype(criteriaGetter), CommandSeparationCriteriaComparator>(gpu.allocator, criteriaGetter, { task.gltf->hoveringNode->index }, [&](const fastgltf::Primitive &primitive) -> decltype(auto) { return task.gltf->assetGpuBuffers.primitiveInfos.at(&primitive); }),
                    task.gltf->hoveringNode->outlineColor,
                    task.gltf->hoveringNode->outlineThickness);
            }
        }
        else {
            hoveringNode.reset();
        }
    }

    if (task.solidBackground) {
        background.emplace<glm::vec3>(*task.solidBackground);
    }
    else {
        background.emplace<vku::DescriptorSet<dsl::Skybox>>(sharedData.skyboxDescriptorSet);
    }

    return result;
}

void vk_gltf_viewer::vulkan::Frame::recordCommandsAndSubmit(std::uint32_t swapchainImageIndex) const {
    // Record commands.
    graphicsCommandPool.reset();
    computeCommandPool.reset();

    // Depth prepass and jump flood seed image calculation pass.
    {
        scenePrepassCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
        recordScenePrepassCommands(scenePrepassCommandBuffer);
        scenePrepassCommandBuffer.end();

        gpu.queues.graphicsPresent.submit(vk::SubmitInfo {
            {},
            {},
            scenePrepassCommandBuffer,
            *scenePrepassFinishSema,
        });
    }

    // Jump flood calculation pass.
    // TODO: If there are multiple compute queues, distribute the tasks to avoid the compute pipeline stalling.
    std::optional<bool> hoveringNodeJumpFloodForward{}, selectedNodeJumpFloodForward{};
    {
        jumpFloodCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
        if (hoveringNode) {
            hoveringNodeJumpFloodForward = recordJumpFloodComputeCommands(
                jumpFloodCommandBuffer,
                passthruResources->hoveringNodeOutlineJumpFloodResources.image,
                hoveringNodeJumpFloodSet,
                std::bit_ceil(static_cast<std::uint32_t>(hoveringNode->outlineThickness)));
            gpu.device.updateDescriptorSets(
                hoveringNodeOutlineSet.getWriteOne<0>({
                    {},
                    *hoveringNodeJumpFloodForward
                        ? *passthruResources->hoveringNodeOutlineJumpFloodResources.pongImageView
                        : *passthruResources->hoveringNodeOutlineJumpFloodResources.pingImageView,
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                }),
                {});
        }
        if (selectedNodes) {
            selectedNodeJumpFloodForward = recordJumpFloodComputeCommands(
                jumpFloodCommandBuffer,
                passthruResources->selectedNodeOutlineJumpFloodResources.image,
                selectedNodeJumpFloodSet,
                std::bit_ceil(static_cast<std::uint32_t>(selectedNodes->outlineThickness)));
            gpu.device.updateDescriptorSets(
                selectedNodeOutlineSet.getWriteOne<0>({
                    {},
                    *selectedNodeJumpFloodForward
                        ? *passthruResources->selectedNodeOutlineJumpFloodResources.pongImageView
                        : *passthruResources->selectedNodeOutlineJumpFloodResources.pingImageView,
                    vk::ImageLayout::eShaderReadOnlyOptimal,
                }),
                {});
        }
        jumpFloodCommandBuffer.end();

        gpu.queues.compute.submit(vk::SubmitInfo {
            *scenePrepassFinishSema,
            vku::unsafeProxy(vk::Flags { vk::PipelineStageFlagBits::eComputeShader }),
            jumpFloodCommandBuffer,
            *jumpFloodFinishSema,
        });
    }

    // glTF scene rendering pass.
    {
        sceneRenderingCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

        vk::ClearColorValue backgroundColor { 0.f, 0.f, 0.f, 0.f };
        if (auto *clearColor = get_if<glm::vec3>(&background)) {
            backgroundColor.setFloat32({ clearColor->x, clearColor->y, clearColor->z, 1.f });
        }
        sceneRenderingCommandBuffer.beginRenderPass({
            *sharedData.sceneRenderPass,
            *framebuffers[swapchainImageIndex],
            vk::Rect2D { { 0, 0 }, sharedData.swapchainExtent },
            vku::unsafeProxy<vk::ClearValue>({
                backgroundColor,
                vk::ClearColorValue{},
                vk::ClearDepthStencilValue { 0.f, 0 },
                vk::ClearColorValue { 0.f, 0.f, 0.f, 0.f },
                vk::ClearColorValue{},
                vk::ClearColorValue { 1.f, 0.f, 0.f, 0.f },
                vk::ClearColorValue{},
            }),
        }, vk::SubpassContents::eInline);

        const vk::Viewport passthruViewport {
            // Use negative viewport.
            static_cast<float>(passthruRect.offset.x), static_cast<float>(passthruRect.offset.y + passthruRect.extent.height),
            static_cast<float>(passthruRect.extent.width), -static_cast<float>(passthruRect.extent.height),
            0.f, 1.f,
        };
        sceneRenderingCommandBuffer.setViewport(0, passthruViewport);
        sceneRenderingCommandBuffer.setScissor(0, passthruRect);

        if (renderingNodes) {
            recordSceneOpaqueMeshDrawCommands(sceneRenderingCommandBuffer);
        }
        if (holds_alternative<vku::DescriptorSet<dsl::Skybox>>(background)) {
            recordSkyboxDrawCommands(sceneRenderingCommandBuffer);
        }

        // Render meshes whose AlphaMode=Blend.
        sceneRenderingCommandBuffer.nextSubpass(vk::SubpassContents::eInline);
        bool hasBlendMesh = false;
        if (renderingNodes) {
            hasBlendMesh = recordSceneBlendMeshDrawCommands(sceneRenderingCommandBuffer);
        }

        sceneRenderingCommandBuffer.nextSubpass(vk::SubpassContents::eInline);

        if (hasBlendMesh) {
            // Weighted blended composition.
            sceneRenderingCommandBuffer.bindPipeline(
                vk::PipelineBindPoint::eGraphics,
                sharedData.weightedBlendedCompositionRenderer.pipeline);
            sceneRenderingCommandBuffer.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                sharedData.weightedBlendedCompositionRenderer.pipelineLayout,
                0, weightedBlendedCompositionSet, {});
            sceneRenderingCommandBuffer.draw(3, 1, 0, 0);
        }

        sceneRenderingCommandBuffer.endRenderPass();

        sceneRenderingCommandBuffer.end();
    }

    // Post-composition pass.
    {
        compositionCommandBuffer.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

        if (selectedNodes || hoveringNode) {
            recordNodeOutlineCompositionCommands(compositionCommandBuffer, hoveringNodeJumpFloodForward, selectedNodeJumpFloodForward, swapchainImageIndex);

            // Make sure the outline composition is done before rendering ImGui.
            compositionCommandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                {},
                vk::MemoryBarrier {
                    vk::AccessFlagBits::eColorAttachmentWrite,
                    vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
                },
                {}, {});
        }

        recordImGuiCompositionCommands(compositionCommandBuffer, swapchainImageIndex);

        // Change swapchain image layout from ColorAttachmentOptimal to PresentSrcKHR.
        compositionCommandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
            {}, {}, {},
            vk::ImageMemoryBarrier {
                vk::AccessFlagBits::eColorAttachmentWrite, {},
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sharedData.swapchainImages[swapchainImageIndex], vku::fullSubresourceRange(),
            });

        compositionCommandBuffer.end();
    }

    gpu.queues.graphicsPresent.submit({
        vk::SubmitInfo {
            *swapchainImageAcquireSema,
            vku::unsafeProxy(vk::Flags { vk::PipelineStageFlagBits::eColorAttachmentOutput }),
            sceneRenderingCommandBuffer,
            *sceneRenderingFinishSema,
        },
        vk::SubmitInfo {
            vku::unsafeProxy({ *sceneRenderingFinishSema, *jumpFloodFinishSema }),
            vku::unsafeProxy({
                vk::Flags { vk::PipelineStageFlagBits::eFragmentShader },
                vk::Flags { vk::PipelineStageFlagBits::eFragmentShader },
            }),
            compositionCommandBuffer,
            *compositionFinishSema,
        },
    }, *inFlightFence);
}

vk_gltf_viewer::vulkan::Frame::PassthruResources::JumpFloodResources::JumpFloodResources(
    const Gpu &gpu,
    const vk::Extent2D &extent
) : image { gpu.allocator, vk::ImageCreateInfo {
        {},
        vk::ImageType::e2D,
        vk::Format::eR16G16Uint,
        vk::Extent3D { extent, 1 },
        1, 2, // arrayLevels=0 for ping image, arrayLevels=1 for pong image.
        vk::SampleCountFlagBits::e1,
        vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eColorAttachment /* write from DepthRenderer */
            | vk::ImageUsageFlagBits::eStorage /* used as ping pong image in JumpFloodComputer */
            | vk::ImageUsageFlagBits::eSampled /* read in OutlineRenderer */,
        gpu.queueFamilies.uniqueIndices.size() == 1 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
        gpu.queueFamilies.uniqueIndices,
    } },
    imageView { gpu.device, image.getViewCreateInfo(vk::ImageViewType::e2DArray) },
    pingImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }) },
    pongImageView { gpu.device, image.getViewCreateInfo({ vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 }) } { }

vk_gltf_viewer::vulkan::Frame::PassthruResources::PassthruResources(
    const Gpu &gpu,
    const vk::Extent2D &extent,
    vk::CommandBuffer graphicsCommandBuffer
) : extent { extent },
    hoveringNodeOutlineJumpFloodResources { gpu, extent },
    selectedNodeOutlineJumpFloodResources { gpu, extent },
    depthPrepassAttachmentGroup { gpu, extent },
    hoveringNodeJumpFloodSeedAttachmentGroup { gpu, hoveringNodeOutlineJumpFloodResources.image },
    selectedNodeJumpFloodSeedAttachmentGroup { gpu, selectedNodeOutlineJumpFloodResources.image } {
    recordInitialImageLayoutTransitionCommands(graphicsCommandBuffer);
}

auto vk_gltf_viewer::vulkan::Frame::PassthruResources::recordInitialImageLayoutTransitionCommands(
    vk::CommandBuffer graphicsCommandBuffer
) const -> void {
    constexpr auto layoutTransitionBarrier = [](
        vk::ImageLayout newLayout,
        vk::Image image,
        const vk::ImageSubresourceRange &subresourceRange = vku::fullSubresourceRange()
    ) {
        return vk::ImageMemoryBarrier {
            {}, {},
            {}, newLayout,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            image, subresourceRange
        };
    };
    graphicsCommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
        {}, {}, {},
        {
            layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, depthPrepassAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
            layoutTransitionBarrier(vk::ImageLayout::eGeneral, hoveringNodeOutlineJumpFloodResources.image, { vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 } /* pong image */),
            layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, hoveringNodeJumpFloodSeedAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
            layoutTransitionBarrier(vk::ImageLayout::eGeneral, selectedNodeOutlineJumpFloodResources.image, { vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 } /* pong image */),
            layoutTransitionBarrier(vk::ImageLayout::eDepthAttachmentOptimal, selectedNodeJumpFloodSeedAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth)),
        });
}

auto vk_gltf_viewer::vulkan::Frame::createFramebuffers() const -> std::vector<vk::raii::Framebuffer> {
    return sceneOpaqueAttachmentGroup.getSwapchainAttachment(0).resolveViews
        | std::views::transform([this](vk::ImageView swapchainImageView) {
            return vk::raii::Framebuffer { gpu.device, vk::FramebufferCreateInfo {
                {},
                *sharedData.sceneRenderPass,
                vku::unsafeProxy({
                    *sceneOpaqueAttachmentGroup.getSwapchainAttachment(0).view,
                    swapchainImageView,
                    *sceneOpaqueAttachmentGroup.depthStencilAttachment->view,
                    *sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).view,
                    *sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).resolveView,
                    *sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).view,
                    *sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).resolveView,
                }),
                sharedData.swapchainExtent.width,
                sharedData.swapchainExtent.height,
                1,
            } };
        })
        | std::ranges::to<std::vector>();
}

auto vk_gltf_viewer::vulkan::Frame::createDescriptorPool() const -> decltype(descriptorPool) {
    return {
        gpu.device,
        (2 * getPoolSizes(sharedData.jumpFloodComputer.descriptorSetLayout, sharedData.outlineRenderer.descriptorSetLayout)
            + sharedData.weightedBlendedCompositionRenderer.descriptorSetLayout.getPoolSize())
            .getDescriptorPoolCreateInfo(),
    };
}

auto vk_gltf_viewer::vulkan::Frame::recordScenePrepassCommands(vk::CommandBuffer cb) const -> void {
    boost::container::static_vector<vk::ImageMemoryBarrier, 3> memoryBarriers;

    // If glTF Scene have to be rendered, prepare attachment layout transition for node index and depth rendering.
    if (renderingNodes) {
        memoryBarriers.push_back({
            {}, vk::AccessFlagBits::eColorAttachmentWrite,
            {}, vk::ImageLayout::eColorAttachmentOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            passthruResources->depthPrepassAttachmentGroup.getColorAttachment(0).image, vku::fullSubresourceRange(),
        });
    }

    // If hovering node's outline have to be rendered, prepare attachment layout transition for jump flood seeding.
    const auto addJumpFloodSeedImageMemoryBarrier = [&](vk::Image image) {
        memoryBarriers.push_back({
            {}, vk::AccessFlagBits::eColorAttachmentWrite,
            {}, vk::ImageLayout::eColorAttachmentOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } /* ping image */,
        });
    };
    if (selectedNodes) {
        addJumpFloodSeedImageMemoryBarrier(passthruResources->selectedNodeOutlineJumpFloodResources.image);
    }
    // Same holds for hovering nodes' outline.
    if (hoveringNode) {
        addJumpFloodSeedImageMemoryBarrier(passthruResources->hoveringNodeOutlineJumpFloodResources.image);
    }

    // Attachment layout transitions.
    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
        {}, {}, {}, memoryBarriers);

    struct {
        std::optional<vk::Pipeline> boundPipeline{};
        std::optional<vk::CullModeFlagBits> cullMode{};
        std::optional<vk::IndexType> indexBuffer;

        // (Mask){Depth|JumpFloodSeed}Renderer have compatible descriptor set layouts and push constant range,
        // therefore they only need to be bound once.
        bool descriptorSetBound = false;
        bool pushConstantBound = false;
    } resourceBindingState{};

    const auto drawPrimitives = [&](
        const CriteriaSeparatedIndirectDrawCommands &indirectDrawCommandBuffers,
        concepts::signature_of<vk::Pipeline, RenderingStrategy> auto const &pipelineGetter
    ) {
        for (const auto &[criteria, indirectDrawCommandBuffer] : indirectDrawCommandBuffers) {
            if (vk::Pipeline pipeline = pipelineGetter(criteria.strategy); resourceBindingState.boundPipeline != pipeline) {
                cb.bindPipeline(vk::PipelineBindPoint::eGraphics, resourceBindingState.boundPipeline.emplace(pipeline));
            }

            if (!resourceBindingState.descriptorSetBound) {
                cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.primitiveNoShadingPipelineLayout,
                    0, { sharedData.assetDescriptorSet, sharedData.sceneDescriptorSet }, {});
                resourceBindingState.descriptorSetBound = true;
            }

            if (!resourceBindingState.pushConstantBound) {
                sharedData.primitiveNoShadingPipelineLayout.pushConstants(cb, { projectionViewMatrix });
                resourceBindingState.pushConstantBound = true;
            }

            if (auto cullMode = criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack; resourceBindingState.cullMode != cullMode) {
                cb.setCullMode(resourceBindingState.cullMode.emplace(cullMode));
            }

            if (const auto &indexType = criteria.indexType; indexType && resourceBindingState.indexBuffer != *indexType) {
                cb.bindIndexBuffer(indexBuffers.at(*indexType), 0, resourceBindingState.indexBuffer.emplace(*indexType));
            }
            visit([&](const auto &x) { x.recordDrawCommand(cb, gpu.supportDrawIndirectCount); }, indirectDrawCommandBuffer);
        }
    };

    if (renderingNodes && cursorPosFromPassthruRectTopLeft) {
        cb.beginRenderingKHR(passthruResources->depthPrepassAttachmentGroup.getRenderingInfo(
            vku::AttachmentGroup::ColorAttachmentInfo {
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore,
                { static_cast<std::uint32_t>(NO_INDEX), 0U, 0U, 0U },
            },
            vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));

        cb.setViewport(0, vku::toViewport(passthruResources->extent, true));
        cb.setScissor(0, vk::Rect2D{ *cursorPosFromPassthruRectTopLeft, { 1, 1 } });

        drawPrimitives(renderingNodes->indirectDrawCommandBuffers, [this](RenderingStrategy strategy) {
            if (ranges::one_of(strategy, RenderingStrategy::Mask, RenderingStrategy::MaskUnlit, RenderingStrategy::MaskFaceted)) {
                return *sharedData.maskDepthRenderer;
            }
            return *sharedData.depthRenderer;
        });

        cb.endRenderingKHR();
    }

    // Seeding jump flood initial image for hovering node.
    if (hoveringNode) {
        cb.beginRenderingKHR(passthruResources->hoveringNodeJumpFloodSeedAttachmentGroup.getRenderingInfo(
            vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
            vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));

        cb.setViewport(0, vku::toViewport(passthruResources->extent, true));
        cb.setScissor(0, vk::Rect2D{ { 0, 0 }, passthruResources->extent });

        drawPrimitives(hoveringNode->indirectDrawCommandBuffers, [this](RenderingStrategy strategy) {
            if (ranges::one_of(strategy, RenderingStrategy::Mask, RenderingStrategy::MaskUnlit, RenderingStrategy::MaskFaceted)) {
                return *sharedData.maskJumpFloodSeedRenderer;
            }
            return *sharedData.jumpFloodSeedRenderer;
        });

        cb.endRenderingKHR();
    }

    // Seeding jump flood initial image for selected node.
    if (selectedNodes) {
        cb.beginRenderingKHR(passthruResources->selectedNodeJumpFloodSeedAttachmentGroup.getRenderingInfo(
            vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, { 0U, 0U, 0U, 0U } },
            vku::AttachmentGroup::DepthStencilAttachmentInfo { vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, { 0.f, 0U } }));

        cb.setViewport(0, vku::toViewport(passthruResources->extent, true));
        cb.setScissor(0, vk::Rect2D{ { 0, 0 }, passthruResources->extent });

        drawPrimitives(selectedNodes->indirectDrawCommandBuffers, [this](RenderingStrategy strategy) {
            if (ranges::one_of(strategy, RenderingStrategy::Mask, RenderingStrategy::MaskUnlit, RenderingStrategy::MaskFaceted)) {
                return *sharedData.maskJumpFloodSeedRenderer;
            }
            return *sharedData.jumpFloodSeedRenderer;
        });

        cb.endRenderingKHR();
    }

    // If there are rendered nodes and the cursor is inside the passthru rect, do mouse picking.
    if (renderingNodes && cursorPosFromPassthruRectTopLeft) {
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer,
            {}, {}, {},
            // For copying to hoveringNodeIndexBuffer.
            vk::ImageMemoryBarrier {
                vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                passthruResources->depthPrepassAttachmentGroup.getColorAttachment(0).image, vku::fullSubresourceRange(),
            });

        cb.copyImageToBuffer(
            passthruResources->depthPrepassAttachmentGroup.getColorAttachment(0).image, vk::ImageLayout::eTransferSrcOptimal,
            hoveringNodeIndexBuffer,
            vk::BufferImageCopy {
                0, {}, {},
                { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
                vk::Offset3D { *cursorPosFromPassthruRectTopLeft, 0 },
                { 1, 1, 1 },
            });

        // hoveringNodeIndexBuffer data have to be available to the host.
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
            {}, {},
            vk::BufferMemoryBarrier {
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                hoveringNodeIndexBuffer, 0, vk::WholeSize,
            },
            {});
    }
}

auto vk_gltf_viewer::vulkan::Frame::recordJumpFloodComputeCommands(
    vk::CommandBuffer cb,
    const vku::Image &image,
    vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> descriptorSet,
    std::uint32_t initialSampleOffset
) const -> bool {
    cb.pipelineBarrier2KHR({
        {}, {}, {},
        vku::unsafeProxy({
            vk::ImageMemoryBarrier2 {
                // Dependency chain: this srcStageMask must match to the cb's submission waitDstStageMask.
                vk::PipelineStageFlagBits2::eComputeShader, {},
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
            },
            vk::ImageMemoryBarrier2 {
                {}, {},
                vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
                {}, vk::ImageLayout::eGeneral,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                image, { vk::ImageAspectFlagBits::eColor, 0, 1, 1, 1 },
            }
        }),
    });

    // Compute jump flood and get the last execution direction.
    return sharedData.jumpFloodComputer.compute(cb, descriptorSet, initialSampleOffset, vku::toExtent2D(image.extent));
}

auto vk_gltf_viewer::vulkan::Frame::recordSceneOpaqueMeshDrawCommands(vk::CommandBuffer cb) const -> void {
    assert(renderingNodes && "No nodes have to be rendered.");

    struct {
        std::optional<vk::Pipeline> boundPipeline{};
        std::optional<vk::CullModeFlagBits> cullMode{};
        std::optional<vk::IndexType> indexBuffer{};

        // (Mask)(Faceted)PrimitiveRenderer have compatible descriptor set layouts and push constant range,
        // therefore they only need to be bound once.
        bool descriptorBound = false;
        bool pushConstantBound = false;
    } resourceBindingState{};

    const auto getPipeline = [this](RenderingStrategy strategy) {
        switch (strategy) {
        case RenderingStrategy::Opaque:
            return *sharedData.primitiveRenderer;
        case RenderingStrategy::OpaqueUnlit:
            return *sharedData.unlitPrimitiveRenderer;
        case RenderingStrategy::OpaqueFaceted:
            return *sharedData.facetedPrimitiveRenderer;
        case RenderingStrategy::Mask:
            return *sharedData.maskPrimitiveRenderer;
        case RenderingStrategy::MaskUnlit:
            return *sharedData.maskUnlitPrimitiveRenderer;
        case RenderingStrategy::MaskFaceted:
            return *sharedData.maskFacetedPrimitiveRenderer;
        default:
            throw std::invalid_argument { "Invalid rendering strategy for this function" };
        }
    };

    // Render alphaMode=Opaque | Mask meshes.
    const auto drawCommandBuffers = std::ranges::subrange(
        renderingNodes->indirectDrawCommandBuffers.lower_bound(RenderingStrategy::Opaque),
        renderingNodes->indirectDrawCommandBuffers.end());
    for (const auto &[criteria, indirectDrawCommandBuffer] : drawCommandBuffers) {
        if (vk::Pipeline pipeline = getPipeline(criteria.strategy); resourceBindingState.boundPipeline != pipeline) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, resourceBindingState.boundPipeline.emplace(pipeline));
        }
        if (!resourceBindingState.descriptorBound) {
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.primitivePipelineLayout, 0,
                { sharedData.imageBasedLightingDescriptorSet, sharedData.assetDescriptorSet, sharedData.sceneDescriptorSet }, {});
            resourceBindingState.descriptorBound = true;
        }
        if (!resourceBindingState.pushConstantBound) {
            sharedData.primitivePipelineLayout.pushConstants(cb, { projectionViewMatrix, viewPosition });
            resourceBindingState.pushConstantBound = true;
        }

        if (auto cullMode = criteria.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack; resourceBindingState.cullMode != cullMode) {
            cb.setCullMode(resourceBindingState.cullMode.emplace(cullMode));
        }

        if (const auto &indexType = criteria.indexType; indexType && resourceBindingState.indexBuffer != *indexType) {
            cb.bindIndexBuffer(indexBuffers.at(*indexType), 0, resourceBindingState.indexBuffer.emplace(*indexType));
        }
        visit([&](const auto &x) { x.recordDrawCommand(cb, gpu.supportDrawIndirectCount); }, indirectDrawCommandBuffer);
    }
}

auto vk_gltf_viewer::vulkan::Frame::recordSceneBlendMeshDrawCommands(vk::CommandBuffer cb) const -> bool {
    assert(renderingNodes && "No nodes have to be rendered.");

    struct {
        std::optional<vk::Pipeline> boundPipeline{};
        std::optional<vk::IndexType> indexBuffer{};

        // Blend(Faceted)PrimitiveRenderer have compatible descriptor set layouts and push constant range,
        // therefore they only need to be bound once.
        bool descriptorBound = false;
        bool pushConstantBound = false;
    } resourceBindingState;

    const auto getPipeline = [this](RenderingStrategy strategy) {
        switch (strategy) {
            case RenderingStrategy::Blend:
                return *sharedData.blendPrimitiveRenderer;
            case RenderingStrategy::BlendUnlit:
                return *sharedData.blendUnlitPrimitiveRenderer;
            case RenderingStrategy::BlendFaceted:
                return *sharedData.blendFacetedPrimitiveRenderer;
            default:
                throw std::invalid_argument { "Invalid rendering strategy for this function" };
        }
    };

    // Render alphaMode=Blend meshes.
    bool hasBlendMesh = false;
    const auto drawCommandBuffers = std::ranges::subrange(
        renderingNodes->indirectDrawCommandBuffers.begin(),
        renderingNodes->indirectDrawCommandBuffers.upper_bound(RenderingStrategy::BlendFaceted));
    for (const auto &[criteria, indirectDrawCommandBuffer] : drawCommandBuffers) {
        if (vk::Pipeline pipeline = getPipeline(criteria.strategy); resourceBindingState.boundPipeline != pipeline) {
            resourceBindingState.boundPipeline = pipeline;
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *resourceBindingState.boundPipeline);
        }
        if (!resourceBindingState.descriptorBound) {
            cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.primitivePipelineLayout, 0,
                { sharedData.imageBasedLightingDescriptorSet, sharedData.assetDescriptorSet, sharedData.sceneDescriptorSet }, {});
            resourceBindingState.descriptorBound = true;
        }
        if (!resourceBindingState.pushConstantBound) {
            sharedData.primitivePipelineLayout.pushConstants(cb, { projectionViewMatrix, viewPosition });
            resourceBindingState.pushConstantBound = true;
        }

        if (const auto &indexType = criteria.indexType; indexType && resourceBindingState.indexBuffer != *indexType) {
            cb.bindIndexBuffer(indexBuffers.at(*indexType), 0, resourceBindingState.indexBuffer.emplace(*indexType));
        }
        visit([&](const auto &x) { x.recordDrawCommand(cb, gpu.supportDrawIndirectCount); }, indirectDrawCommandBuffer);
        hasBlendMesh = true;
    }

    return hasBlendMesh;
}

auto vk_gltf_viewer::vulkan::Frame::recordSkyboxDrawCommands(vk::CommandBuffer cb) const -> void {
    assert(holds_alternative<vku::DescriptorSet<dsl::Skybox>>(background) && "recordSkyboxDrawCommand called, but background is not set to the proper skybox descriptor set.");
    sharedData.skyboxRenderer.draw(cb, get<vku::DescriptorSet<dsl::Skybox>>(background), { translationlessProjectionViewMatrix });
}

auto vk_gltf_viewer::vulkan::Frame::recordNodeOutlineCompositionCommands(
    vk::CommandBuffer cb,
    std::optional<bool> hoveringNodeJumpFloodForward,
    std::optional<bool> selectedNodeJumpFloodForward,
    std::uint32_t swapchainImageIndex
) const -> void {
    boost::container::static_vector<vk::ImageMemoryBarrier, 2> memoryBarriers;
    // Change jump flood image layouts to ShaderReadOnlyOptimal.
    if (hoveringNodeJumpFloodForward) {
        memoryBarriers.push_back({
            {}, vk::AccessFlagBits::eShaderRead,
            vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            passthruResources->hoveringNodeOutlineJumpFloodResources.image,
            { vk::ImageAspectFlagBits::eColor, 0, 1, *hoveringNodeJumpFloodForward, 1 },
        });
    }
    if (selectedNodeJumpFloodForward) {
        memoryBarriers.push_back({
            {}, vk::AccessFlagBits::eShaderRead,
            vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
            passthruResources->selectedNodeOutlineJumpFloodResources.image,
            { vk::ImageAspectFlagBits::eColor, 0, 1, *selectedNodeJumpFloodForward, 1 },
        });
    }
    if (!memoryBarriers.empty()) {
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, memoryBarriers);
    }

    // Set viewport and scissor.
    const vk::Viewport passthruViewport {
        // Use negative viewport.
        static_cast<float>(passthruRect.offset.x), static_cast<float>(passthruRect.offset.y + passthruRect.extent.height),
        static_cast<float>(passthruRect.extent.width), -static_cast<float>(passthruRect.extent.height),
        0.f, 1.f,
    };
    cb.setViewport(0, passthruViewport);
    cb.setScissor(0, passthruRect);

    cb.beginRenderingKHR(sharedData.swapchainAttachmentGroup.getRenderingInfo(
        vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore },
        swapchainImageIndex));

    // Draw hovering/selected node outline if exists.
    bool pipelineBound = false;
    if (selectedNodes) {
        if (!pipelineBound) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.outlineRenderer.pipeline);
            pipelineBound = true;
        }
        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.outlineRenderer.pipelineLayout, 0,
            selectedNodeOutlineSet, {});
        cb.pushConstants<OutlineRenderer::PushConstant>(
            *sharedData.outlineRenderer.pipelineLayout, vk::ShaderStageFlagBits::eFragment,
            0, OutlineRenderer::PushConstant {
                .outlineColor = selectedNodes->outlineColor,
                .passthruOffset = { passthruRect.offset.x, passthruRect.offset.y },
                .outlineThickness = selectedNodes->outlineThickness,
            });
        cb.draw(3, 1, 0, 0);
    }
    if (hoveringNode) {
        if (selectedNodes) {
            // TODO: pipeline barrier required.
        }

        if (!pipelineBound) {
            cb.bindPipeline(vk::PipelineBindPoint::eGraphics, *sharedData.outlineRenderer.pipeline);
            pipelineBound = true;
        }

        cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *sharedData.outlineRenderer.pipelineLayout, 0,
            hoveringNodeOutlineSet, {});
        cb.pushConstants<OutlineRenderer::PushConstant>(
            *sharedData.outlineRenderer.pipelineLayout, vk::ShaderStageFlagBits::eFragment,
            0, OutlineRenderer::PushConstant {
                .outlineColor = hoveringNode->outlineColor,
                .passthruOffset = { passthruRect.offset.x, passthruRect.offset.y },
                .outlineThickness = hoveringNode->outlineThickness,
            });
        cb.draw(3, 1, 0, 0);
    }

    cb.endRenderingKHR();
}

auto vk_gltf_viewer::vulkan::Frame::recordImGuiCompositionCommands(
    vk::CommandBuffer cb,
    std::uint32_t swapchainImageIndex
) const -> void {
    // Start dynamic rendering with B8G8R8A8_UNORM format.
    cb.beginRenderingKHR(visit_as<const ag::Swapchain&>(sharedData.imGuiSwapchainAttachmentGroup).getRenderingInfo(
        vku::AttachmentGroup::ColorAttachmentInfo { vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore },
        swapchainImageIndex));

    // Draw ImGui.
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);

    cb.endRenderingKHR();
}

auto vk_gltf_viewer::vulkan::Frame::recordSwapchainExtentDependentImageLayoutTransitionCommands(
    vk::CommandBuffer graphicsCommandBuffer
) const -> void {
    graphicsCommandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
        {}, {}, {},
        {
            vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sceneOpaqueAttachmentGroup.getSwapchainAttachment(0).image, vku::fullSubresourceRange(),
            },
            vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::eDepthAttachmentOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sceneOpaqueAttachmentGroup.depthStencilAttachment->image, vku::fullSubresourceRange(vk::ImageAspectFlagBits::eDepth),
            },
            vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).image, vku::fullSubresourceRange(),
            },
            vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sceneWeightedBlendedAttachmentGroup.getColorAttachment(0).resolveImage, vku::fullSubresourceRange(),
            },
            vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).image, vku::fullSubresourceRange(),
            },
            vk::ImageMemoryBarrier {
                {}, {},
                {}, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored, vk::QueueFamilyIgnored,
                sceneWeightedBlendedAttachmentGroup.getColorAttachment(1).resolveImage, vku::fullSubresourceRange(),
            },
        });
}