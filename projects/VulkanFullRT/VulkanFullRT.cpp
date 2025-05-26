/*
 * Sogang Univ, Graphics Lab, 2024
 * 
 * Abura Soba, 2025
 * 
 * Full Ray Tracing
 * 
 * Pipeline Composition
 * |
 * |------ (1) Gaussian Enclosing pass : Vulkan Compute pipeline
 * |
 * |------ (2) Particle Rendering pass : Vulkan Ray tracing pipeline or Vulkan Compute pipeline
 */

#include "VulkanRTCommon.h"
#include "VulkanUtils.h"
#include "SimpleUtils.h"
#include "Vulkan3DGRTModel.h"

#define DIR_PATH "VulkanFullRT/"

class VulkanFullRT : public VulkanRTCommon
{
public:

	struct ParticleDensity {
		glm::vec3 position;
		float density;
		glm::vec4 quaternion;
		glm::vec3 scale;
		float padding;
	};
	
	struct ParticleSphCoefficient {
		glm::vec3 featuresAlbedo;
		float featuresSpecular[SPECULAR_DIMENSION];
	};

	struct ParticleVisibility {
		float particleVisibility[NUM_OF_GAUSSIANS];	// temporal size
	};

	vks::Buffer particleDensities;	//read only
	vks::Buffer particleSphCoefficients;	//read only
	vks::Buffer particleVisibility;			//write (maybe don't need)

	// For 3DGRT Model
	AccelerationStructure bottomLevelAS3DGRT{};
	AccelerationStructure topLevelAS3DGRT{};

	vks::Buffer transformBuffer3DGRT;

#if !RAY_QUERY
	std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups{};
	struct ShaderBindingTables {
		ShaderBindingTable raygen;
		ShaderBindingTable miss;
		ShaderBindingTable hit;
	} shaderBindingTables;
#endif

	vks::utils::UniformDataDynamic uniformDataDynamic;
	vks::utils::UniformDataStatic uniformDataStatic;
	vks::utils::GaussianEnclosingUniformData gaussianEnclosingUniformData;

	struct SpecializationData {
		uint32_t numOfLights = NUM_OF_LIGHTS_SUPPORTED;
		uint32_t numOfDynamicLights = NUM_OF_DYNAMIC_LIGHTS;
		uint32_t numOfStaticLights = NUM_OF_STATIC_LIGHTS;
		uint32_t staticLightOffset = STATIC_LIGHT_OFFSET;
		uint32_t windowSizeX = 1;
		uint32_t windowSizeY = 1;
	} specializationData;

	// for Particle Rendering pass
	VkPipeline pipeline{ VK_NULL_HANDLE };
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };

	struct GaussianEnclosing {
		VkPipeline pipeline{ VK_NULL_HANDLE };
		VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };

		VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
		VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
		
		vks::Buffer uniformBuffer;
		VkCommandBuffer commandBuffer{ VK_NULL_HANDLE };
	} gaussianEnclosing;

	vk3DGRT::Model gModel;

	struct FrameObject : public BaseFrameObject {
		VkDescriptorSet descriptorSet{ VK_NULL_HANDLE };
	};

	std::vector<FrameObject> frameObjects;
	std::vector<BaseFrameObject*> pBaseFrameObjects;

	VkPhysicalDeviceDescriptorIndexingFeaturesEXT physicalDeviceDescriptorIndexingFeatures{};
	VkPhysicalDeviceHostQueryResetFeaturesEXT physicalDeviceHostQueryResetFeatures{};
#if RAY_QUERY
	VkPhysicalDeviceRayQueryFeaturesKHR enabledRayQueryFeatures{};
#endif

	const uint32_t timeStampCountPerFrame = 1;

#if LOAD_GLTF
	// For Triangle Mesh (Should be renamed)
	AccelerationStructure bottomLevelAS{};
	AccelerationStructure topLevelAS{};

	struct GeometryNode {
		uint64_t vertexBufferDeviceAddress;
		uint64_t indexBufferDeviceAddress;
		int32_t textureIndexBaseColor;
		int32_t textureIndexOcclusion;
		int32_t textureIndexNormal;
		int32_t textureIndexMetallicRoughness;
		int32_t textureIndexEmissive;
		float reflectance;
		float refractance;
		float ior;
		float metallicFactor;
		float roughnessFactor;
	};
	vks::Buffer geometryNodesBuffer;

	vks::Buffer transformBuffer;

	vkglTF::Model scene;
#endif

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	typedef unsigned char byte;
#endif

	VulkanFullRT() : VulkanRTCommon()
	{
		title = "Abura Soba - Vulkan Full Ray Tracing";
		initCamera();

#if RAY_QUERY
		rayQueryOnly = true;
#endif
	}

	~VulkanFullRT()
	{
		if (device) {
			// for Gaussian Enclosing pass
			vkDestroyPipeline(device, gaussianEnclosing.pipeline, nullptr);
			vkDestroyPipelineLayout(device, gaussianEnclosing.pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, gaussianEnclosing.descriptorSetLayout, nullptr);

			// for Particle Rendering pass
			vkDestroyPipeline(device, pipeline, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

			for (FrameObject& frame : frameObjects)
			{
				frame.uniformBuffer.destroy();
				frame.uniformBufferStatic.destroy();

				vkDestroyQueryPool(device, frame.timeStampQueryPool, nullptr);
			}

			particleDensities.destroy();
			particleSphCoefficients.destroy();
			particleVisibility.destroy();

			gaussianEnclosing.uniformBuffer.destroy();
			
			deleteAccelerationStructure(bottomLevelAS3DGRT);
			deleteAccelerationStructure(topLevelAS3DGRT);
			transformBuffer3DGRT.destroy();
		
#if !RAY_QUERY
			shaderBindingTables.raygen.destroy();
			shaderBindingTables.miss.destroy();
			shaderBindingTables.hit.destroy();
#endif
			destroyCommandBuffers();

#if LOAD_GLTF
			geometryNodesBuffer.destroy();
			deleteAccelerationStructure(bottomLevelAS);
			deleteAccelerationStructure(topLevelAS);
			transformBuffer.destroy();
#endif
		}
	}

	virtual void createCommandBuffers()
	{
		VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::commandBufferAllocateInfo(cmdPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);

		for (FrameObject& frame : frameObjects)
		{
			VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &frame.commandBuffer));
		}
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &gaussianEnclosing.commandBuffer));
	}

	virtual void destroyCommandBuffers()
	{
		for (FrameObject& frame : frameObjects)
		{
			vkFreeCommandBuffers(device, cmdPool, 1, &frame.commandBuffer);
		}
		vkFreeCommandBuffers(device, cmdPool, 1, &gaussianEnclosing.commandBuffer);
	}


	void createAccelerationStructureBuffer(AccelerationStructure& accelerationStructure, VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo)
	{
		VkBufferCreateInfo bufferCreateInfo{};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = buildSizeInfo.accelerationStructureSize;
		bufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &accelerationStructure.buffer));
		VkMemoryRequirements memoryRequirements{};
		vkGetBufferMemoryRequirements(device, accelerationStructure.buffer, &memoryRequirements);
		VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
		memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
		memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
		VkMemoryAllocateInfo memoryAllocateInfo{};
		memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
		memoryAllocateInfo.allocationSize = memoryRequirements.size;
		memoryAllocateInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &accelerationStructure.memory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, accelerationStructure.buffer, accelerationStructure.memory, 0));
	}

