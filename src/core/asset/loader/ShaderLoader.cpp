#include "core/asset/loader/ShaderLoader.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <filesystem>
//#include <Windows.h>

std::vector<char> ShaderLoader::readFile(const std::string& filepath) {
	std::ifstream file(filepath, std::ios::ate | std::ios::binary);
	if (!file.is_open()) {
		/*void* stack[100];
		unsigned short frames = CaptureStackBackTrace(0, 100, stack, NULL);
		std::cout << "Captured " << frames << " frames.\n";*/
		std::cout << "CWD: " << std::filesystem::current_path().string() << std::endl;
		throw std::runtime_error("Failed to open shader file: " + filepath);
	}
	size_t filesize = static_cast<size_t>(file.tellg());
	std::vector<char> buffer(filesize);
	file.seekg(0);
	file.read(buffer.data(), filesize);
	file.close();
	return buffer;
}

ShaderLoader::ShaderLoader(VulkanContext& context)
	: mContext(context)
{
}

std::unique_ptr<Shader> ShaderLoader::loadShader(const std::string& filepath)
{
	try {
		std::vector<char> code = readFile(filepath);
		return std::make_unique<Shader>(mContext, code, filepath);
	} catch (const std::exception& e) {
		std::cerr << "ShaderLoader: failed to load " << filepath << ": " << e.what() << std::endl;
		return nullptr;
	}
}
