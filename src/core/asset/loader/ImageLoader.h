#pragma once

#include "platform/graphics/vulkan/VulkanContext.h"
#include "platform/graphics/vulkan/VulkanImage.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vulkan/vulkan.h>

// Structure containing image, image view, and sampler for a loaded texture
struct LoadedImage {
	std::unique_ptr<VulkanImage> image;
	VkImageView imageView;
	VkSampler sampler;
	VkDevice device;  // Needed for cleanup

	LoadedImage() : imageView(VK_NULL_HANDLE), sampler(VK_NULL_HANDLE), device(VK_NULL_HANDLE) {}
	
	// Move constructor
	LoadedImage(LoadedImage&& other) noexcept
		: image(std::move(other.image))
		, imageView(other.imageView)
		, sampler(other.sampler)
		, device(other.device)
	{
		other.imageView = VK_NULL_HANDLE;
		other.sampler = VK_NULL_HANDLE;
		other.device = VK_NULL_HANDLE;
	}
	
	// Move assignment
	LoadedImage& operator=(LoadedImage&& other) noexcept {
		if (this != &other) {
			// Clean up existing sampler
			if (sampler != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
				vkDestroySampler(device, sampler, nullptr);
			}
			image = std::move(other.image);
			imageView = other.imageView;
			sampler = other.sampler;
			device = other.device;
			other.imageView = VK_NULL_HANDLE;
			other.sampler = VK_NULL_HANDLE;
			other.device = VK_NULL_HANDLE;
		}
		return *this;
	}
	
	// Non-copyable
	LoadedImage(const LoadedImage&) = delete;
	LoadedImage& operator=(const LoadedImage&) = delete;
	
	// Destructor - destroys sampler if valid
	~LoadedImage() {
		// Image and imageView are destroyed by VulkanImage destructor
		// Only need to destroy sampler if it was created
		if (sampler != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
			vkDestroySampler(device, sampler, nullptr);
		}
	}
};

class ImageLoader {
public:
	explicit ImageLoader(VulkanContext& context);
	~ImageLoader() = default;

	// Non-copyable
	ImageLoader(const ImageLoader&) = delete;
	ImageLoader& operator=(const ImageLoader&) = delete;

	/**
	 * Load an image file using stb_image and create a VulkanImage with ImageView and Sampler.
	 * Returns empty struct (with nullptr image) on failure.
	 */
	LoadedImage loadImage(const std::string& filepath);

	/**
	 * Decode embedded image bytes (e.g. from glTF buffer views) and upload to the GPU.
	 * \a debugLabel is used only for logging.
	 */
	LoadedImage loadImageFromMemory(const uint8_t* data, std::size_t size, const std::string& debugLabel = {},
		VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

	/** glTF metallic-roughness combined map: metallic = B channel, roughness = G channel (replicated to R for sampling). */
	struct MetallicRoughnessImages {
		std::unique_ptr<VulkanImage> metallic;
		std::unique_ptr<VulkanImage> roughness;
	};
	MetallicRoughnessImages loadMetallicRoughnessFromMemory(
		const uint8_t* data, std::size_t size, const std::string& debugLabel = {});

	/**
	 * Create a 1x1 white texture (RGBA = 1.0, 1.0, 1.0, 1.0).
	 * Returns nullptr on failure.
	 */
	std::unique_ptr<VulkanImage> createWhiteTexture();

private:
	LoadedImage uploadRgbaPixels(int width, int height, const uint8_t* rgbaPixels, const std::string& debugLabel,
		VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
	std::unique_ptr<VulkanImage> uploadRgbaPixelsToImage(int width, int height, const uint8_t* rgbaPixels,
		VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
	VkSampler createSampler(VulkanContext& context);

	VulkanContext& mContext;
};
