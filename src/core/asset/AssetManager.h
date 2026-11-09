#pragma once

#include "core/asset/loader/ImageLoader.h"
#include "core/asset/loader/MeshLoader.h"
#include "core/asset/loader/MaterialLoader.h"
#include "core/asset/loader/SceneLoader.h"
#include "core/asset/loader/ShaderLoader.h"
#include "core/asset/types/Mesh.h"
#include "core/asset/types/Material.h"
#include "core/asset/types/Scene.h"
#include "core/asset/types/Shader.h"
#include "platform/graphics/vulkan/VulkanContext.h"
#include "platform/graphics/vulkan/VulkanDescriptorPool.h"
#include "platform/graphics/vulkan/VulkanDescriptorSetLayout.h"
#include "platform/graphics/vulkan/VulkanImage.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using MeshId = uint32_t;
static constexpr MeshId InvalidMeshId = 0;

using MaterialId = uint32_t;
static constexpr MaterialId InvalidMaterialId = 0;

using ShaderId = uint32_t;
static constexpr ShaderId InvalidShaderId = 0;

/**
 * AssetManager handles loading of .obj files that include .mtl (material) files.
 * OBJ and MTL are loaded via MeshLoader; MTL base path is resolved from the OBJ
 * directory so referenced materials and textures are found correctly.
 * Meshes are stored internally; only meshIDs are handed out. Use getMesh() to
 * access a mesh by ID for rendering.
 */
class AssetManager {
public:
	explicit AssetManager(VulkanContext& context);
	~AssetManager();

	// Non-copyable
	AssetManager(const AssetManager&) = delete;
	AssetManager& operator=(const AssetManager&) = delete;

	/**
	 * Load an OBJ file (and its referenced .mtl). Returns one meshID per shape.
	 * Returns empty vector on failure.
	 */
	std::vector<MeshId> loadObj(const std::string& filepath);

	/**
	 * Load an OBJ file and combine all shapes into a single mesh.
	 * Returns InvalidMeshId on failure.
	 */
	MeshId loadObjCombined(const std::string& filepath);

	/**
	 * Try loading from multiple paths (e.g. relative to executable vs build dir).
	 * Fills outMeshIds and outLoadedPath on success; clears them on failure.
	 * Returns true if any path succeeded, false otherwise.
	 */
	bool loadObjFromPaths(const std::vector<std::string>& pathsToTry,
		std::vector<MeshId>& outMeshIds,
		std::string& outLoadedPath);

	/**
	 * Load a glTF 2.0 .glb scene via SceneLoader.
	 * \param filepath Path to the .glb file.
	 * \return The parsed Scene, or std::nullopt on parse error or if the file has no default scene.
	 */
	std::optional<Scene> loadScene(const std::string& filepath);

	/** Returns the Vulkan context (e.g. for creating meshes/materials from glTF). */
	VulkanContext& context() { return mContext; }
	const VulkanContext& context() const { return mContext; }

	ImageLoader& imageLoader() { return m_imageLoader; }
	const ImageLoader& imageLoader() const { return m_imageLoader; }

	/**
	 * Register an externally-created mesh (e.g. from glTF). Returns the new MeshId.
	 */
	MeshId addMesh(std::unique_ptr<Mesh> mesh);

	/**
	 * Register an externally-created material (e.g. default material for glTF). Returns the new MaterialId.
	 */
	MaterialId addMaterial(std::unique_ptr<Material> material);

	/**
	 * Get mesh by ID for rendering. Returns nullptr if ID is invalid or not found.
	 */
	Mesh* getMesh(MeshId id);
	const Mesh* getMesh(MeshId id) const;

	/**
	 * Get material by ID. Returns nullptr if ID is invalid or not found.
	 */
	Material* getMaterial(MaterialId id);
	const Material* getMaterial(MaterialId id) const;

	/**
	 * Get shader by ID. Returns nullptr if ID is invalid or not found.
	 */
	Shader* getShader(ShaderId id);
	const Shader* getShader(ShaderId id) const;

	/**
	 * Load a shader from file. Returns InvalidShaderId on failure.
	 */
	ShaderId loadShader(const std::string& filepath);

	/**
	 * Load materials from an OBJ file's MTL using MaterialLoader.
	 * Extracts material paths from the OBJ file and loads them.
	 * Returns a vector of MaterialIds, one per material loaded.
	 * Returns empty vector on failure.
	 */
	std::vector<MaterialId> loadMaterials(const std::string& filepath);

	/**
	 * Create and own a default sampler, descriptor pool for material descriptor sets (5 image samplers + 1 UBO per set),
	 * then allocate and write one descriptor set per material.
	 * Pool size is based on the number of materials in m_materialMap.
	 */
	void updateMaterialDescriptorSets(VulkanDescriptorSetLayout& materialDescriptorLayout);

	/**
	 * Get the material descriptor pool. Valid after updateMaterialDescriptorSets() has been called.
	 */
	VulkanDescriptorPool* materialDescriptorPool() { return m_materialDescriptorPool.get(); }
	const VulkanDescriptorPool* materialDescriptorPool() const { return m_materialDescriptorPool.get(); }

	/** Log all loaded meshes, materials, and shaders to stdout. */
	void logAssets() const;

private:
	MeshId nextMeshId();
	MaterialId nextMaterialId();
	ShaderId nextShaderId();

	VulkanContext& mContext;
	ImageLoader m_imageLoader;
	MeshLoader mMeshLoader;
	MaterialLoader mMaterialLoader;
	ShaderLoader mShaderLoader;
	std::unique_ptr<VulkanImage> m_defaultTexture;
	std::unordered_map<MeshId, std::unique_ptr<Mesh>> m_meshMap;
	std::unordered_map<MaterialId, std::unique_ptr<Material>> m_materialMap;
	std::unordered_map<ShaderId, std::unique_ptr<Shader>> m_shaderMap;
	MeshId m_nextMeshId{ 1 };
	MaterialId m_nextMaterialId{ 1 };
	ShaderId m_nextShaderId{ 1 };
	std::unique_ptr<VulkanDescriptorPool> m_materialDescriptorPool;
	VkSampler m_defaultSampler = VK_NULL_HANDLE;
};
