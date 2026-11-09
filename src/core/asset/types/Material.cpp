#include "core/asset/types/Material.h"
#include "core/asset/loader/ShaderLoader.h"

#include <vk_mem_alloc.h>
#include <stdexcept>
#include <filesystem>
#include <iostream>

Material::Material(VulkanContext& context)
    : mContext(context)
{
    // Initialize material data with defaults
    mMaterialData.albedo[0] = 0.8f;
    mMaterialData.albedo[1] = 0.6f;
    mMaterialData.albedo[2] = 0.4f;
    mMaterialData.albedo[3] = 1.0f;
    mMaterialData.metallic = 0.0f;
    mMaterialData.roughness = 0.5f;
    mMaterialData.ao = 1.0f;
    mMaterialData.useAlbedoMap = 0;
    mMaterialData.useNormalMap = 0;
    mMaterialData.useMetallicMap = 0;
    mMaterialData.useRoughnessMap = 0;
    mMaterialData.useAoMap = 0;
    
    // Create material UBO buffer
    mMaterialUBO = std::make_unique<VulkanBuffer>(
        context,
        &mMaterialData,
        sizeof(MaterialUBO),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
    );
    
    // Load default PBR shaders
	std::cout << "CWD: " << std::filesystem::current_path().string() << std::endl;
    std::vector<char> vertCode = ShaderLoader::readFile("spv/pbr.vert.spv");
    std::vector<char> fragCode = ShaderLoader::readFile("spv/pbr.frag.spv");
    mVertexShader = std::make_unique<VulkanShaderModule>(context, vertCode);
    mFragmentShader = std::make_unique<VulkanShaderModule>(context, fragCode);
}

Material::Material(VulkanContext& context, const MaterialPaths& paths)
    : Material(context)
{
    // Store paths for later use (AssetManager can load textures from these paths)
    // The actual texture loading will be handled by AssetManager
    // This constructor initializes the material with default values;
    // textures can be loaded and set via setAlbedoMap(), setNormalMap(), etc.
}

void Material::setAlbedoMap(std::unique_ptr<Texture> texture)
{
    mAlbedoMap = std::move(texture);
    mMaterialData.useAlbedoMap = mAlbedoMap != nullptr ? 1 : 0;
    updateUBO();
}

void Material::setNormalMap(std::unique_ptr<Texture> texture)
{
    mNormalMap = std::move(texture);
    mMaterialData.useNormalMap = mNormalMap != nullptr ? 1 : 0;
    updateUBO();
}

void Material::setMetallicMap(std::unique_ptr<Texture> texture)
{
    mMetallicMap = std::move(texture);
    mMaterialData.useMetallicMap = mMetallicMap != nullptr ? 1 : 0;
    updateUBO();
}

void Material::setRoughnessMap(std::unique_ptr<Texture> texture)
{
    mRoughnessMap = std::move(texture);
    mMaterialData.useRoughnessMap = mRoughnessMap != nullptr ? 1 : 0;
    updateUBO();
}

void Material::setAoMap(std::unique_ptr<Texture> texture)
{
    mAoMap = std::move(texture);
    mMaterialData.useAoMap = mAoMap != nullptr ? 1 : 0;
    updateUBO();
}

void Material::setAlbedo(float r, float g, float b, float a)
{
    mMaterialData.albedo[0] = r;
    mMaterialData.albedo[1] = g;
    mMaterialData.albedo[2] = b;
    mMaterialData.albedo[3] = a;
    updateUBO();
}

void Material::setMetallic(float metallic)
{
    mMaterialData.metallic = metallic;
    updateUBO();
}

void Material::setRoughness(float roughness)
{
    mMaterialData.roughness = roughness;
    updateUBO();
}

void Material::setAo(float ao)
{
    mMaterialData.ao = ao;
    updateUBO();
}

void Material::setDescriptorSet(std::unique_ptr<VulkanDescriptorSet> descriptorSet)
{
    mDescriptorSet = std::move(descriptorSet);
}

void Material::updateUBO()
{
    mMaterialUBO->upload(&mMaterialData, sizeof(MaterialUBO));
}