#if LOAD_GLTF
	/*
	Create the bottom level acceleration structure that contains the scene's actual geometry (vertices, triangles)
	*/
	void createBottomLevelAccelerationStructure()
	{
		// Use transform matrices from the glTF nodes
		std::vector<VkTransformMatrixKHR> transformMatrices{};
		for (auto node : scene.linearNodes) {
			if (node->mesh) {
				for (auto primitive : node->mesh->primitives) {
					if (primitive->indexCount > 0) {
						VkTransformMatrixKHR transformMatrix{};
						//auto m = glm::mat3x4(glm::transpose(node->getMatrix()));
						auto m = glm::mat3x4(glm::mat4(1.0f));	// The code above doesn't need because of pretransform.
						memcpy(&transformMatrix, (void*)&m, sizeof(glm::mat3x4));
						transformMatrices.push_back(transformMatrix);
					}
				}
			}
		}

		// Transform buffer
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&transformBuffer,
			static_cast<uint32_t>(transformMatrices.size()) * sizeof(VkTransformMatrixKHR),
			transformMatrices.data()));

		// Build
		// One geometry per glTF node, so we can index materials using gl_GeometryIndexEXT
		std::vector<uint32_t> maxPrimitiveCounts;
		std::vector<VkAccelerationStructureGeometryKHR> geometries;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR> buildRangeInfos;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> pBuildRangeInfos;
		std::vector<GeometryNode> geometryNodes;

		for (auto node : scene.linearNodes) {
			if (node->mesh) {
				for (auto primitive : node->mesh->primitives) {
					if (primitive->indexCount > 0) {
						VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
						VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
						VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{};

						vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(scene.vertices.buffer);// +primitive->firstVertex * sizeof(vkglTF::Vertex);
						indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(scene.indices.buffer) + primitive->firstIndex * sizeof(uint32_t);
						transformBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(transformBuffer.buffer) + static_cast<uint32_t>(geometryNodes.size()) * sizeof(VkTransformMatrixKHR);

						VkAccelerationStructureGeometryKHR geometry{};
						geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
						geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
						geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
						geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
						geometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
						geometry.geometry.triangles.maxVertex = scene.vertices.count;
						geometry.geometry.triangles.vertexStride = sizeof(vkglTF::Vertex);
						geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
						geometry.geometry.triangles.indexData = indexBufferDeviceAddress;
						geometry.geometry.triangles.transformData = transformBufferDeviceAddress;
						geometries.push_back(geometry);
						maxPrimitiveCounts.push_back(primitive->indexCount / 3);

						VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
						buildRangeInfo.firstVertex = 0;
						buildRangeInfo.primitiveOffset = 0; // primitive->firstIndex * sizeof(uint32_t);
						buildRangeInfo.primitiveCount = primitive->indexCount / 3;
						buildRangeInfo.transformOffset = 0;
						buildRangeInfos.push_back(buildRangeInfo);

						GeometryNode geometryNode{};
						geometryNode.vertexBufferDeviceAddress = vertexBufferDeviceAddress.deviceAddress;
						geometryNode.indexBufferDeviceAddress = indexBufferDeviceAddress.deviceAddress;
						geometryNode.textureIndexBaseColor = primitive->material.baseColorTexture ? primitive->material.baseColorTexture->index : -1;
						geometryNode.textureIndexOcclusion = primitive->material.occlusionTexture ? primitive->material.occlusionTexture->index : -1;
						geometryNode.textureIndexNormal = primitive->material.normalTexture ? primitive->material.normalTexture->index : -1;
						geometryNode.textureIndexMetallicRoughness = primitive->material.metallicRoughnessTexture ? primitive->material.metallicRoughnessTexture->index : -1;
						geometryNode.textureIndexEmissive = primitive->material.emissiveTexture ? primitive->material.emissiveTexture->index : -1;
						geometryNode.reflectance = primitive->material.Kr;
						geometryNode.refractance = primitive->material.Kt;
						geometryNode.ior = primitive->material.ior;
						geometryNode.metallicFactor = primitive->material.metallicFactor;
						geometryNode.roughnessFactor = primitive->material.roughnessFactor;
						// @todo: map material id to global texture array
						geometryNodes.push_back(geometryNode);
					}
				}
			}
		}

		for (auto& rangeInfo : buildRangeInfos)
		{
			pBuildRangeInfos.push_back(&rangeInfo);
		}

		// @todo: stage to device

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&geometryNodesBuffer,
			static_cast<uint32_t> (geometryNodes.size() * sizeof(GeometryNode)),
			geometryNodes.data()));

		// Get size info
		VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
		accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationStructureBuildGeometryInfo.geometryCount = static_cast<uint32_t>(geometries.size());
		accelerationStructureBuildGeometryInfo.pGeometries = geometries.data();

		const uint32_t numTriangles = maxPrimitiveCounts[0];

		VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
		accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
		vkGetAccelerationStructureBuildSizesKHR(
			device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&accelerationStructureBuildGeometryInfo,
			maxPrimitiveCounts.data(),
			&accelerationStructureBuildSizesInfo);

		createAccelerationStructureBuffer(bottomLevelAS, accelerationStructureBuildSizesInfo);

		VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
		accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		accelerationStructureCreateInfo.buffer = bottomLevelAS.buffer;
		accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
		accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		vkCreateAccelerationStructureKHR(device, &accelerationStructureCreateInfo, nullptr, &bottomLevelAS.handle);

		// Create a small scratch buffer used during build of the bottom level acceleration structure
		ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);

		accelerationStructureBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		accelerationStructureBuildGeometryInfo.dstAccelerationStructure = bottomLevelAS.handle;
		accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

		// Build the acceleration structure on the device via a one-time command buffer submission
		// Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
		VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBuildAccelerationStructuresKHR(
			commandBuffer,
			1,
			&accelerationStructureBuildGeometryInfo,
			pBuildRangeInfos.data());
		vulkanDevice->flushCommandBuffer(commandBuffer, graphicsQueue);

		VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
		accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		accelerationDeviceAddressInfo.accelerationStructure = bottomLevelAS.handle;
		bottomLevelAS.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &accelerationDeviceAddressInfo);

		deleteScratchBuffer(scratchBuffer);
	}

	/*
		The top level acceleration structure contains the scene's object instances
	*/
	void createTopLevelAccelerationStructure()
	{
		VkTransformMatrixKHR transformMatrix = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f };

		std::vector<VkAccelerationStructureInstanceKHR> instances;
		VkAccelerationStructureInstanceKHR instance{};
		instance.transform = transformMatrix;
		instance.instanceCustomIndex = 0;
		instance.mask = 0xFF;
		instance.instanceShaderBindingTableRecordOffset = 0;
		instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance.accelerationStructureReference = bottomLevelAS.deviceAddress;
		instances.push_back(instance);

		// Buffer for instance data
		vks::Buffer instancesBuffer;
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&instancesBuffer,
			instances.size() * sizeof(VkAccelerationStructureInstanceKHR),
			instances.data()));

		VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
		instanceDataDeviceAddress.deviceAddress = getBufferDeviceAddress(instancesBuffer.buffer);

		VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
		accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		accelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
		accelerationStructureGeometry.geometry.instances.data = instanceDataDeviceAddress;

		// Get size info
		/*
		The pSrcAccelerationStructure, dstAccelerationStructure, and mode members of pBuildInfo are ignored. Any VkDeviceOrHostAddressKHR members of pBuildInfo are ignored by this command, except that the hostAddress member of VkAccelerationStructureGeometryTrianglesDataKHR::transformData will be examined to check if it is NULL.*
		*/
		VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
		accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationStructureBuildGeometryInfo.geometryCount = 1;
		accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

		uint32_t primitive_count = instances.size();

		VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
		accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
		vkGetAccelerationStructureBuildSizesKHR(
			device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&accelerationStructureBuildGeometryInfo,
			&primitive_count,
			&accelerationStructureBuildSizesInfo);

		createAccelerationStructureBuffer(topLevelAS, accelerationStructureBuildSizesInfo);

		VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
		accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		accelerationStructureCreateInfo.buffer = topLevelAS.buffer;
		accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
		accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		vkCreateAccelerationStructureKHR(device, &accelerationStructureCreateInfo, nullptr, &topLevelAS.handle);

		// Create a small scratch buffer used during build of the top level acceleration structure
		ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);

		VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
		accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		accelerationBuildGeometryInfo.dstAccelerationStructure = topLevelAS.handle;
		accelerationBuildGeometryInfo.geometryCount = 1;
		accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
		accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
		accelerationStructureBuildRangeInfo.primitiveCount = primitive_count;
		accelerationStructureBuildRangeInfo.primitiveOffset = 0;
		accelerationStructureBuildRangeInfo.firstVertex = 0;
		accelerationStructureBuildRangeInfo.transformOffset = 0;
		std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

		// Build the acceleration structure on the device via a one-time command buffer submission
		// Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
		VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBuildAccelerationStructuresKHR(
			commandBuffer,
			1,
			&accelerationBuildGeometryInfo,
			accelerationBuildStructureRangeInfos.data());
		vulkanDevice->flushCommandBuffer(commandBuffer, graphicsQueue);

		VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
		accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		accelerationDeviceAddressInfo.accelerationStructure = topLevelAS.handle;

		deleteScratchBuffer(scratchBuffer);
		instancesBuffer.destroy();
	}
