module;

#include <fastgltf/core.hpp>

module vk_gltf_viewer;
import :gltf.SceneResources;

import std;
import :helpers.ranges;

using namespace std::views;

vk_gltf_viewer::gltf::SceneResources::SceneResources(
    const AssetResources &assetResources,
    const fastgltf::Scene &scene,
    const vulkan::Gpu &gpu
) : assetResources { assetResources },
    scene { scene },
    nodeTransformBuffer { createNodeTransformBuffer(gpu.allocator) },
    primitiveBuffer { createPrimitiveBuffer(gpu) },
    indirectDrawCommandBuffers { createIndirectDrawCommandBuffer(assetResources.asset, gpu.allocator) } { }

auto vk_gltf_viewer::gltf::SceneResources::createOrderedNodePrimitiveInfoPtrs() const -> decltype(orderedNodePrimitiveInfoPtrs) {
    const fastgltf::Asset &asset = assetResources.asset;

    // Collect glTF mesh primitives.
    std::vector<std::pair<std::uint32_t /* nodeIndex */, const AssetResources::PrimitiveInfo*>> nodePrimitiveInfoPtrs;
    for (std::stack dfs { std::from_range, asset.scenes[asset.defaultScene.value_or(0)].nodeIndices | reverse }; !dfs.empty(); ) {
        const std::size_t nodeIndex = dfs.top();
        const fastgltf::Node &node = asset.nodes[nodeIndex];
        if (node.meshIndex) {
            const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
            nodePrimitiveInfoPtrs.append_range(
                mesh.primitives
                    | transform([&](const fastgltf::Primitive &primitive) {
                        const AssetResources::PrimitiveInfo &primitiveInfo = assetResources.primitiveInfos.at(&primitive);
                        return std::pair { static_cast<std::uint32_t>(nodeIndex), &primitiveInfo };
                    }));
        }
        dfs.pop();
        dfs.push_range(node.children | reverse);
    }

    return nodePrimitiveInfoPtrs;
}

auto vk_gltf_viewer::gltf::SceneResources::createNodeTransformBuffer(
    vma::Allocator allocator
) const -> decltype(nodeTransformBuffer) {
    std::vector<glm::mat4> nodeTransforms(assetResources.asset.nodes.size());
    const auto calculateNodeTransformsRecursive
        = [&](this const auto &self, std::size_t nodeIndex, glm::mat4 transform) -> void {
            const fastgltf::Node &node = assetResources.asset.nodes[nodeIndex];
            transform *= visit(fastgltf::visitor {
                [](const fastgltf::TRS &trs) {
                    return glm::gtc::translate(glm::mat4 { 1.f }, glm::gtc::make_vec3(trs.translation.data()))
                        * glm::gtc::mat4_cast(glm::gtc::make_quat(trs.rotation.data()))
                        * glm::gtc::scale(glm::mat4 { 1.f }, glm::gtc::make_vec3(trs.scale.data()));
                },
                [](const fastgltf::Node::TransformMatrix &mat) {
                    return glm::gtc::make_mat4(mat.data());
                },
            }, node.transform);
            nodeTransforms[nodeIndex] = transform;
            for (std::size_t childIndex : node.children) {
                self(childIndex, transform);
            }
        };
    for (std::size_t nodeIndex : scene.nodeIndices) {
        calculateNodeTransformsRecursive(nodeIndex, { 1.f });
    }

    return {
        allocator,
        std::from_range, nodeTransforms,
        vk::BufferUsageFlagBits::eStorageBuffer,
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAuto,
        },
    };
}

