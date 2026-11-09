#include "core/asset/loader/SceneLoader.h"
#include "core/asset/AssetManager.h"
#include "core/asset/loader/ImageLoader.h"
#include "core/asset/types/Mesh.h"
#include "core/asset/types/Material.h"
#include "core/asset/types/Texture.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct EncodedImageBytes {
	const uint8_t* data = nullptr;
	std::size_t size = 0;
};

std::optional<EncodedImageBytes> bufferDataSpan(const fastgltf::Asset& asset, std::size_t bufferIndex) {
	if (bufferIndex >= asset.buffers.size())
		return std::nullopt;
	const auto& buffer = asset.buffers[bufferIndex];
	return std::visit(fastgltf::visitor{
		[](const std::monostate&) -> std::optional<EncodedImageBytes> { return std::nullopt; },
		[](const fastgltf::sources::BufferView&) -> std::optional<EncodedImageBytes> { return std::nullopt; },
		[](const fastgltf::sources::URI&) -> std::optional<EncodedImageBytes> { return std::nullopt; },
		[](const fastgltf::sources::CustomBuffer&) -> std::optional<EncodedImageBytes> { return std::nullopt; },
		[](const fastgltf::sources::Fallback&) -> std::optional<EncodedImageBytes> { return std::nullopt; },
		[](const fastgltf::sources::Array& arr) -> std::optional<EncodedImageBytes> {
			return EncodedImageBytes{
				reinterpret_cast<const uint8_t*>(arr.bytes.data()), arr.bytes.size()};
		},
		[](const fastgltf::sources::Vector& vec) -> std::optional<EncodedImageBytes> {
			return EncodedImageBytes{
				reinterpret_cast<const uint8_t*>(vec.bytes.data()), vec.bytes.size()};
		},
		[](const fastgltf::sources::ByteView& view) -> std::optional<EncodedImageBytes> {
			return EncodedImageBytes{
				reinterpret_cast<const uint8_t*>(view.bytes.data()), view.bytes.size()};
		},
	}, buffer.data);
}

std::optional<EncodedImageBytes> encodedGltfImageBytes(const fastgltf::Asset& asset, std::size_t imageIndex) {
	if (imageIndex >= asset.images.size())
		return std::nullopt;
	const auto& image = asset.images[imageIndex];
	return std::visit(fastgltf::visitor{
		[](const std::monostate&) -> std::optional<EncodedImageBytes> { return std::nullopt; },
		[&](const fastgltf::sources::BufferView& view) -> std::optional<EncodedImageBytes> {
			if (view.bufferViewIndex >= asset.bufferViews.size())
				return std::nullopt;
			const auto& bufferView = asset.bufferViews[view.bufferViewIndex];
			auto bufferSpan = bufferDataSpan(asset, bufferView.bufferIndex);
			if (!bufferSpan)
				return std::nullopt;
			const std::size_t end = bufferView.byteOffset + bufferView.byteLength;
			if (end > bufferSpan->size)
				return std::nullopt;
			return EncodedImageBytes{bufferSpan->data + bufferView.byteOffset, bufferView.byteLength};
		},
		[](const fastgltf::sources::Array& arr) -> std::optional<EncodedImageBytes> {
			return EncodedImageBytes{
				reinterpret_cast<const uint8_t*>(arr.bytes.data()), arr.bytes.size()};
		},
		[](const fastgltf::sources::Vector& vec) -> std::optional<EncodedImageBytes> {
			return EncodedImageBytes{
				reinterpret_cast<const uint8_t*>(vec.bytes.data()), vec.bytes.size()};
		},
		[](const fastgltf::sources::ByteView& view) -> std::optional<EncodedImageBytes> {
			return EncodedImageBytes{
				reinterpret_cast<const uint8_t*>(view.bytes.data()), view.bytes.size()};
		},
		[](const fastgltf::sources::URI&) -> std::optional<EncodedImageBytes> { return std::nullopt; },
		[](const fastgltf::sources::CustomBuffer&) -> std::optional<EncodedImageBytes> { return std::nullopt; },
		[](const fastgltf::sources::Fallback&) -> std::optional<EncodedImageBytes> { return std::nullopt; },
	}, image.data);
}