#endif

	void createBottomLevelAccelerationStructure3DGRT()
	{
		VkTransformMatrixKHR transformMatrix{};
		auto m = glm::mat3x4(glm::mat4(1.0f));
		memcpy(&transformMatrix, (void*)&m, sizeof(glm::mat3x4));

		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&transformBuffer3DGRT, sizeof(VkTransformMatrixKHR), &transformMatrix));

		// Build
		VkAccelerationStructureGeometryKHR geometry{};
		VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo;
		VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfo;

		uint32_t primitiveCount = gModel.indices.count / 3;
		VkDeviceOrHostAddressConstKHR vertexBufferDeviceAddress{};
		VkDeviceOrHostAddressConstKHR indexBufferDeviceAddress{};
		VkDeviceOrHostAddressConstKHR transformBufferDeviceAddress{};

		vertexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(gModel.vertices.storageBuffer.buffer);
		indexBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(gModel.indices.storageBuffer.buffer);
		transformBufferDeviceAddress.deviceAddress = getBufferDeviceAddress(transformBuffer3DGRT.buffer);

		geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		geometry.geometry.triangles.vertexData = vertexBufferDeviceAddress;
		geometry.geometry.triangles.maxVertex = gModel.vertices.count;
		geometry.geometry.triangles.vertexStride = sizeof(float) * 3;
		geometry.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
		geometry.geometry.triangles.indexData = indexBufferDeviceAddress;
		geometry.geometry.triangles.transformData = transformBufferDeviceAddress;

		buildRangeInfo.firstVertex = 0;
		buildRangeInfo.primitiveOffset = 0;
		buildRangeInfo.primitiveCount = primitiveCount;
		buildRangeInfo.transformOffset = 0;

		pBuildRangeInfo = &buildRangeInfo;

		// Get size info
		VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
		accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationStructureBuildGeometryInfo.geometryCount = 1;
		accelerationStructureBuildGeometryInfo.pGeometries = &geometry;

		VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
		accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
		vkGetAccelerationStructureBuildSizesKHR(
			device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&accelerationStructureBuildGeometryInfo,
			&primitiveCount,
			&accelerationStructureBuildSizesInfo);

		createAccelerationStructureBuffer(bottomLevelAS3DGRT, accelerationStructureBuildSizesInfo);

		VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
		accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		accelerationStructureCreateInfo.buffer = bottomLevelAS3DGRT.buffer;
		accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
		accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		vkCreateAccelerationStructureKHR(device, &accelerationStructureCreateInfo, nullptr, &bottomLevelAS3DGRT.handle);

		// Create a small scratch buffer used during build of the bottom level acceleration structure
		ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);

		accelerationStructureBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		accelerationStructureBuildGeometryInfo.dstAccelerationStructure = bottomLevelAS3DGRT.handle;
		accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

		// Build the acceleration structure on the device via a one-time command buffer submission
		// Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
		VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBuildAccelerationStructuresKHR(
			commandBuffer,
			1,
			&accelerationStructureBuildGeometryInfo,
			&pBuildRangeInfo);
		vulkanDevice->flushCommandBuffer(commandBuffer, graphicsQueue);

		VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
		accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		accelerationDeviceAddressInfo.accelerationStructure = bottomLevelAS3DGRT.handle;
		bottomLevelAS3DGRT.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &accelerationDeviceAddressInfo);

		deleteScratchBuffer(scratchBuffer);
	}

	void createTopLevelAccelerationStructure3DGRT()
	{
		VkTransformMatrixKHR transformMatrix = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f };

		VkAccelerationStructureInstanceKHR instance{};
		instance.transform = transformMatrix;
		instance.instanceCustomIndex = 0;
		instance.mask = 0xFF;
		instance.instanceShaderBindingTableRecordOffset = 0;
		instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance.accelerationStructureReference = bottomLevelAS3DGRT.deviceAddress;

		// Buffer for instance data
		vks::Buffer instancesBuffer;
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&instancesBuffer,
			sizeof(VkAccelerationStructureInstanceKHR),
			&instance));

		VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
		instanceDataDeviceAddress.deviceAddress = getBufferDeviceAddress(instancesBuffer.buffer);

		VkAccelerationStructureGeometryKHR accelerationStructureGeometry{};
		accelerationStructureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		accelerationStructureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
		accelerationStructureGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
		accelerationStructureGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
		accelerationStructureGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
		accelerationStructureGeometry.geometry.instances.data = instanceDataDeviceAddress;

		// Get size info
		/*
		The pSrcAccelerationStructure, dstAccelerationStructure, and mode members of pBuildInfo are ignored. Any VkDeviceOrHostAddressKHR members of pBuildInfo are ignored by this command, except that the hostAddress member of VkAccelerationStructureGeometryTrianglesDataKHR::transformData will be examined to check if it is NULL.*
		*/
		VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
		accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationStructureBuildGeometryInfo.geometryCount = 1;
		accelerationStructureBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;

		uint32_t primitive_count = 1;

		VkAccelerationStructureBuildSizesInfoKHR accelerationStructureBuildSizesInfo{};
		accelerationStructureBuildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
		vkGetAccelerationStructureBuildSizesKHR(
			device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&accelerationStructureBuildGeometryInfo,
			&primitive_count,
			&accelerationStructureBuildSizesInfo);

		createAccelerationStructureBuffer(topLevelAS3DGRT, accelerationStructureBuildSizesInfo);

		VkAccelerationStructureCreateInfoKHR accelerationStructureCreateInfo{};
		accelerationStructureCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
		accelerationStructureCreateInfo.buffer = topLevelAS3DGRT.buffer;
		accelerationStructureCreateInfo.size = accelerationStructureBuildSizesInfo.accelerationStructureSize;
		accelerationStructureCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		vkCreateAccelerationStructureKHR(device, &accelerationStructureCreateInfo, nullptr, &topLevelAS3DGRT.handle);

		// Create a small scratch buffer used during build of the top level acceleration structure
		ScratchBuffer scratchBuffer = createScratchBuffer(accelerationStructureBuildSizesInfo.buildScratchSize);

		VkAccelerationStructureBuildGeometryInfoKHR accelerationBuildGeometryInfo{};
		accelerationBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		accelerationBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
		accelerationBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		accelerationBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		accelerationBuildGeometryInfo.dstAccelerationStructure = topLevelAS3DGRT.handle;
		accelerationBuildGeometryInfo.geometryCount = 1;
		accelerationBuildGeometryInfo.pGeometries = &accelerationStructureGeometry;
		accelerationBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

		VkAccelerationStructureBuildRangeInfoKHR accelerationStructureBuildRangeInfo{};
		accelerationStructureBuildRangeInfo.primitiveCount = primitive_count;
		accelerationStructureBuildRangeInfo.primitiveOffset = 0;
		accelerationStructureBuildRangeInfo.firstVertex = 0;
		accelerationStructureBuildRangeInfo.transformOffset = 0;
		VkAccelerationStructureBuildRangeInfoKHR* accelerationBuildStructureRangeInfo = &accelerationStructureBuildRangeInfo;

		// Build the acceleration structure on the device via a one-time command buffer submission
		// Some implementations may support acceleration structure building on the host (VkPhysicalDeviceAccelerationStructureFeaturesKHR->accelerationStructureHostCommands), but we prefer device builds
		VkCommandBuffer commandBuffer = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		vkCmdBuildAccelerationStructuresKHR(
			commandBuffer,
			1,
			&accelerationBuildGeometryInfo,
			&accelerationBuildStructureRangeInfo);
		vulkanDevice->flushCommandBuffer(commandBuffer, graphicsQueue);

		VkAccelerationStructureDeviceAddressInfoKHR accelerationDeviceAddressInfo{};
		accelerationDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		accelerationDeviceAddressInfo.accelerationStructure = topLevelAS3DGRT.handle;

		deleteScratchBuffer(scratchBuffer);
		instancesBuffer.destroy();
	}