auto vk_gltf_viewer::gltf::SceneResources::createPrimitiveBuffer(
    const vulkan::Gpu &gpu
) -> decltype(primitiveBuffer) {
    return {
        gpu.allocator,
        std::from_range, orderedNodePrimitiveInfoPtrs | transform([](const auto &pair){
            const auto [nodeIndex, pPrimitiveInfo] = pair;
            return GpuPrimitive {
                .pPositionBuffer = pPrimitiveInfo->positionInfo.address,
                .pNormalBuffer = pPrimitiveInfo->normalInfo.value().address,
                .pTangentBuffer = pPrimitiveInfo->tangentInfo.value().address,
                .pTexcoordBufferPtrsBuffer = ranges::value_or(
                    pPrimitiveInfo->indexedAttributeMappingInfos,
                    AssetResources::IndexedAttribute::Texcoord, {}).pBufferPtrBuffer,
                .pColorBufferPtrsBuffer = ranges::value_or(
                    pPrimitiveInfo->indexedAttributeMappingInfos,
                    AssetResources::IndexedAttribute::Color, {}).pBufferPtrBuffer,
                .positionByteStride = pPrimitiveInfo->positionInfo.byteStride,
                .normalByteStride = pPrimitiveInfo->normalInfo.value().byteStride,
                .tangentByteStride = pPrimitiveInfo->tangentInfo.value().byteStride,
                .pTexcoordByteStridesBuffer = ranges::value_or(
                    pPrimitiveInfo->indexedAttributeMappingInfos,
                    AssetResources::IndexedAttribute::Texcoord, {}).pByteStridesBuffer,
                .pColorByteStridesBuffer = ranges::value_or(
                    pPrimitiveInfo->indexedAttributeMappingInfos,
                    AssetResources::IndexedAttribute::Color, {}).pByteStridesBuffer,
                .nodeIndex = nodeIndex,
                .materialIndex = pPrimitiveInfo->materialIndex.value(),
            };
        }),
        vk::BufferUsageFlagBits::eStorageBuffer,
    };
}

auto vk_gltf_viewer::gltf::SceneResources::createIndirectDrawCommandBuffer(
    const fastgltf::Asset &asset,
    vma::Allocator allocator
) const -> decltype(indirectDrawCommandBuffers) {
    std::map<CommandSeparationCriteria, std::vector<vk::DrawIndexedIndirectCommand>> indexedCommandGroups;
    std::map<CommandSeparationCriteria, std::vector<vk::DrawIndirectCommand>> nonIndexedCommandGroups;

    for (std::uint32_t instanceCounter = 0; const AssetResources::PrimitiveInfo *pPrimitiveInfo : orderedNodePrimitiveInfoPtrs | values) {
        const CommandSeparationCriteria criteria {
            .alphaMode = asset.materials[*pPrimitiveInfo->materialIndex].alphaMode,
            .doubleSided = asset.materials[*pPrimitiveInfo->materialIndex].doubleSided,
            .indexType = pPrimitiveInfo->indexInfo.transform([](const auto &info) { return info.type; }),
        };
        if (const auto &indexInfo = pPrimitiveInfo->indexInfo) {
            const std::size_t indexByteSize = [=]() {
                switch (indexInfo->type) {
                    case vk::IndexType::eUint16: return sizeof(std::uint16_t);
                    case vk::IndexType::eUint32: return sizeof(std::uint32_t);
                    default: throw std::runtime_error{ "Unsupported index type: only Uint16 and Uint32 are supported" };
                }
            }();

            indexedCommandGroups[criteria].emplace_back(
                pPrimitiveInfo->drawCount,
                1,
                static_cast<std::uint32_t>(pPrimitiveInfo->indexInfo->offset / indexByteSize),
                0,
                instanceCounter);
        }
        else {
            nonIndexedCommandGroups[criteria].emplace_back(
                pPrimitiveInfo->drawCount,
                1,
                0,
                instanceCounter);
        }

        ++instanceCounter;
    }

    std::map<CommandSeparationCriteria, vku::MappedBuffer, CommandSeparationCriteriaComparator> result;
    for (const auto &[criteria, commands] : indexedCommandGroups) {
        result.try_emplace(
            criteria,
            allocator,
            std::from_range, commands,
            vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
    }
    for (const auto &[criteria, commands] : nonIndexedCommandGroups) {
        result.try_emplace(
            criteria,
            allocator,
            std::from_range, commands,
            vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer);
    }

    return result;
}