std::optional<std::size_t> gltfTextureImageIndex(const fastgltf::Asset& asset, std::size_t textureIndex) {
	if (textureIndex >= asset.textures.size())
		return std::nullopt;
	const auto& texture = asset.textures[textureIndex];
	if (!texture.imageIndex.has_value())
		return std::nullopt;
	return *texture.imageIndex;
}

std::string gltfImageLabel(const fastgltf::Asset& asset, std::size_t imageIndex, std::string_view role) {
	std::string label = std::string(role) + "#" + std::to_string(imageIndex);
	if (imageIndex < asset.images.size() && !asset.images[imageIndex].name.empty())
		label = asset.images[imageIndex].name;
	return label;
}

std::unique_ptr<Texture> loadGltfImageTexture(
	VulkanContext& ctx,
	ImageLoader& imageLoader,
	const fastgltf::Asset& asset,
	std::size_t imageIndex,
	std::string_view role,
	VkFormat format)
{
	auto bytes = encodedGltfImageBytes(asset, imageIndex);
	if (!bytes)
		return nullptr;
	LoadedImage loaded = imageLoader.loadImageFromMemory(
		bytes->data, bytes->size, gltfImageLabel(asset, imageIndex, role), format);
	if (!loaded.image)
		return nullptr;
	return std::make_unique<Texture>(ctx, std::move(loaded.image), gltfImageLabel(asset, imageIndex, role));
}

void applyGltfMaterialTextures(
	Material& material,
	const fastgltf::Material& gltfMat,
	const fastgltf::Asset& asset,
	VulkanContext& ctx,
	ImageLoader& imageLoader)
{
	const auto& pbr = gltfMat.pbrData;

	auto assignTexture = [&](std::size_t textureIndex, auto setter, std::string_view role, VkFormat format) {
		const auto imageIndex = gltfTextureImageIndex(asset, textureIndex);
		if (!imageIndex)
			return;
		if (auto texture = loadGltfImageTexture(ctx, imageLoader, asset, *imageIndex, role, format))
			setter(std::move(texture));
	};

	if (pbr.baseColorTexture.has_value())
		assignTexture(pbr.baseColorTexture->textureIndex,
			[&](std::unique_ptr<Texture> tex) { material.setAlbedoMap(std::move(tex)); },
			"baseColor", VK_FORMAT_R8G8B8A8_SRGB);

	if (pbr.metallicRoughnessTexture.has_value()) {
		const auto imageIndex = gltfTextureImageIndex(asset, pbr.metallicRoughnessTexture->textureIndex);
		if (imageIndex) {
			auto bytes = encodedGltfImageBytes(asset, *imageIndex);
			if (bytes) {
				const std::string label = gltfImageLabel(asset, *imageIndex, "metallicRoughness");
				auto split = imageLoader.loadMetallicRoughnessFromMemory(bytes->data, bytes->size, label);
				if (split.metallic)
					material.setMetallicMap(std::make_unique<Texture>(ctx, std::move(split.metallic), label + ".metallic"));
				if (split.roughness)
					material.setRoughnessMap(std::make_unique<Texture>(ctx, std::move(split.roughness), label + ".roughness"));
			}
		}
	}

	if (gltfMat.normalTexture.has_value())
		assignTexture(gltfMat.normalTexture->textureIndex,
			[&](std::unique_ptr<Texture> tex) { material.setNormalMap(std::move(tex)); },
			"normal", VK_FORMAT_R8G8B8A8_UNORM);

	if (gltfMat.occlusionTexture.has_value())
		assignTexture(gltfMat.occlusionTexture->textureIndex,
			[&](std::unique_ptr<Texture> tex) { material.setAoMap(std::move(tex)); },
			"occlusion", VK_FORMAT_R8G8B8A8_UNORM);
}

// Vertex layout matching MeshLoader (pos, normal, texCoord, tangent, bitangent).
struct Vertex {
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 texCoord;
	glm::vec3 tangent;
	glm::vec3 bitangent;
};

// Convert fastgltf 4x4 matrix (column-major) to glm::mat4.
glm::mat4 toGlmMat4(const fastgltf::math::fmat4x4& m) {
	glm::mat4 out;
	std::memcpy(&out[0][0], &m[0][0], 16 * sizeof(float));
	return out;
}