#if !RAY_QUERY
	/*
		Create the Shader Binding Tables that binds the programs and top-level acceleration structure

		SBT Layout used in VulkanFullRT project:

			/---------------------------------------\
			| Raygen                                |
			|---------------------------------------|
			| Miss(dummy)                           |
			|---------------------------------------|
			| Closest Hit(Basic)                    |
			\---------------------------------------/
	*/
	void createShaderBindingTables() {
		const uint32_t handleSize = rayTracingPipelineProperties.shaderGroupHandleSize;
		const uint32_t handleSizeAligned = vks::tools::alignedSize(rayTracingPipelineProperties.shaderGroupHandleSize, rayTracingPipelineProperties.shaderGroupHandleAlignment);
		const uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
		const uint32_t sbtSize = groupCount * handleSizeAligned;

		std::vector<uint8_t> shaderHandleStorage(sbtSize);
		VK_CHECK_RESULT(vkGetRayTracingShaderGroupHandlesKHR(device, pipeline, 0, groupCount, sbtSize, shaderHandleStorage.data()));

		createShaderBindingTable(shaderBindingTables.raygen, 1);
		createShaderBindingTable(shaderBindingTables.miss, 1);
		createShaderBindingTable(shaderBindingTables.hit, 1);

		// Copy handles
		// Global[0], Group Local[0]: Raygen group
		memcpy(shaderBindingTables.raygen.mapped, shaderHandleStorage.data(), handleSize);
		// Global[1 ~ 2], Group Local[0 ~ 1]: Miss group
		memcpy(shaderBindingTables.miss.mapped, shaderHandleStorage.data() + handleSizeAligned, handleSize);
		// Global[2], Group Local[0]: Hit group
		memcpy(shaderBindingTables.hit.mapped, shaderHandleStorage.data() + handleSizeAligned * 2, handleSize);
	}
#endif

	/*
		Create out gaussianEnclosing pipeline
	*/
	void createGaussianEnclosingPipeline()
	{
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&gaussianEnclosing.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &gaussianEnclosing.pipelineLayout));

		VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(gaussianEnclosing.pipelineLayout, 0);
		computePipelineCreateInfo.stage = loadShader(getShadersPath() + DIR_PATH + "particlePrimitives.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

		VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &gaussianEnclosing.pipeline));
	}

