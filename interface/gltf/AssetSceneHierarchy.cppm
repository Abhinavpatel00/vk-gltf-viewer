export module vk_gltf_viewer:gltf.AssetSceneHierarchy;

import std;
export import fastgltf;
export import glm;
import :gltf.algorithm.traversal;
import :helpers.fastgltf;
import :helpers.optional;
import :helpers.ranges;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return (__VA_ARGS__)(FWD(xs)...); }

namespace vk_gltf_viewer::gltf {
    /**
     * @brief Scene hierarchy information of the glTF asset scene.
     *
     * This class contains a hierarchy of the scene nodes and their world transformation matrices.
     */
    export class AssetSceneHierarchy {
        const fastgltf::Asset *pAsset;

        /**
         * @brief Index of the parent node for each node. If the node is root node, the value is as same as the node index.
         *
         * You should use getParentNodeIndex() method to get the parent node index with proper root node handling.
         */
        std::vector<std::size_t> parentNodeIndices = createParentNodeIndices();

    public:
        /**
         * @brief World transformation matrices of each node. <tt>nodeWorldTransforms[i]</tt> = (world transformation matrix of the <tt>i</tt>-th node).
         */
        std::vector<glm::mat4> nodeWorldTransforms;

        AssetSceneHierarchy(const fastgltf::Asset &asset, const fastgltf::Scene &scene)
            : pAsset { &asset }
            , nodeWorldTransforms { createNodeWorldTransforms(scene) } { }

        /**
         * @brief Get parent node index from current node index.
         * @param nodeIndex Index of the node.
         * @return Index of the parent node if the current node is not root node, otherwise <tt>std::nullopt</tt>.
         */
        [[nodiscard]] std::optional<std::size_t> getParentNodeIndex(std::size_t nodeIndex) const noexcept {
            const std::size_t parentNodeIndex = parentNodeIndices[nodeIndex];
            return value_if(parentNodeIndex != nodeIndex, parentNodeIndex);
        }

        /**
         * @brief Update the world transform matrices of the current (specified by \p nodeIndex) and its descendant nodes.
         *
         * You can call this function when <tt>asset.nodes[nodeIndex]</tt> (local transform of the node) is changed, to update the world transform matrices of the current and its descendant nodes.
         *
         * @param nodeIndex Node index to be started.
         */
        void updateDescendantNodeTransformsFrom(std::size_t nodeIndex) {
            glm::mat4 currentNodeWorldTransform = visit(LIFT(fastgltf::toMatrix), pAsset->nodes[nodeIndex].transform);
            // If node is not root node, pre-multiply the parent node's world transform.
            if (auto parentNodeIndex = getParentNodeIndex(nodeIndex)) {
                currentNodeWorldTransform = nodeWorldTransforms[*parentNodeIndex] * currentNodeWorldTransform;
            }

            algorithm::traverseNode(*pAsset, nodeIndex, [this](std::size_t nodeIndex, const glm::mat4 &nodeWorldTransform) {
                nodeWorldTransforms[nodeIndex] = nodeWorldTransform;
            }, currentNodeWorldTransform);
        }

    private:
        [[nodiscard]] std::vector<std::size_t> createParentNodeIndices() const noexcept {
            std::vector<std::size_t> result { std::from_range, std::views::iota(std::size_t { 0 }, pAsset->nodes.size()) };
            for (const auto &[i, node] : pAsset->nodes | ranges::views::enumerate) {
                for (std::size_t childIndex : node.children) {
                    result[childIndex] = i;
                }
            }
            return result;
        }

        [[nodiscard]] std::vector<glm::mat4> createNodeWorldTransforms(const fastgltf::Scene &scene) const noexcept {
            std::vector<glm::mat4> result(pAsset->nodes.size());
            algorithm::traverseScene(*pAsset, scene, [&](std::size_t nodeIndex, const glm::mat4 &nodeWorldTransform) {
                result[nodeIndex] = nodeWorldTransform;
            });
            return result;
        }
    };
}