// Build local transform from glTF node. Node.transform is variant<TRS, fmat4x4>.
glm::mat4 getLocalMatrix(const fastgltf::Asset&, const fastgltf::Node& fgNode) {
	return std::visit(
		[](auto&& arg) -> glm::mat4 {
			using T = std::decay_t<decltype(arg)>;
			if constexpr (std::is_same_v<T, fastgltf::math::fmat4x4>) {
				return toGlmMat4(arg);
			} else {
				// TRS: T * R * S
				const fastgltf::TRS& trs = arg;
				glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(trs.translation[0], trs.translation[1], trs.translation[2]));
				glm::quat q(trs.rotation.w(), trs.rotation.x(), trs.rotation.y(), trs.rotation.z()); // fastgltf xyzw -> glm wxyz
				glm::mat4 r = glm::mat4_cast(q);
				glm::mat4 s = glm::scale(glm::mat4(1.0f), glm::vec3(trs.scale[0], trs.scale[1], trs.scale[2]));
				return t * r * s;
			}
		},
		fgNode.transform);
}

// Optional: when non-null, node meshIds/materialIds are AssetManager IDs from these maps.
// When null, node IDs are glTF indices.
struct AssetIdMaps {
	const std::vector<std::vector<uint32_t>>* meshIdsByGltfMeshAndPrimitive = nullptr;
	const std::vector<uint32_t>* materialIdByGltfIndex = nullptr;
	uint32_t defaultMaterialId = 0;
};

// Convert one fastgltf node to our Node (only if it has mesh and thus materials).
// If idMaps is set, meshIds/materialIds are AssetManager IDs; otherwise glTF indices.
std::optional<::Node> convertNode(
	const fastgltf::Asset& asset,
	size_t nodeIndex,
	const std::vector<fastgltf::Node>& nodes,
	const std::vector<fastgltf::Mesh>& meshes,
	const AssetIdMaps* idMaps)
{
	if (nodeIndex >= nodes.size())
		return std::nullopt;

	const fastgltf::Node& fgNode = nodes[nodeIndex];

	if (!fgNode.meshIndex.has_value())
		return std::nullopt;

	size_t meshIdx = *fgNode.meshIndex;
	if (meshIdx >= meshes.size())
		return std::nullopt;

	const fastgltf::Mesh& mesh = meshes[meshIdx];
	if (mesh.primitives.empty())
		return std::nullopt;

	::Node out;
	out.transform = getLocalMatrix(asset, fgNode);

	for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
		const auto& prim = mesh.primitives[primIdx];
		if (idMaps && idMaps->meshIdsByGltfMeshAndPrimitive
			&& meshIdx < idMaps->meshIdsByGltfMeshAndPrimitive->size()
			&& primIdx < (*idMaps->meshIdsByGltfMeshAndPrimitive)[meshIdx].size()) {
			uint32_t meshId = (*idMaps->meshIdsByGltfMeshAndPrimitive)[meshIdx][primIdx];
			if (meshId == InvalidMeshId)
				continue;
			out.meshIds.push_back(meshId);
			out.materialIds.push_back(prim.materialIndex.has_value()
				&& prim.materialIndex.value() < idMaps->materialIdByGltfIndex->size()
				? (*idMaps->materialIdByGltfIndex)[prim.materialIndex.value()]
				: idMaps->defaultMaterialId);
		} else {
			out.meshIds.push_back(static_cast<uint32_t>(meshIdx));
			out.materialIds.push_back(prim.materialIndex.has_value()
				? static_cast<uint32_t>(*prim.materialIndex)
				: 0u);
		}
	}

	if (out.meshIds.empty())
		return std::nullopt;

	for (size_t childIdx : fgNode.children) {
		auto childOpt = convertNode(asset, childIdx, nodes, meshes, idMaps);
		if (childOpt.has_value())
			out.children.push_back(std::move(*childOpt));
	}

	return out;
}

} // namespace