#if RAY_QUERY == 1
	void createParticleRenderingPipeline()
	{
		// Specialization constants
		specializationData.windowSizeX = width;
		specializationData.windowSizeY = height;
		std::vector<VkSpecializationMapEntry> specializationMapEntries = {
			vks::initializers::specializationMapEntry(0, 0, sizeof(uint32_t)),
			vks::initializers::specializationMapEntry(1, sizeof(uint32_t), sizeof(uint32_t)),
			vks::initializers::specializationMapEntry(2, sizeof(uint32_t) * 2, sizeof(uint32_t)),
			vks::initializers::specializationMapEntry(3, sizeof(uint32_t) * 3, sizeof(uint32_t)),
			vks::initializers::specializationMapEntry(4, sizeof(uint32_t) * 4, sizeof(uint32_t)),
			vks::initializers::specializationMapEntry(5, sizeof(uint32_t) * 5, sizeof(uint32_t)),
		};
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(static_cast<uint32_t>(specializationMapEntries.size()), specializationMapEntries.data(), sizeof(SpecializationData), &specializationData);

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		// For transfer push constants
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_COMPUTE_BIT, sizeof(pushConstants), 0);
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::computePipelineCreateInfo(pipelineLayout, 0);
		computePipelineCreateInfo.stage = loadShader(getShadersPath() + DIR_PATH + "particleRendering.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);
		computePipelineCreateInfo.stage.pSpecializationInfo = &specializationInfo;

		VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &pipeline));
	}
#elif RAY_QUERY == 2
	void createParticleRenderingPipeline()
	{
		// Specialization constants
		std::vector<VkSpecializationMapEntry> specializationMapEntries = {
			vks::initializers::specializationMapEntry(0, 0, sizeof(uint32_t)),
			vks::initializers::specializationMapEntry(1, sizeof(uint32_t), sizeof(uint32_t)),
			vks::initializers::specializationMapEntry(2, sizeof(uint32_t) * 2, sizeof(uint32_t)),
			vks::initializers::specializationMapEntry(3, sizeof(uint32_t) * 3, sizeof(uint32_t)),
		};
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(static_cast<uint32_t>(specializationMapEntries.size()), specializationMapEntries.data(), sizeof(SpecializationData), &specializationData);

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		// For transfer push constants
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(pushConstants), 0);
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0] = loadShader(getShadersPath() + DIR_PATH + "particleRendering.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + DIR_PATH + "particleRendering.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
	}
