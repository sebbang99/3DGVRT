/*
 * Sogang Univ, Graphics Lab, 2024
 *
 * Abura Soba, 2025
 *
 * Camera(Euler Angle)
 *
 */
#pragma once

//#define GLM_FORCE_RADIANS
//#define GLM_FORCE_DEPTH_ZERO_TO_ONE
//#include <glm/glm.hpp>
//#include <glm/gtc/quaternion.hpp>
//#include <glm/gtc/matrix_transform.hpp>
//#include "Define.h"

#include "VulkanUtils.h"
#include "VulkanTools.h"

#define DIR_PATH "VulkanFullRT/"

class BilateralFiltering {
private:
	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout pipelineLayout;
	VkPipeline pipeline;
	VkPipelineCache pipelineCache{ VK_NULL_HANDLE };

	uint32_t kernelSize = 5;
	float sigmaSpace = 1.0f;
	float sigmaZ = 0.01;

	VkDevice device;
	VkDescriptorPool descriptorPool;

	std::vector<VkShaderModule> shaderModules;

	VkPipelineShaderStageCreateInfo loadShader(std::string fileName, VkShaderStageFlagBits stage)
	{
		VkPipelineShaderStageCreateInfo shaderStage = {};
		shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStage.stage = stage;
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
		shaderStage.module = vks::tools::loadShader(androidApp->activity->assetManager, fileName.c_str(), device);
#else
		shaderStage.module = vks::tools::loadShader(fileName.c_str(), device);
#endif
		shaderStage.pName = "main";
		assert(shaderStage.module != VK_NULL_HANDLE);
		shaderModules.push_back(shaderStage.module);
		return shaderStage;
	}
	std::string shaderDir = "glsl";
	std::string getShadersPath() const
	{
		return getShaderBasePath() + shaderDir + "/";
	}

public:
	void createGaussianEnclosingDescriptorSets()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			// gaussianEnclosing pipeline
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2), // input, output
		};
		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 1); // gaussianEnclosing pipeline

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));	// descriptor pool

		// for gaussianEnclosing pipeline begin
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0: Ray tracing result image (input)
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 1),
			// Binding 1: Bilateral filtering result image (output)
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 1),
		};

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout));

		VkDescriptorSet& descriptorSet = descriptorSet;
		VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &descriptorSet));

		VkDescriptorImageInfo storageImageDescriptor = { VK_NULL_HANDLE, swapChain.buffers[frame.imageIndex].view, VK_IMAGE_LAYOUT_GENERAL };

		std::vector<VkWriteDescriptorSet> computeWriteDescriptorSets = {
			// Binding 0: Ray tracing result image
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, &storageImageDescriptor),
			// Binding 1: Ray tracing result image
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor),
		};

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(computeWriteDescriptorSets.size()), computeWriteDescriptorSets.data(), 0, nullptr);
		// for gaussianEnclosing pipeline end
	}

	void createGaussianEnclosingPipeline()
	{
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipelineLayout, 0);
		computePipelineCreateInfo.stage = loadShader(getShadersPath() + DIR_PATH + "particlePrimitives.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

		VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &pipeline));
	}

	void computeGaussianEnclosingIcosaHedron()
	{
		// gaussianEnclosing pipeline
		buildGaussianEnclosingCommandBuffer();

		VkSubmitInfo computeSubmitInfo = vks::initializers::submitInfo();
		computeSubmitInfo.commandBufferCount = 1;
		computeSubmitInfo.pCommandBuffers = &gaussianEnclosing.commandBuffer;
		VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &computeSubmitInfo, VK_NULL_HANDLE));

		vkDeviceWaitIdle(device);

		//gModel.positions.storageBuffer.destroy();
		//gModel.rotations.storageBuffer.destroy();
		//gModel.scales.storageBuffer.destroy();
		//gModel.densities.storageBuffer.destroy();
	}
};