std::optional<Scene> SceneLoader::loadGLB(AssetManager* assetManager, const std::filesystem::path& path) {
	auto bufferResult = fastgltf::GltfDataBuffer::FromPath(path);
	if (!bufferResult)
		return std::nullopt;

	fastgltf::Parser parser(fastgltf::Extensions::None);
	constexpr auto gltfLoadOptions = fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;
	auto assetResult = parser.loadGltf(bufferResult.get(), path.parent_path(),
		gltfLoadOptions, fastgltf::Category::OnlyRenderable);

	if (!assetResult)
		return std::nullopt;

	fastgltf::Asset& asset = assetResult.get();

	if (asset.scenes.empty())
		return std::nullopt;

	size_t sceneIndex = asset.defaultScene.has_value()
		? static_cast<size_t>(*asset.defaultScene)
		: 0u;
	if (sceneIndex >= asset.scenes.size())
		sceneIndex = 0;

	std::vector<std::vector<uint32_t>> meshIdsByGltfMeshAndPrimitive;
	std::vector<uint32_t> materialIdByGltfIndex;
	uint32_t defaultMaterialId = 0;
	AssetIdMaps idMaps;

	if (assetManager) {
		VulkanContext& ctx = assetManager->context();

		// Default material for primitives without a material.
		auto defaultMat = std::make_unique<Material>(ctx);
		defaultMat->setAlbedo(0.6f, 0.6f, 0.6f, 1.0f);
		defaultMat->setMetallic(0.5f);
		defaultMat->setRoughness(0.5f);
		defaultMaterialId = assetManager->addMaterial(std::move(defaultMat));

		ImageLoader& imageLoader = assetManager->imageLoader();

		// One Material per glTF material (PBR factors + embedded textures).
		for (const auto& gltfMat : asset.materials) {
			auto mat = std::make_unique<Material>(ctx);
			const auto& pbr = gltfMat.pbrData;
			const auto& bc = pbr.baseColorFactor;
			mat->setAlbedo(bc[0], bc[1], bc[2], bc[3]);
			mat->setMetallic(static_cast<float>(pbr.metallicFactor));
			mat->setRoughness(static_cast<float>(pbr.roughnessFactor));
			mat->setAo(1.0f);
			applyGltfMaterialTextures(*mat, gltfMat, asset, ctx, imageLoader);
			materialIdByGltfIndex.push_back(assetManager->addMaterial(std::move(mat)));
		}

		// One Mesh per glTF mesh primitive (triangles only).
		meshIdsByGltfMeshAndPrimitive.resize(asset.meshes.size());
		fastgltf::DefaultBufferDataAdapter adapter;

		for (size_t meshIdx = 0; meshIdx < asset.meshes.size(); ++meshIdx) {
			const auto& gltfMesh = asset.meshes[meshIdx];
			auto& primIds = meshIdsByGltfMeshAndPrimitive[meshIdx];

			for (const auto& prim : gltfMesh.primitives) {
				if (prim.type != fastgltf::PrimitiveType::Triangles) {
					primIds.push_back(InvalidMeshId);
					continue;
				}

				auto posIt = prim.findAttribute("POSITION");
				if (posIt == prim.attributes.cend()) {
					primIds.push_back(InvalidMeshId);
					continue;
				}
				const fastgltf::Accessor& posAccessor = asset.accessors[posIt->accessorIndex];
				if (posAccessor.type != fastgltf::AccessorType::Vec3 || posAccessor.componentType != fastgltf::ComponentType::Float) {
					primIds.push_back(InvalidMeshId);
					continue;
				}
				const size_t vertexCount = posAccessor.count;

				std::vector<Vertex> vertices(vertexCount);
				for (size_t i = 0; i < vertexCount; ++i) {
					auto p = fastgltf::getAccessorElement<fastgltf::math::fvec3>(asset, posAccessor, i, adapter);
					vertices[i].pos = glm::vec3(p[0], p[1], p[2]);
					vertices[i].normal = glm::vec3(0, 1, 0);
					vertices[i].texCoord = glm::vec2(0, 0);
					vertices[i].tangent = glm::vec3(0, 0, 0);
					vertices[i].bitangent = glm::vec3(0, 0, 0);
				}

				auto normIt = prim.findAttribute("NORMAL");
				if (normIt != prim.attributes.cend()) {
					const fastgltf::Accessor& normAccessor = asset.accessors[normIt->accessorIndex];
					if (normAccessor.type == fastgltf::AccessorType::Vec3 && normAccessor.count >= vertexCount)
						for (size_t i = 0; i < vertexCount; ++i) {
							auto n = fastgltf::getAccessorElement<fastgltf::math::fvec3>(asset, normAccessor, i, adapter);
							vertices[i].normal = glm::vec3(n[0], n[1], n[2]);
						}
				}

				auto uvIt = prim.findAttribute("TEXCOORD_0");
				if (uvIt != prim.attributes.cend()) {
					const fastgltf::Accessor& uvAccessor = asset.accessors[uvIt->accessorIndex];
					if (uvAccessor.type == fastgltf::AccessorType::Vec2 && uvAccessor.count >= vertexCount)
						for (size_t i = 0; i < vertexCount; ++i) {
							auto uv = fastgltf::getAccessorElement<fastgltf::math::fvec2>(asset, uvAccessor, i, adapter);
							vertices[i].texCoord = glm::vec2(uv[0], uv[1]);
						}
				}

				auto tanIt = prim.findAttribute("TANGENT");
				if (tanIt != prim.attributes.cend()) {
					const fastgltf::Accessor& tanAccessor = asset.accessors[tanIt->accessorIndex];
					if (tanAccessor.type == fastgltf::AccessorType::Vec4 && tanAccessor.count >= vertexCount)
						for (size_t i = 0; i < vertexCount; ++i) {
							auto t = fastgltf::getAccessorElement<fastgltf::math::fvec4>(asset, tanAccessor, i, adapter);
							vertices[i].tangent = glm::vec3(t[0], t[1], t[2]);
						}
				}

				uint32_t indexCount = 0;
				std::vector<uint16_t> indices16;
				std::vector<uint32_t> indices32;
				VkIndexType indexType = VK_INDEX_TYPE_UINT32;
				const void* indexData = nullptr;
				VkDeviceSize indexSize = 0;

				if (prim.indicesAccessor.has_value()) {
					const fastgltf::Accessor& idxAccessor = asset.accessors[*prim.indicesAccessor];
					indexCount = static_cast<uint32_t>(idxAccessor.count);
					if (idxAccessor.componentType == fastgltf::ComponentType::UnsignedShort) {
						indices16.resize(indexCount);
						for (size_t i = 0; i < indexCount; ++i)
							indices16[i] = fastgltf::getAccessorElement<std::uint16_t>(asset, idxAccessor, i, adapter);
						indexType = VK_INDEX_TYPE_UINT16;
						indexSize = indexCount * sizeof(uint16_t);
						indexData = indices16.data();
					} else {
						indices32.resize(indexCount);
						for (size_t i = 0; i < indexCount; ++i)
							indices32[i] = fastgltf::getAccessorElement<std::uint32_t>(asset, idxAccessor, i, adapter);
						indexSize = indexCount * sizeof(uint32_t);
						indexData = indices32.data();
					}
				}

				VkDeviceSize vertexStride = sizeof(Vertex);
				VkDeviceSize vertexBufferSize = vertexStride * vertexCount;
				uint32_t vCount = static_cast<uint32_t>(vertexCount);
				std::unique_ptr<Mesh> mesh;
				if (indexCount > 0 && indexData != nullptr)
					mesh = std::make_unique<Mesh>(ctx, vertices.data(), vertexBufferSize, vCount, indexData, indexSize, indexCount, indexType);
				else
					mesh = std::make_unique<Mesh>(ctx, vertices.data(), vertexBufferSize, vCount);
				MeshId assignedId = assetManager->addMesh(std::move(mesh));
				primIds.push_back(assignedId);
			}
		}

		idMaps.meshIdsByGltfMeshAndPrimitive = &meshIdsByGltfMeshAndPrimitive;
		idMaps.materialIdByGltfIndex = &materialIdByGltfIndex;
		idMaps.defaultMaterialId = defaultMaterialId;
	}

	const fastgltf::Scene& scene = asset.scenes[sceneIndex];
	::Scene out;

	for (size_t rootNodeIndex : scene.nodeIndices) {
		auto nodeOpt = convertNode(asset, rootNodeIndex, asset.nodes, asset.meshes, assetManager ? &idMaps : nullptr);
		if (nodeOpt.has_value())
			out.rootNodes.push_back(std::move(*nodeOpt));
	}

	return out;
}

std::optional<Scene> SceneLoader::loadGLB(const std::filesystem::path& path) {
	return loadGLB(nullptr, path);
}

std::optional<Scene> SceneLoader::loadGLB(const std::string& path) {
	return loadGLB(nullptr, std::filesystem::path(path));
}