#else
	void createParticleRenderingPipeline()
	{
		// For transfer of num of lights, use specialization constant.
		std::vector<VkSpecializationMapEntry> specializationMapEntries = {
			vks::initializers::specializationMapEntry(0, 0, sizeof(uint32_t)),
			vks::initializers::specializationMapEntry(1, sizeof(uint32_t), sizeof(uint32_t)),
			vks::initializers::specializationMapEntry(2, sizeof(uint32_t) * 2, sizeof(uint32_t)),
			vks::initializers::specializationMapEntry(3, sizeof(uint32_t) * 3, sizeof(uint32_t)),
		};
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(static_cast<uint32_t>(specializationMapEntries.size()), specializationMapEntries.data(), sizeof(SpecializationData), &specializationData);

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);

		// For transfer push constants
		VkPushConstantRange pushConstantRange = vks::initializers::pushConstantRange(VK_SHADER_STAGE_RAYGEN_BIT_KHR, sizeof(pushConstants), 0);
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
		/*
			Setup ray tracing shader groups
		*/
		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

		// Ray generation group
		{
			shaderStages.push_back(loadShader(getShadersPath() + DIR_PATH + "raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR));
			shaderStages[shaderStages.size() - 1].pSpecializationInfo = &specializationInfo;
			VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
			shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
			shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroups.push_back(shaderGroup);
		}

		// Miss group
		{
			shaderStages.push_back(loadShader(getShadersPath() + DIR_PATH + "miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
			VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
			shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
			shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroups.push_back(shaderGroup);
#if LOAD_GLTF
			// Second shader for shadows
			shaderStages.push_back(loadShader(getShadersPath() + DIR_PATH + "shadow.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR));
			shaderGroup.generalShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroups.push_back(shaderGroup);
#endif
		}

		// Closest hit group 0 : Basic
		{
			shaderStages.push_back(loadShader(getShadersPath() + DIR_PATH + "anyhit.rahit.spv", VK_SHADER_STAGE_ANY_HIT_BIT_KHR));
			//shaderStages[shaderStages.size() - 1].pSpecializationInfo = &specializationInfo;
			VkRayTracingShaderGroupCreateInfoKHR shaderGroup{};
			shaderGroup.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
			shaderGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
			shaderGroup.generalShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.intersectionShader = VK_SHADER_UNUSED_KHR;
			shaderGroup.anyHitShader = static_cast<uint32_t>(shaderStages.size()) - 1;
			shaderGroups.push_back(shaderGroup);
		}
		/*
			Create the ray tracing pipeline
		*/
		VkRayTracingPipelineCreateInfoKHR rayTracingPipelineCI{};
		rayTracingPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
		rayTracingPipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		rayTracingPipelineCI.pStages = shaderStages.data();
		rayTracingPipelineCI.groupCount = static_cast<uint32_t>(shaderGroups.size());
		rayTracingPipelineCI.pGroups = shaderGroups.data();
		rayTracingPipelineCI.maxPipelineRayRecursionDepth = 1;
		rayTracingPipelineCI.layout = pipelineLayout;
		VK_CHECK_RESULT(vkCreateRayTracingPipelinesKHR(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &rayTracingPipelineCI, nullptr, &pipeline));
	}
#endif

	void createDescriptorSets()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			// ray tracing pipeline
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 * swapChain.imageCount),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 * swapChain.imageCount),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * swapChain.imageCount),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * swapChain.imageCount),
		};
		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, swapChain.imageCount); // gaussianEnclosing pipeline + ray tracing pipeline

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));	// descriptor pool
		
		// for ray tracing pipeline begin
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
#if RAY_QUERY == 1
			// Binding 0: Top level acceleration structure
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_COMPUTE_BIT, 0),
			// Binding 1: Ray tracing result image
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT, 1),
			// Binding 2: Uniform buffer Dynamic
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2),
			// Binding 3: Uniform buffer Static
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 3),
			// Binding 4: Storage buffer - Particle Densities
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 4),
			// Binding 5: Storage buffer - Particle Sph Coefficients
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 5),
			// Binding 6: Storage buffer - Particle Visibility
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 6),
#elif RAY_QUERY == 2
			// Binding 0: Top level acceleration structure
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_FRAGMENT_BIT, 0),
			// Binding 1: Ray tracing result image
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
			// Binding 2: Uniform buffer Dynamic
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),
			// Binding 3: Uniform buffer Static
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),
			// Binding 4: Storage buffer - Particle Densities
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 4),
			// Binding 5: Storage buffer - Particle Sph Coefficients
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 5),
			// Binding 6: Storage buffer - Particle Visibility
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 6),
#else
			// Binding 0: Top level acceleration structure
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0),
			// Binding 1: Ray tracing result image
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 1),
			// Binding 2: Uniform buffer Dynamic
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 2),
			// Binding 3: Uniform buffer Static
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 3),
			// Binding 4: Storage buffer - Particle Densities
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 4),
			// Binding 5: Storage buffer - Particle Sph Coefficients
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 5),
			// Binding 6: Storage buffer - Particle Visibility
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 6),
#endif
		};

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &descriptorSetLayout));

		for (auto& frame : frameObjects)
		{
			VkDescriptorSet& descriptorSet = frame.descriptorSet;
			VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &descriptorSet));	// descriptor set

			// WriteDescriptorSet for TLAS (binding0)
			VkWriteDescriptorSetAccelerationStructureKHR descriptorAccelerationStructureInfo = vks::initializers::writeDescriptorSetAccelerationStructureKHR();
			descriptorAccelerationStructureInfo.accelerationStructureCount = 1;
			descriptorAccelerationStructureInfo.pAccelerationStructures = &topLevelAS3DGRT.handle;

			VkWriteDescriptorSet accelerationStructureWrite{};
			accelerationStructureWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			// The specialized acceleration structure descriptor has to be chained
			accelerationStructureWrite.pNext = &descriptorAccelerationStructureInfo;
			accelerationStructureWrite.dstSet = descriptorSet;
			accelerationStructureWrite.dstBinding = 0;
			accelerationStructureWrite.descriptorCount = 1;
			accelerationStructureWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

			VkDescriptorImageInfo storageImageDescriptor = { VK_NULL_HANDLE, swapChain.buffers[frame.imageIndex].view, VK_IMAGE_LAYOUT_GENERAL };

			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				// Binding 0: Top level acceleration structure
				accelerationStructureWrite,
				// Binding 1: Ray tracing result image
				vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor),
				// Binding 2: Uniform data Dynamic
				vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, &frame.uniformBuffer.descriptor),
				// Binding 3: Uniform data Static
				vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3, &frame.uniformBufferStatic.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4, &particleDensities.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5, &particleSphCoefficients.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6, &particleVisibility.descriptor),
			};

			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, VK_NULL_HANDLE);
		}
		// for ray tracing pipeline end
	}

	/*
		Create the descriptor sets used for the gaussianEnclosing pipeline
	*/
	void createGaussianEnclosingDescriptorSets()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = {
			// gaussianEnclosing pipeline
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6), // vertices, triangles, position, rotation, scale, density
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1),	// particle density
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3)	// particle sph coefficient
		};
		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, 1); // gaussianEnclosing pipeline

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));	// descriptor pool

		// for gaussianEnclosing pipeline begin
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0: Vertices
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 0),
			// Binding 1: Triangles
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1),
			// Binding 2: Position
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 2),
			// Binding 3: Rotation
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 3),
			// Binding 4: Scale
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 4),
			// Binding 5: Density
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 5),
			// Binding 6: Uniform Buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 6),
			// Binding 7: Particle density
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 7),
			// Binding 8: Features Albedo
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 8),
			// Binding 9: Features Specular
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 9),
			// Binding 10: Particle Sph Coefficient
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 10),
		};

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCI, nullptr, &gaussianEnclosing.descriptorSetLayout));

		VkDescriptorSet& descriptorSet = gaussianEnclosing.descriptorSet;
		VkDescriptorSetAllocateInfo descriptorSetAllocateInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &gaussianEnclosing.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocateInfo, &descriptorSet));

		std::vector<VkWriteDescriptorSet> computeWriteDescriptorSets = {
			// Binding 0: Vertices
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &gModel.vertices.storageBuffer.descriptor),
			// Binding 1: Triangles
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, &gModel.indices.storageBuffer.descriptor),
			// Binding 2: Position
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2, &gModel.positions.storageBuffer.descriptor),
			// Binding 3: Rotation
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3, &gModel.rotations.storageBuffer.descriptor),
			// Binding 4: Scale
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4, &gModel.scales.storageBuffer.descriptor),
			// Binding 5: Density
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5, &gModel.densities.storageBuffer.descriptor),
			// Binding 6: Uniform Buffer 
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 6, &gaussianEnclosing.uniformBuffer.descriptor),
			// Binding 7: Particle density
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7, &particleDensities.descriptor),
			// Binding 8: Features albedo
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8, &gModel.featuresAlbedo.storageBuffer.descriptor),
			// Binding 9: Features specular
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 9, &gModel.featuresSpecular.storageBuffer.descriptor),
			// Binding 10: Particle sph coefficient
			vks::initializers::writeDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10, &particleSphCoefficients.descriptor),
		};

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(computeWriteDescriptorSets.size()), computeWriteDescriptorSets.data(), 0, nullptr);
		// for gaussianEnclosing pipeline end
	}

	/*
		If the window has been resized, we need to recreate the storage image and it's descriptor
	*/
	void handleResize()
	{
		VK_CHECK_RESULT(vkDeviceWaitIdle(device));
		for (FrameObject& frame : frameObjects) {

			VkDescriptorImageInfo storageImageDescriptor{ VK_NULL_HANDLE, swapChain.buffers[frame.imageIndex].view, VK_IMAGE_LAYOUT_GENERAL };
			VkWriteDescriptorSet resultImageWrite = vks::initializers::writeDescriptorSet(frame.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor);
			vkUpdateDescriptorSets(device, 1, &resultImageWrite, 0, VK_NULL_HANDLE);
		}
		resized = false;
	}

	void buildGaussianEnclosingCommandBuffer()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VK_CHECK_RESULT(vkBeginCommandBuffer(gaussianEnclosing.commandBuffer, &cmdBufInfo));

		vkCmdBindPipeline(gaussianEnclosing.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, gaussianEnclosing.pipeline);
		vkCmdBindDescriptorSets(gaussianEnclosing.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, gaussianEnclosing.pipelineLayout, 0, 1, &gaussianEnclosing.descriptorSet, 0, 0);

		uint32_t groupCountX = NUM_OF_GAUSSIANS;
		vkCmdDispatch(gaussianEnclosing.commandBuffer, (gModel.splatSet.size() + groupCountX - 1)/ groupCountX, 1, 1);

		VK_CHECK_RESULT(vkEndCommandBuffer(gaussianEnclosing.commandBuffer));
	}

	/*
		Command buffer record
	*/
	void buildCommandBuffer(FrameObject& frame)
	{
		if (resized)
		{
			handleResize();
		}

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		VK_CHECK_RESULT(vkBeginCommandBuffer(frame.commandBuffer, &cmdBufInfo));

		vkCmdResetQueryPool(frame.commandBuffer, frame.timeStampQueryPool, 0, static_cast<uint32_t>(frame.timeStamps.size()));

#if RAY_QUERY == 1
		vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
		vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &frame.descriptorSet, 0, 0);
		vkCmdPushConstants(frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
#elif RAY_QUERY == 2
		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = frameBuffers[frame.imageIndex];
		vkCmdBeginRenderPass(frame.commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &frame.descriptorSet, 0, nullptr);
		vkCmdPushConstants(frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
#else
		vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
		vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0, 1, &frame.descriptorSet, 0, 0);
		vkCmdPushConstants(frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(pushConstants), &pushConstants);
#endif

		vks::tools::setImageLayout(
			frame.commandBuffer,
			swapChain.images[frame.imageIndex],
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			subresourceRange);

#if RAY_QUERY == 1
		vkCmdDispatch(frame.commandBuffer, (width + TB_SIZE_X - 1) / TB_SIZE_X, (height + TB_SIZE_Y - 1) / TB_SIZE_Y, 1);
#elif RAY_QUERY == 2
		vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
#else
		VkStridedDeviceAddressRegionKHR emptySbtEntry = {};
		vkCmdTraceRaysKHR(
			frame.commandBuffer,
			&shaderBindingTables.raygen.stridedDeviceAddressRegion,
			&shaderBindingTables.miss.stridedDeviceAddressRegion,
			&shaderBindingTables.hit.stridedDeviceAddressRegion,
			&emptySbtEntry,
			width,
			height,
			1);
#endif

		vks::tools::setImageLayout(
			frame.commandBuffer,
			swapChain.images[frame.imageIndex],
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			subresourceRange);

		drawUI(frame.commandBuffer, frameBuffers[frame.imageIndex], frame.vertexBuffer, frame.indexBuffer);

#if RAY_QUERY == 2
		vkCmdEndRenderPass(frame.commandBuffer);
#endif

		vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.timeStampQueryPool, 0);

		VK_CHECK_RESULT(vkEndCommandBuffer(frame.commandBuffer));
	}

	virtual void buildCommandBuffers()
	{
		if (resized)
		{
			handleResize();
		}

		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

		for (auto& frame : frameObjects)
		{
			VK_CHECK_RESULT(vkBeginCommandBuffer(frame.commandBuffer, &cmdBufInfo));

			vkCmdResetQueryPool(frame.commandBuffer, frame.timeStampQueryPool, 0, static_cast<uint32_t>(frame.timeStamps.size()));

#if RAY_QUERY == 1
			vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &frame.descriptorSet, 0, 0);
			vkCmdPushConstants(frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);
#elif RAY_QUERY == 2
			VkClearValue clearValues[2];
			clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
			clearValues[1].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
			renderPassBeginInfo.renderPass = renderPass;
			renderPassBeginInfo.renderArea.offset.x = 0;
			renderPassBeginInfo.renderArea.offset.y = 0;
			renderPassBeginInfo.renderArea.extent.width = width;
			renderPassBeginInfo.renderArea.extent.height = height;
			renderPassBeginInfo.clearValueCount = 2;
			renderPassBeginInfo.pClearValues = clearValues;
			renderPassBeginInfo.framebuffer = frameBuffers[frame.imageIndex];
			vkCmdBeginRenderPass(frame.commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(frame.commandBuffer, 0, 1, &viewport);
			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(frame.commandBuffer, 0, 1, &scissor);

			vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &frame.descriptorSet, 0, nullptr);
			vkCmdPushConstants(frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
#else
			vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
			vkCmdBindDescriptorSets(frame.commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipelineLayout, 0, 1, &frame.descriptorSet, 0, 0);
			vkCmdPushConstants(frame.commandBuffer, pipelineLayout, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(pushConstants), &pushConstants);
#endif

			vks::tools::setImageLayout(
				frame.commandBuffer,
				swapChain.images[frame.imageIndex],
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_GENERAL,
				subresourceRange);

#if RAY_QUERY == 1
			vkCmdDispatch(frame.commandBuffer, (width + TB_SIZE_X - 1) / TB_SIZE_X, (height + TB_SIZE_Y - 1) / TB_SIZE_Y, 1);
#elif RAY_QUERY == 2
			vkCmdDraw(frame.commandBuffer, 3, 1, 0, 0);
#else
			VkStridedDeviceAddressRegionKHR emptySbtEntry = {};
			vkCmdTraceRaysKHR(
				frame.commandBuffer,
				&shaderBindingTables.raygen.stridedDeviceAddressRegion,
				&shaderBindingTables.miss.stridedDeviceAddressRegion,
				&shaderBindingTables.hit.stridedDeviceAddressRegion,
				&emptySbtEntry,
				width,
				height,
				1);
#endif

			vks::tools::setImageLayout(
				frame.commandBuffer,
				swapChain.images[frame.imageIndex],
				VK_IMAGE_LAYOUT_GENERAL,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
				subresourceRange);

			drawUI(frame.commandBuffer, frameBuffers[frame.imageIndex], frame.vertexBuffer, frame.indexBuffer);

#if RAY_QUERY == 2
			vkCmdEndRenderPass(frame.commandBuffer);
#endif

			vkCmdWriteTimestamp(frame.commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame.timeStampQueryPool, 0);

			VK_CHECK_RESULT(vkEndCommandBuffer(frame.commandBuffer));
		}
	}

	void updateUniformBuffer()
	{
		uniformDataDynamic.viewInverse = glm::inverse(camera.matrices.view);
		uniformDataDynamic.projInverse = glm::inverse(camera.matrices.perspective);

		FrameObject currentFrame = frameObjects[getCurrentFrameIndex()];
		memcpy(currentFrame.uniformBuffer.mapped, &uniformDataDynamic, sizeof(uniformDataDynamic));
	}

	void updateGaussianEnclosingUniformBuffer()
	{
		gaussianEnclosingUniformData.numOfGaussians = gModel.splatSet.size();
		gaussianEnclosingUniformData.kernelMinResponse = 0.0113f;	// these values should be managed as config val
		gaussianEnclosingUniformData.opts = 0;
		gaussianEnclosingUniformData.degree = 4;

		// mapping
		vks::Buffer stagingBuffer;
		stagingBuffer.size = sizeof(vks::utils::GaussianEnclosingUniformData);
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer.size, &stagingBuffer.buffer, &stagingBuffer.memory, nullptr));

		void* data;
		vkMapMemory(vulkanDevice->logicalDevice, stagingBuffer.memory, 0, sizeof(vks::utils::GaussianEnclosingUniformData), 0, &data);
		memcpy(data, (void*)&gaussianEnclosingUniformData, sizeof(vks::utils::GaussianEnclosingUniformData));
		vkUnmapMemory(vulkanDevice->logicalDevice, stagingBuffer.memory);

		VkBufferCopy copyRegion;
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = 0;
		copyRegion.size = stagingBuffer.size;
		vulkanDevice->copyBuffer(&stagingBuffer, &gaussianEnclosing.uniformBuffer, graphicsQueue, &copyRegion);

		vkDestroyBuffer(vulkanDevice->logicalDevice, stagingBuffer.buffer, nullptr);
		vkFreeMemory(vulkanDevice->logicalDevice, stagingBuffer.memory, nullptr);
	}

	virtual void getEnabledFeatures()
	{
		// New Features using VkPhysicalDeviceFeatures2 structure.
#if RAY_QUERY
		// Enable feature required for ray query.
		enabledRayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
		enabledRayQueryFeatures.rayQuery = VK_TRUE;
		
		physicalDeviceHostQueryResetFeatures.pNext = &enabledRayQueryFeatures;
#endif

		// Enable feature required for time stamp command pool reset.
		physicalDeviceHostQueryResetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT;
		physicalDeviceHostQueryResetFeatures.hostQueryReset = VK_TRUE;

		// Enable features required for ray tracing using feature chaining via pNext		
		enabledBufferDeviceAddresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
		enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;
		enabledBufferDeviceAddresFeatures.pNext = &physicalDeviceHostQueryResetFeatures;

		enabledRayTracingPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
		enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
		enabledRayTracingPipelineFeatures.pNext = &enabledBufferDeviceAddresFeatures;

		enabledAccelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
		enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
		enabledAccelerationStructureFeatures.pNext = &enabledRayTracingPipelineFeatures;

		physicalDeviceDescriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
		physicalDeviceDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
		physicalDeviceDescriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
		physicalDeviceDescriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
		physicalDeviceDescriptorIndexingFeatures.pNext = &enabledAccelerationStructureFeatures;

		deviceCreatepNextChain = &physicalDeviceDescriptorIndexingFeatures;

		// Original Features using VkPhysicalDeviceFeature structure.
#if BUFFER_REFERENCE
		enabledFeatures.shaderInt64 = VK_TRUE;	// Buffer device address requires the 64-bit integer feature to be enabled
#endif
		enabledFeatures.samplerAnisotropy = VK_TRUE;
	}

	virtual void getEnabledExtensions()
	{
		enableExtensions();

		enabledDeviceExtensions.push_back(VK_KHR_MAINTENANCE3_EXTENSION_NAME);
		enabledDeviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
		enabledDeviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::PreTransformVertices;
		vkglTF::bufferUsageFlags = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

#if LOAD_GLTF
		scene.loadFromFile(getAssetPath() + ASSET_PATH, vulkanDevice, graphicsQueue, glTFLoadingFlags);

		// Cubemap texture
		loadCubemap(getAssetPath() + CUBEMAP_TEXTURE_PATH, VK_FORMAT_R8G8B8A8_UNORM);
#endif

		//gModel.load3DGRTObject(getAssetPath() + "3DGRTModels/lego/ckpt_last.pt", vulkanDevice);
		gModel.load3DGRTModel(getAssetPath() + "3DGRTModels/lego/export_last.ply", vulkanDevice);
	}

	bool initVulkan() {
		// Auto-compile shaders
		// Remove 'pause' from batch file for speedy execution
		std::cout << "*** Shader compilation BEGIN ***\n";
		system("cd ..\\shaders\\glsl\\base\\ && baseCompile.bat");
		std::cout << "\t...base project shaders compile completed.\n";
		system("cd ..\\shaders\\glsl\\VulkanFullRT\\ && VulkanFullRTCompile.bat");
		std::cout << "\t...Vulkan FullRT project shaders compile completed.\n";
		std::cout << "*** Shader compilation END ***\n";

		bool result = VulkanRTBase::initVulkan();
		if (!result) {
			std::cout << "VulkanFullRT initVulkan failed.\n";
			return false;
		}

		return true;
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

	void prepare()
	{
		VulkanRTCommon::prepare();

#ifdef __ANDROID__
		vkGetFenceStatus = reinterpret_cast<PFN_vkGetFenceStatus>(vkGetDeviceProcAddr(device, "vkGetFenceStatus"));
		vkCmdWriteTimestamp = reinterpret_cast<PFN_vkCmdWriteTimestamp>(vkGetDeviceProcAddr(device, "vkCmdWriteTimestamp"));
#endif

		loadAssets();

		frameObjects.resize(swapChain.imageCount);

		createCommandBuffers();

		int i = 0;
		for (FrameObject& frame : frameObjects)
		{
			createBaseFrameObjects(frame, i++);
			pBaseFrameObjects.push_back(&frame);

			// Uniform buffers
			VK_CHECK_RESULT(vulkanDevice->createAndMapBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &frame.uniformBuffer, sizeof(uniformDataDynamic), &uniformDataDynamic));
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &frame.uniformBufferStatic, sizeof(vks::utils::UniformDataStatic), nullptr));
			vks::utils::updateUniformBufferStatic(uniformDataStatic, frame, vulkanDevice, graphicsQueue);
			//VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			//VkMemoryPropertyFlags memoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			//vulkanDevice->createAndCopyToDeviceBuffer(&params3dgrt, frame.uniformBufferParams.buffer, frame.uniformBufferParams.memory, sizeof(vks::utils::Params3DGRT), graphicsQueue, usageFlags, memoryFlags);

			// Time Stamp for measuring performance.
			setupTimeStampQueries(frame, timeStampCountPerFrame);
		}

		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &gaussianEnclosing.uniformBuffer, sizeof(vks::utils::GaussianEnclosingUniformData), nullptr));
		updateGaussianEnclosingUniformBuffer();

		// allocate device memory for vertex/index buffer
		gModel.allocateAttributeBuffers(vulkanDevice, graphicsQueue);
		// particle density
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &particleDensities, sizeof(ParticleDensity) * gModel.splatSet.size(), nullptr));
		// particle sph coefficient
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &particleSphCoefficients, sizeof(ParticleSphCoefficient) * gModel.splatSet.size(), nullptr));
		// particle visibility
		VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &particleVisibility, sizeof(ParticleVisibility) * gModel.splatSet.size(), nullptr));

		// (1) Gaussian Enclosing pass
		createGaussianEnclosingDescriptorSets();
		createGaussianEnclosingPipeline();
		computeGaussianEnclosingIcosaHedron();

		// Create the acceleration structures used to render the ray traced scene
#if LOAD_GLTF
		createBottomLevelAccelerationStructure();
		createTopLevelAccelerationStructure();
#endif

		createBottomLevelAccelerationStructure3DGRT();
		createTopLevelAccelerationStructure3DGRT();

		// (2) Particle Rendering pass
		createDescriptorSets();
		createParticleRenderingPipeline();
#if !RAY_QUERY
		createShaderBindingTables();
#endif

		prepared = true;
	}

	void draw()
	{
		FrameObject currentFrame = frameObjects[getCurrentFrameIndex()];
		VulkanRTBase::prepareFrame(currentFrame);

		VkDescriptorImageInfo storageImageDescriptor{ VK_NULL_HANDLE, swapChain.buffers[currentFrame.imageIndex].view, VK_IMAGE_LAYOUT_GENERAL };
		VkWriteDescriptorSet resultImageWrite = vks::initializers::writeDescriptorSet(currentFrame.descriptorSet, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, &storageImageDescriptor);
		vkUpdateDescriptorSets(device, 1, &resultImageWrite, 0, VK_NULL_HANDLE);

		buildCommandBuffer(currentFrame);
		VulkanRTBase::submitFrame(currentFrame);
	}

	virtual void render()
	{
		if (!prepared)
			return;

		//vks::utils::updateLightDynamicInfo(uniformData, scene, timer);
		updateUniformBuffer();

		draw();
	}
};

VULKAN_FULL_RT()
