/*
* Vulkan RT base class
* 
* Graphics Lab, Sogang Univ
*
* Copyright (C) 2016-2024 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "VulkanRTBase.h"
#include "../../base/Define.h"

#if (defined(VK_USE_PLATFORM_MACOS_MVK) && defined(VK_EXAMPLE_XCODE_GENERATED))
#include <Cocoa/Cocoa.h>
#include <QuartzCore/CAMetalLayer.h>
#include <CoreVideo/CVDisplayLink.h>
#endif

std::vector<const char*> VulkanRTBase::args;

VkResult VulkanRTBase::createInstance(bool enableValidation)
{
	this->settings.validation = enableValidation;

	// Validation can also be forced via a define
#if defined(_VALIDATION)
	this->settings.validation = true;
#endif

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = name.c_str();
	appInfo.pEngineName = name.c_str();
	appInfo.apiVersion = apiVersion;

	std::vector<const char*> instanceExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };

	// Enable surface extensions depending on os
#if defined(_WIN32)
	instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	instanceExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(_DIRECT2DISPLAY)
	instanceExtensions.push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
	instanceExtensions.push_back(VK_EXT_DIRECTFB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	instanceExtensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	instanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
	instanceExtensions.push_back(VK_MVK_IOS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
	instanceExtensions.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_HEADLESS_EXT)
	instanceExtensions.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_SCREEN_QNX)
	instanceExtensions.push_back(VK_QNX_SCREEN_SURFACE_EXTENSION_NAME);
#endif

	// Get extensions supported by the instance and store for later use
	uint32_t extCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
	if (extCount > 0)
	{
		std::vector<VkExtensionProperties> extensions(extCount);
		if (vkEnumerateInstanceExtensionProperties(nullptr, &extCount, &extensions.front()) == VK_SUCCESS)
		{
			for (VkExtensionProperties& extension : extensions)
			{
				supportedInstanceExtensions.push_back(extension.extensionName);
			}
		}
	}

	// Enabled requested instance extensions
	if (enabledInstanceExtensions.size() > 0)
	{
		for (const char * enabledExtension : enabledInstanceExtensions)
		{
			// Output message if requested extension is not available
			if (std::find(supportedInstanceExtensions.begin(), supportedInstanceExtensions.end(), enabledExtension) == supportedInstanceExtensions.end())
			{
				std::cerr << "Enabled instance extension \"" << enabledExtension << "\" is not present at instance level\n";
			}
			instanceExtensions.push_back(enabledExtension);
		}
	}

	VkInstanceCreateInfo instanceCreateInfo = {};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pNext = NULL;
	instanceCreateInfo.pApplicationInfo = &appInfo;

	VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCI{};
	if (settings.validation) {
		vks::debug::setupDebugingMessengerCreateInfo(debugUtilsMessengerCI);
		debugUtilsMessengerCI.pNext = instanceCreateInfo.pNext;
		instanceCreateInfo.pNext = &debugUtilsMessengerCI;
	}

	// Enable the debug utils extension if available (e.g. when debugging tools are present)
	if (settings.validation || std::find(supportedInstanceExtensions.begin(), supportedInstanceExtensions.end(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME) != supportedInstanceExtensions.end()) {
		instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	if (instanceExtensions.size() > 0)
	{
		instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
		instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
	}

	// The VK_LAYER_KHRONOS_validation contains all current validation functionality.
	// Note that on Android this layer requires at least NDK r20
	const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
	if (settings.validation)
	{
		// Check if this layer is available at instance level
		uint32_t instanceLayerCount;
		vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr);
		std::vector<VkLayerProperties> instanceLayerProperties(instanceLayerCount);
		vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayerProperties.data());
		bool validationLayerPresent = false;
		for (VkLayerProperties& layer : instanceLayerProperties) {
			if (strcmp(layer.layerName, validationLayerName) == 0) {
				validationLayerPresent = true;
				break;
			}
		}
		if (validationLayerPresent) {
			instanceCreateInfo.ppEnabledLayerNames = &validationLayerName;
			instanceCreateInfo.enabledLayerCount = 1;
		} else {
			std::cerr << "Validation layer VK_LAYER_KHRONOS_validation not present, validation is disabled";
		}
	}
	VkResult result = vkCreateInstance(&instanceCreateInfo, nullptr, &instance);

	// If the debug utils extension is present we set up debug functions, so samples can label objects for debugging
	if (std::find(supportedInstanceExtensions.begin(), supportedInstanceExtensions.end(), VK_EXT_DEBUG_UTILS_EXTENSION_NAME) != supportedInstanceExtensions.end()) {
		vks::debugutils::setup(instance);
	}

	return result;
}

void VulkanRTBase::renderFrame()
{
	VulkanRTBase::prepareFrame();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &drawCmdBuffers[acquiredIndex];
	VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
	VulkanRTBase::submitFrame();
}

void VulkanRTBase::createBaseFrameObjects(BaseFrameObject& frame, uint32_t imageIndex)
{
	VkFenceCreateInfo fenceCreateInfo = vks::initializers::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
	VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &frame.renderCompleteFence));
	VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();
	VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frame.presentCompleteSemaphore));
	VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frame.renderCompleteSemaphore));
	frame.imageIndex = imageIndex;
}

void VulkanRTBase::destroyBaseFrameObjects(BaseFrameObject& frame)
{
	vkDestroyFence(device, frame.renderCompleteFence, nullptr);
	vkDestroySemaphore(device, frame.presentCompleteSemaphore, nullptr);
	vkDestroySemaphore(device, frame.renderCompleteSemaphore, nullptr);
}

std::string VulkanRTBase::getWindowTitle()
{
	std::string device(deviceProperties.deviceName);
	std::string windowTitle;
	windowTitle = title + " - " + device;
	if (!settings.overlay) {
		windowTitle += " - " + std::to_string(frameCounter) + " fps";
	}
	return windowTitle;
}

// This function must be deleted and organized in each project. (by Sehee)
//void VulkanRTBase::createCommandBuffers()
//{
//	// Create one command buffer for each swap chain image and reuse for rendering
//	drawCmdBuffers.resize(swapChain.imageCount);
//
//	VkCommandBufferAllocateInfo cmdBufAllocateInfo =
//		vks::initializers::commandBufferAllocateInfo(
//			cmdPool,
//			VK_COMMAND_BUFFER_LEVEL_PRIMARY,
//			static_cast<uint32_t>(drawCmdBuffers.size()));
//
//	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, drawCmdBuffers.data()));
//}
void VulkanRTBase::createCommandBuffers() {}

// This function must be deleted and organized in each project. (by Sehee)
//void VulkanRTBase::destroyCommandBuffers()
//{
//	vkFreeCommandBuffers(device, cmdPool, static_cast<uint32_t>(drawCmdBuffers.size()), drawCmdBuffers.data());
//}
void VulkanRTBase::destroyCommandBuffers() {}

std::string VulkanRTBase::getShadersPath() const
{
	return getShaderBasePath() + shaderDir + "/";
}

bool VulkanRTBase::updateOverlayBuffers(vks::UIOverlay& UIOverlay, std::vector<BaseFrameObject*>& frameObjects)
{
	ImDrawData* imDrawData = ImGui::GetDrawData();
	bool updateCmdBuffers = false;

	if (!imDrawData || imDrawData->CmdListsCount == 0) return false;

	// Note: Alignment is done inside buffer creation
	VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
	VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

	// Update buffers only if vertex or index count has been changed compared to current buffer size
	if ((vertexBufferSize == 0) || (indexBufferSize == 0)) return false;

	for (BaseFrameObject* frame : frameObjects)
	{
		bool vertexBufferRecreate = false, indexBufferRecreate = false;
		if ((frame->vertexBuffer.buffer == VK_NULL_HANDLE) || (UIOverlay.vertexCount != imDrawData->TotalVtxCount) || (frame->vertexBuffer.size != vertexBufferSize)) {
			while (true)
				if (vkGetFenceStatus(device, frame->renderCompleteFence) == VK_SUCCESS)
					break;
			vertexBufferRecreate = true;
		}

		if ((frame->indexBuffer.buffer == VK_NULL_HANDLE) || (UIOverlay.indexCount != imDrawData->TotalIdxCount) || (frame->indexBuffer.size != indexBufferSize)) {
			while (true)
				if (vkGetFenceStatus(device, frame->renderCompleteFence) == VK_SUCCESS)
					break;
			indexBufferRecreate = true;
		}

		/*if (vkGetFenceStatus(device, frame->renderCompleteFence) != VK_SUCCESS)
			continue;*/

			// Vertex buffer
		if (vertexBufferRecreate) {
			frame->vertexBuffer.unmap();
			frame->vertexBuffer.destroy();
			VK_CHECK_RESULT(UIOverlay.device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &frame->vertexBuffer, vertexBufferSize));
			UIOverlay.vertexCount = imDrawData->TotalVtxCount;
			frame->vertexBuffer.unmap();
			frame->vertexBuffer.map();
			updateCmdBuffers = true;
		}

		// Index buffer
		if (indexBufferRecreate) {
			frame->indexBuffer.unmap();
			frame->indexBuffer.destroy();
			VK_CHECK_RESULT(UIOverlay.device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, &frame->indexBuffer, indexBufferSize));
			UIOverlay.indexCount = imDrawData->TotalIdxCount;
			frame->indexBuffer.map();
			updateCmdBuffers = true;
		}

		// Upload data
		ImDrawVert* vtxDst = (ImDrawVert*)frame->vertexBuffer.mapped;
		ImDrawIdx* idxDst = (ImDrawIdx*)frame->indexBuffer.mapped;

		for (int n = 0; n < imDrawData->CmdListsCount; n++) {
			const ImDrawList* cmd_list = imDrawData->CmdLists[n];
			memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtxDst += cmd_list->VtxBuffer.Size;
			idxDst += cmd_list->IdxBuffer.Size;
		}

		// Flush to make writes visible to GPU
		frame->vertexBuffer.flush();
		frame->indexBuffer.flush();
	}

	return updateCmdBuffers;
}

void VulkanRTBase::createPipelineCache()
{
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &pipelineCache));
}

void VulkanRTBase::prepare()
{
	initSwapchain();
	createCommandPool();
	setupSwapChain();
	createSynchronizationPrimitives();
	setupDepthStencil();
#if RAY_QUERY == 2
	VulkanRTBase::setupRenderPass();
#else
	setupRenderPass();
#endif
	createPipelineCache();
	setupFrameBuffer();
	settings.overlay = settings.overlay && (!benchmark.active);
	if (settings.overlay) {
		UIOverlay.device = vulkanDevice;
		UIOverlay.queue = graphicsQueue;
		UIOverlay.shaders = {
			loadShader(getShadersPath() + "base/uioverlay.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShadersPath() + "base/uioverlay.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
		};
		UIOverlay.prepareResources();
		UIOverlay.preparePipeline(pipelineCache, renderPass, swapChain.colorFormat, depthFormat);
	}
}

VkPipelineShaderStageCreateInfo VulkanRTBase::loadShader(std::string fileName, VkShaderStageFlagBits stage)
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

void VulkanRTBase::nextFrame(std::vector<BaseFrameObject*>& frameObjects)
{
	auto tStart = std::chrono::high_resolution_clock::now();
	if (viewUpdated)
	{
		viewUpdated = false;
	}

	render();
	frameCounter++;
	auto tEnd = std::chrono::high_resolution_clock::now();
	auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

	frameTimer = (float)tDiff / 1000.0f;
	camera.update(frameTimer);
	if (camera.moving())
	{
		viewUpdated = true;
	}
	// Convert to clamped timer value
	if (!paused)
	{
#if TIMER_CORRECTION
		timer += timerSpeed * frameTimer;
#else
		timer += timerSpeed * timerControl;
#endif
		if (timer > 1.0)
		{
			timer -= 1.0f;
		}
	}
	float fpsTimer = (float)(std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count());
	if (fpsTimer > 1000.0f)
	{
		lastFPS = static_cast<uint32_t>((float)frameCounter * (1000.0f / fpsTimer));
#if defined(_WIN32)
		if (!settings.overlay)	{
			std::string windowTitle = getWindowTitle();
			SetWindowText(window, windowTitle.c_str());
		}
#endif
		frameCounter = 0;
		lastTimestamp = tEnd;
	}
	tPrevEnd = tEnd;

	// TODO: Cap UI overlay update rates
	updateOverlay(frameObjects);
}

void VulkanRTBase::renderLoop(std::vector<BaseFrameObject*>& frameObjects)
{
	// SRS - handle benchmarking here within VulkanRTBase::renderLoop()
	if (benchmark.active) {
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
		while (!configured)
			wl_display_dispatch(display);
		while (wl_display_prepare_read(display) != 0)
			wl_display_dispatch_pending(display);
		wl_display_flush(display);
		wl_display_read_events(display);
		wl_display_dispatch_pending(display);
#endif

		benchmark.run([=, this] { render(); }, vulkanDevice->properties);
		vkDeviceWaitIdle(device);
		if (benchmark.filename != "") {
			benchmark.saveResults();
		}
		return;
	}

	destWidth = width;
	destHeight = height;
	lastTimestamp = std::chrono::high_resolution_clock::now();
	tPrevEnd = lastTimestamp;
#if defined(_WIN32)
	MSG msg;
	bool quitMessageReceived = false;
	while (!quitMessageReceived) {
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT) {
				quitMessageReceived = true;
				break;
			}
		}
		if (prepared && !IsIconic(window)) {
			nextFrame(frameObjects);
		}
	}
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	while (1)
	{
		int ident;
		int events;
		struct android_poll_source* source;
		bool destroy = false;

		focused = true;

		while ((ident = ALooper_pollAll(focused ? 0 : -1, NULL, &events, (void**)&source)) >= 0)
		{
			if (source != NULL)
			{
				source->process(androidApp, source);
			}
			if (androidApp->destroyRequested != 0)
			{
				LOGD("Android app destroy requested");
				destroy = true;
				break;
			}
		}

		// App destruction requested
		// Exit loop, example will be destroyed in application main
		if (destroy)
		{
			ANativeActivity_finish(androidApp->activity);
			break;
		}

		// Render frame
		if (prepared)
		{
			auto tStart = std::chrono::high_resolution_clock::now();
			render();
			frameCounter++;
			auto tEnd = std::chrono::high_resolution_clock::now();
			auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
			frameTimer = tDiff / 1000.0f;
			camera.update(frameTimer);
			// Convert to clamped timer value
			if (!paused)
			{
#if TIMER_CORRECTION
				timer += timerSpeed * frameTimer;
#else
				timer += timerSpeed * timerControl;
#endif
				if (timer > 1.0)
				{
					timer -= 1.0f;
				}
			}
			float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
			if (fpsTimer > 1000.0f)
			{
				lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
				frameCounter = 0;
				lastTimestamp = tEnd;
			}

			// TODO: Cap UI overlay update rates/only issue when update requested
			updateOverlay(frameObjects);

			bool updateView = false;

			// Check touch state (for movement)
			// do not using now
			/*if (touchDown) {
				touchTimer += frameTimer;
				if (touchTimer >= 1.5) {
					camera.keys.up = true;
				}
			}
			if (doubleTouchDown) {
				touchTimer += frameTimer;
				if (touchTimer >= 1.0) {
					camera.keys.down = true;
				}
			}*/


			// Check gamepad state
			const float deadZone = 0.0015f;
			// todo : check if gamepad is present
			// todo : time based and relative axis positions
			if (camera.type != Camera::CameraType::firstperson)
			{
				// Rotate
				if (std::abs(gamePadState.axisLeft.x) > deadZone)
				{
					camera.rotate(glm::vec3(0.0f, gamePadState.axisLeft.x * 0.5f, 0.0f));
					updateView = true;
				}
				if (std::abs(gamePadState.axisLeft.y) > deadZone)
				{
					camera.rotate(glm::vec3(gamePadState.axisLeft.y * 0.5f, 0.0f, 0.0f));
					updateView = true;
				}
				// Zoom
				if (std::abs(gamePadState.axisRight.y) > deadZone)
				{
					camera.translate(glm::vec3(0.0f, 0.0f, gamePadState.axisRight.y * 0.01f));
					updateView = true;
				}
			}
			else
			{
				updateView = camera.updatePad(gamePadState.axisLeft, gamePadState.axisRight, frameTimer);
			}
		}
	}
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
		}
		DFBWindowEvent event;
		while (!event_buffer->GetEvent(event_buffer, DFB_EVENT(&event)))
		{
			handleEvent(&event);
		}
		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		// Convert to clamped timer value
		if (!paused)
		{
			timer += timerSpeed * frameTimer;
			if (timer > 1.0)
			{
				timer -= 1.0f;
			}
		}
		float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
		if (fpsTimer > 1000.0f)
		{
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			frameCounter = 0;
			lastTimestamp = tEnd;
		}
		updateOverlay();
	}
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
		}

		while (!configured)
			wl_display_dispatch(display);
		while (wl_display_prepare_read(display) != 0)
			wl_display_dispatch_pending(display);
		wl_display_flush(display);
		wl_display_read_events(display);
		wl_display_dispatch_pending(display);

		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		// Convert to clamped timer value
		if (!paused)
		{
			timer += timerSpeed * frameTimer;
			if (timer > 1.0)
			{
				timer -= 1.0f;
			}
		}
		float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
		if (fpsTimer > 1000.0f)
		{
			if (!settings.overlay)
			{
				std::string windowTitle = getWindowTitle();
				xdg_toplevel_set_title(xdg_toplevel, windowTitle.c_str());
			}
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			frameCounter = 0;
			lastTimestamp = tEnd;
		}
		updateOverlay();
	}
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	xcb_flush(connection);
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
		}
		xcb_generic_event_t *event;
		while ((event = xcb_poll_for_event(connection)))
		{
			handleEvent(event);
			free(event);
		}
		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		// Convert to clamped timer value
		if (!paused)
		{
			timer += timerSpeed * frameTimer;
			if (timer > 1.0)
			{
				timer -= 1.0f;
			}
		}
		float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
		if (fpsTimer > 1000.0f)
		{
			if (!settings.overlay)
			{
				std::string windowTitle = getWindowTitle();
				xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
					window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
					windowTitle.size(), windowTitle.c_str());
			}
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			frameCounter = 0;
			lastTimestamp = tEnd;
		}
		updateOverlay();
	}
#elif defined(VK_USE_PLATFORM_HEADLESS_EXT)
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
		}
		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		// Convert to clamped timer value
		timer += timerSpeed * frameTimer;
		if (timer > 1.0)
		{
			timer -= 1.0f;
		}
		float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
		if (fpsTimer > 1000.0f)
		{
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			frameCounter = 0;
			lastTimestamp = tEnd;
		}
		updateOverlay();
	}
#elif defined(VK_USE_PLATFORM_SCREEN_QNX)
	while (!quit) {
		handleEvent();

		if (prepared) {
			nextFrame();
		}
	}
#endif
	// Flush device to make sure all resources can be freed
	if (device != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(device);
	}
}

void VulkanRTBase::updateOverlay(std::vector<BaseFrameObject*>& frameObjects)
{
	if (!settings.overlay)
		return;

	ImGuiIO& io = ImGui::GetIO();

	io.DisplaySize = ImVec2((float)width, (float)height);
	io.DeltaTime = frameTimer;

	io.MousePos = ImVec2(mouseState.position.x, mouseState.position.y);
	io.MouseDown[0] = mouseState.buttons.left && UIOverlay.visible;
	io.MouseDown[1] = mouseState.buttons.right && UIOverlay.visible;
	io.MouseDown[2] = mouseState.buttons.middle && UIOverlay.visible;

	ImGui::NewFrame();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(ImVec2(10 * UIOverlay.scale, 10 * UIOverlay.scale));
	ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiSetCond_FirstUseEver);
	ImGui::Begin("Sponza scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::TextUnformatted(title.c_str());
	ImGui::TextUnformatted(deviceProperties.deviceName);
	ImGui::Text("%.2f ms/frame (%.1d fps)", (1000.0f / lastFPS), lastFPS);
	//imgui custom
	//Todo1 : new text line for shadow / reflection / refraction	on / off
	//Todo2 : add new window for camera movement(maybe hard)
	ImGui::Checkbox("Shadow ray", &pushConstants.rayOption.shadowRay);
	ImGui::Checkbox("Reflection", &pushConstants.rayOption.reflection);
	ImGui::Checkbox("Refraction", &pushConstants.rayOption.refraction);
	ImGui::Text("Camera Vertical Movement");
	ImGui::SameLine();
	if (ImGui::ArrowButton("##Up", ImGuiDir_Up) || ImGui::IsItemActive()) {
		camera.keys.up = true;
	}
	if (ImGui::IsItemDeactivated()) {
		camera.keys.up = false;
	}
	ImGui::SameLine();
	if (ImGui::ArrowButton("##Down", ImGuiDir_Down) || ImGui::IsItemActive()) {
		camera.keys.down = true;
	}
	if (ImGui::IsItemDeactivated()) {
		camera.keys.down = false;
	}
	ImGui::Text("Camera View");
	ImGui::SameLine();
	if (ImGui::Button("0")) {
		setCamera(0);
	}
	ImGui::SameLine();
	if(ImGui::Button("1")) {
		setCamera(1);
	}
	ImGui::SameLine();
	if (ImGui::Button("2")) {
		setCamera(2);
	}
	ImGui::Separator();
	ImGui::Text("Light Attenuation Factor");
	ImGui::SliderFloat("_alpha", &pushConstants.lightAttVar.alpha, 0.001f, 1.0f);
	ImGui::SliderFloat("_beta", &pushConstants.lightAttVar.beta, 0.001f, 1.0f);
	ImGui::SliderFloat("_gamma", &pushConstants.lightAttVar.gamma, 0.001f, 1.0f);
	ImGui::Separator();

	//text input for fps calculation
	if(!fpsQuery) {
		//ImGui::PushItemWidth(50);
		//ImGui::InputInt("fps measure frame input", &measureFrame, 0, 0);
		//ImGui::InputText("fps measure frame input", measureFrameText, 5, ImGuiInputTextFlags_CharsDecimal);
		//measureFrame = atoi(measureFrameText);
		//ImGui::PopItemWidth();
		if (ImGui::Button("measure fps")) {
			startFrame = recordCount + 1;
			fpsQuery = true;
		}
		//ImGui::button
	}
	if (delta_in_ms > 0.0f) {
		ImGui::Text("%f (ms), %f (fps)", delta_in_ms, 1000.0f / delta_in_ms);
	}

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 5.0f * UIOverlay.scale));
#endif
	ImGui::PushItemWidth(110.0f * UIOverlay.scale);
	//OnUpdateUIOverlay(&UIOverlay);
	ImGui::PopItemWidth();
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	ImGui::PopStyleVar();
#endif

	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::Render();

	updateOverlayBuffers(UIOverlay, frameObjects);

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	/*if (mouseState.buttons.left) {
		mouseState.buttons.left = false;
	}*/
#endif
}

void VulkanRTBase::drawUI(const VkCommandBuffer commandBuffer)
{
	if (settings.overlay && UIOverlay.visible) {
		const VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		const VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		UIOverlay.draw(commandBuffer);
	}
}

void VulkanRTBase::drawUI(const VkCommandBuffer commandBuffer, const vks::Buffer& vertexBuffer, const vks::Buffer& indexBuffer)
{
	if (settings.overlay && UIOverlay.visible) {
		const VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		const VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		UIOverlay.draw(commandBuffer, vertexBuffer, indexBuffer);
	}
}
void VulkanRTBase::calculateFPS(BaseFrameObject& frame)
{
	//static int recordCount = 0;
	// Time stamp
	if (fpsQuery && (recordCount == swapChain.imageCount + startFrame || recordCount == measureFrame + swapChain.imageCount + startFrame)) {
		vkGetQueryPoolResults(device, frame.timeStampQueryPool, 0, 1, frame.timeStamps.size() * sizeof(uint64_t),
			frame.timeStamps.data(), sizeof(uint64_t) * 2, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
#if defined(_WIN32)
		if (frame.timeStamps[1] != 0) {
			//timeRecords.push_back(frame.timeStamps[0]);

			std::cout << "[Timestamp] Current time : " << frame.timeStamps[0] << "\n";
			recordResults.push_back(frame.timeStamps[0]);
			if (recordCount == measureFrame + swapChain.imageCount + startFrame) {
				VkPhysicalDeviceLimits device_limits = deviceProperties.limits;
				delta_in_ms = float(recordResults[1] - recordResults[0]) * device_limits.timestampPeriod / (measureFrame * 1000000.0f);
				std::cout << delta_in_ms << " (ms), " << 1000.0f / delta_in_ms << " (fps)\n";
				recordResults.clear();
				fpsQuery = false;
			}
		}
		else {
			std::cout << "[Timestamp] Timestamp Not Ready...\n";
		}
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
		if (frame.timeStamps[1] != 0) {
			//timeRecords.push_back(frame.timeStamps[0]);

			LOGD("[Timestamp] Current time : %d\n", frame.timeStamps[0]);
			recordResults.push_back(frame.timeStamps[0]);
			if (recordCount == measureFrame + swapChain.imageCount + startFrame) {
				VkPhysicalDeviceLimits device_limits = deviceProperties.limits;
				delta_in_ms = float(recordResults[1] - recordResults[0]) * device_limits.timestampPeriod / (measureFrame * 1000000.0f);
				LOGD("%f (ms), %f (fps)\n", delta_in_ms, 1000.0f / delta_in_ms);
				recordResults.clear();
				fpsQuery = false;
			}
		}
		else {
			LOGD("[Timestamp] Timestamp Not Ready...\n");
		}
#endif
	}
	if (recordCount < INT_MAX) {
		recordCount++;
	}
}

void VulkanRTBase::prepareFrame()
{
	// Acquire the next image from the swap chain
	VkResult result = swapChain.acquireNextImage(semaphores.presentComplete, &acquiredIndex);
	// Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE)
	// SRS - If no longer optimal (VK_SUBOPTIMAL_KHR), wait until submitFrame() in case number of swapchain images will change on resize

	if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			windowResize();
		}
		return;
	}
	else {
		VK_CHECK_RESULT(result);
	}
}

void VulkanRTBase::prepareFrame(BaseFrameObject& frame)
{
	// Ensure command buffer execution has finished
	//VK_CHECK_RESULT(vkWaitForFences(device, 1, &frame.renderCompleteFence, VK_TRUE, UINT64_MAX));
	VkResult res = vkWaitForFences(device, 1, &frame.renderCompleteFence, VK_TRUE, UINT64_MAX);

	calculateFPS(frame);

	VK_CHECK_RESULT(vkResetFences(device, 1, &frame.renderCompleteFence));
	VkResult result = swapChain.acquireNextImage(frame.presentCompleteSemaphore, &acquiredIndex);
	frame.imageIndex = acquiredIndex;
	// Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE) or no longer optimal for presentation (SUBOPTIMAL)

	if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
		windowResize();
	}
	else {
		VK_CHECK_RESULT(result);
	}
}

void VulkanRTBase::submitFrame()
{
#if MULTIQUEUE
	VkResult result = swapChain.queuePresent(presentQueue, acquiredIndex, semaphores.renderComplete);
#else
	VkResult result = swapChain.queuePresent(graphicsQueue, acquiredIndex, semaphores.renderComplete);
#endif
	// Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE) or no longer optimal for presentation (SUBOPTIMAL)
	if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
		windowResize();
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			return;
		}
	}
	else {
		VK_CHECK_RESULT(result);
	}

	frameIndex = (frameIndex + 1) % swapChain.imageCount;
}

void VulkanRTBase::submitFrame(BaseFrameObject& frame)
{
	// Submit command buffer to queue
	VkPipelineStageFlags submitWaitStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submitInfo = vks::initializers::submitInfo();
	submitInfo.pWaitDstStageMask = &submitWaitStages;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &frame.presentCompleteSemaphore;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &frame.renderCompleteSemaphore;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &frame.commandBuffer;
	VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, frame.renderCompleteFence));

#if MULTIQUEUE
	VkResult result = swapChain.queuePresent(presentQueue, frame.imageIndex, frame.renderCompleteSemaphore);
#else
	VkResult result = swapChain.queuePresent(graphicsQueue, frame.imageIndex, frame.renderCompleteSemaphore);
#endif

	if (!((result == VK_SUCCESS) || (result == VK_SUBOPTIMAL_KHR))) {
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			// Swap chain is no longer compatible with the surface and needs to be recreated
			windowResize();
			return;
		}
		else {
			VK_CHECK_RESULT(result);
		}
	}

	frameIndex = (frameIndex + 1) % swapChain.imageCount;

	//vkQueueWaitIdle(graphicsQueue);
}

void VulkanRTBase::submitFrameCustom(BaseFrameObject& frame, VkCommandBuffer& commandBuffer, std::vector<VkSemaphore> waitSemaphores, std::vector<VkSemaphore> signalSemaphores, std::vector<VkPipelineStageFlags> waitStages)
{
	// Submit command buffer to queue
	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.pNext = NULL;
	submitInfo.pWaitDstStageMask = waitStages.data();
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
	submitInfo.pWaitSemaphores = waitSemaphores.data();
	submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
	submitInfo.pSignalSemaphores = signalSemaphores.data();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
}

void VulkanRTBase::submitFrameCustomMini(VkCommandBuffer& commandBuffer)
{
	// Submit command buffer to queue
	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.pNext = NULL;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
}

void VulkanRTBase::submitFrameCustomWait(BaseFrameObject& frame, std::vector<VkSemaphore> waitSemaphores, VkPipelineStageFlags waitStages)
{
	// Submit command buffer to queue
	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.pNext = NULL;
	submitInfo.pWaitDstStageMask = &waitStages;
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
	submitInfo.pWaitSemaphores = waitSemaphores.data();
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &frame.renderCompleteSemaphore;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &frame.commandBuffer;
	VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, frame.renderCompleteFence));

#if MULTIQUEUE
	VkResult result = swapChain.queuePresent(presentQueue, frame.imageIndex, frame.renderCompleteSemaphore);
#else
	VkResult result = swapChain.queuePresent(graphicsQueue, frame.imageIndex, frame.renderCompleteSemaphore);
#endif
	if (!((result == VK_SUCCESS) || (result == VK_SUBOPTIMAL_KHR))) {
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			// Swap chain is no longer compatible with the surface and needs to be recreated
			windowResize();
			return;
		}
		else {
			VK_CHECK_RESULT(result);
		}
	}

	frameIndex = (frameIndex + 1) % swapChain.imageCount;
}

void VulkanRTBase::submitFrameCustomSignal(BaseFrameObject& frame, VkCommandBuffer& commandBuffer, std::vector<VkSemaphore> signalSemaphores, VkPipelineStageFlags waitStages)
{
	// Submit command buffer to queue
	VkSubmitInfo submitInfo = vks::initializers::submitInfo();
	submitInfo.pNext = NULL;
	submitInfo.pWaitDstStageMask = &waitStages;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &frame.presentCompleteSemaphore;
	submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
	submitInfo.pSignalSemaphores = signalSemaphores.data();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	VK_CHECK_RESULT(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
}

uint32_t VulkanRTBase::getCurrentFrameIndex()
{
	return frameIndex;
}

void VulkanRTBase::setupTimeStampQueries(BaseFrameObject& frame, const uint32_t timeStampCountPerFrame) {
	frame.timeStamps.resize(timeStampCountPerFrame * 2);    // Multiply by 2, because each time stamp uses 2 values(result, availability).

	VkQueryPoolCreateInfo queryPoolInfo{};
	queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	queryPoolInfo.queryCount = static_cast<uint32_t>(frame.timeStamps.size());
	VK_CHECK_RESULT(vkCreateQueryPool(device, &queryPoolInfo, nullptr, &frame.timeStampQueryPool));
}

VulkanRTBase::VulkanRTBase()
{
#if !defined(VK_USE_PLATFORM_ANDROID_KHR)
	// Check for a valid asset path
	struct stat info;
	if (stat(getAssetPath().c_str(), &info) != 0)
	{
#if defined(_WIN32)
		std::string msg = "Could not locate asset path in \"" + getAssetPath() + "\" !";
		MessageBox(NULL, msg.c_str(), "Fatal error", MB_OK | MB_ICONERROR);
#else
		std::cerr << "Error: Could not find asset path in " << getAssetPath() << "\n";
#endif
		exit(-1);
	}
#endif

	// Validation for all samples can be forced at compile time using the FORCE_VALIDATION define
#if defined(FORCE_VALIDATION)
	settings.validation = true;
#endif

	// Command line arguments
	commandLineParser.add("help", { "--help" }, 0, "Show help");
	commandLineParser.add("validation", { "-v", "--validation" }, 0, "Enable validation layers");
	commandLineParser.add("vsync", { "-vs", "--vsync" }, 0, "Enable V-Sync");
	commandLineParser.add("fullscreen", { "-f", "--fullscreen" }, 0, "Start in fullscreen mode");
	commandLineParser.add("width", { "-w", "--width" }, 1, "Set window width");
	commandLineParser.add("height", { "-h", "--height" }, 1, "Set window height");
	commandLineParser.add("shaders", { "-s", "--shaders" }, 1, "Select shader type to use (glsl or hlsl)");
	commandLineParser.add("gpuselection", { "-g", "--gpu" }, 1, "Select GPU to run on");
	commandLineParser.add("gpulist", { "-gl", "--listgpus" }, 0, "Display a list of available Vulkan devices");
	commandLineParser.add("benchmark", { "-b", "--benchmark" }, 0, "Run example in benchmark mode");
	commandLineParser.add("benchmarkwarmup", { "-bw", "--benchwarmup" }, 1, "Set warmup time for benchmark mode in seconds");
	commandLineParser.add("benchmarkruntime", { "-br", "--benchruntime" }, 1, "Set duration time for benchmark mode in seconds");
	commandLineParser.add("benchmarkresultfile", { "-bf", "--benchfilename" }, 1, "Set file name for benchmark results");
	commandLineParser.add("benchmarkresultframes", { "-bt", "--benchframetimes" }, 0, "Save frame times to benchmark results file");
	commandLineParser.add("benchmarkframes", { "-bfs", "--benchmarkframes" }, 1, "Only render the given number of frames");

	commandLineParser.parse(args);
	if (commandLineParser.isSet("help")) {
#if defined(_WIN32)
		setupConsole("Abura Soba - Console - Help");
#endif
		commandLineParser.printHelp();
		std::cin.get();
		exit(0);
	}
	if (commandLineParser.isSet("validation")) {
		settings.validation = true;
	}
	if (commandLineParser.isSet("vsync")) {
		settings.vsync = true;
	}
	if (commandLineParser.isSet("height")) {
		height = commandLineParser.getValueAsInt("height", height);
	}
	if (commandLineParser.isSet("width")) {
		width = commandLineParser.getValueAsInt("width", width);
	}
	if (commandLineParser.isSet("fullscreen")) {
		settings.fullscreen = true;
	}
	if (commandLineParser.isSet("shaders")) {
		std::string value = commandLineParser.getValueAsString("shaders", "glsl");
		if ((value != "glsl") && (value != "hlsl")) {
			std::cerr << "Shader type must be one of 'glsl' or 'hlsl'\n";
		}
		else {
			shaderDir = value;
		}
	}
	if (commandLineParser.isSet("benchmark")) {
		benchmark.active = true;
		vks::tools::errorModeSilent = true;
	}
	if (commandLineParser.isSet("benchmarkwarmup")) {
		benchmark.warmup = commandLineParser.getValueAsInt("benchmarkwarmup", 0);
	}
	if (commandLineParser.isSet("benchmarkruntime")) {
		benchmark.duration = commandLineParser.getValueAsInt("benchmarkruntime", benchmark.duration);
	}
	if (commandLineParser.isSet("benchmarkresultfile")) {
		benchmark.filename = commandLineParser.getValueAsString("benchmarkresultfile", benchmark.filename);
	}
	if (commandLineParser.isSet("benchmarkresultframes")) {
		benchmark.outputFrameTimes = true;
	}
	if (commandLineParser.isSet("benchmarkframes")) {
		benchmark.outputFrames = commandLineParser.getValueAsInt("benchmarkframes", benchmark.outputFrames);
	}

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	// Vulkan library is loaded dynamically on Android
	bool libLoaded = vks::android::loadVulkanLibrary();
	assert(libLoaded);

#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	initWaylandConnection();
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	initxcbConnection();
#endif

#if defined(_WIN32)
	// Enable console if validation is active, debug message callback will output to it
	if (this->settings.validation)
	{
		setupConsole("Abura Soba - Console - Validation");
	}
	setupDPIAwareness();

	// Main Console
	setupConsole("Abura Soba - Console");
	std::cout << "***** Team Abura Soba's 3D Gaussian Vulkan Ray Tracing *****\n";
#endif
}

VulkanRTBase::~VulkanRTBase()
{
#if defined(_WIN32)
	FreeConsole();
#endif

	// Clean up Vulkan resources
	swapChain.cleanup();
	if (descriptorPool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	}
	//destroyCommandBuffers();
	if (renderPass != VK_NULL_HANDLE)
	{
		vkDestroyRenderPass(device, renderPass, nullptr);
	}
	for (uint32_t i = 0; i < frameBuffers.size(); i++)
	{
		vkDestroyFramebuffer(device, frameBuffers[i], nullptr);
	}

	for (auto& shaderModule : shaderModules)
	{
		vkDestroyShaderModule(device, shaderModule, nullptr);
	}

	vkDestroyImageView(device, depthStencil.view, nullptr);
	vkDestroyImage(device, depthStencil.image, nullptr);
	vkFreeMemory(device, depthStencil.memory, nullptr);

	cubeMap.destroy();

	vkDestroyPipelineCache(device, pipelineCache, nullptr);

	vkDestroyCommandPool(device, cmdPool, nullptr);

	//for (auto& semaphore : semaphores) {
	//	vkDestroySemaphore(device, semaphore.presentComplete, nullptr);
	//	vkDestroySemaphore(device, semaphore.renderComplete, nullptr);
	//}
	for (auto& fence : waitFences) {
		vkDestroyFence(device, fence, nullptr);
	}

	if (settings.overlay) {
		UIOverlay.freeResources();
	}

	delete vulkanDevice;

	if (settings.validation)
	{
		vks::debug::freeDebugCallback(instance);
	}

	vkDestroyInstance(instance, nullptr);

#if defined(VK_USE_PLATFORM_DIRECTFB_EXT)
	if (event_buffer)
		event_buffer->Release(event_buffer);
	if (surface)
		surface->Release(surface);
	if (window)
		window->Release(window);
	if (layer)
		layer->Release(layer);
	if (dfb)
		dfb->Release(dfb);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	xdg_toplevel_destroy(xdg_toplevel);
	xdg_surface_destroy(xdg_surface);
	wl_surface_destroy(surface);
	if (keyboard)
		wl_keyboard_destroy(keyboard);
	if (pointer)
		wl_pointer_destroy(pointer);
	if (seat)
		wl_seat_destroy(seat);
	xdg_wm_base_destroy(shell);
	wl_compositor_destroy(compositor);
	wl_registry_destroy(registry);
	wl_display_disconnect(display);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	// todo : android cleanup (if required)
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	xcb_destroy_window(connection, window);
	xcb_disconnect(connection);
#elif defined(VK_USE_PLATFORM_SCREEN_QNX)
	screen_destroy_event(screen_event);
	screen_destroy_window(screen_window);
	screen_destroy_context(screen_context);
#endif
}

bool VulkanRTBase::initVulkan()
{
	VkResult err;

	// Vulkan instance
	err = createInstance(settings.validation);
	if (err) {
		vks::tools::exitFatal("Could not create Vulkan instance : \n" + vks::tools::errorString(err), err);
		return false;
	}

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	vks::android::loadVulkanFunctions(instance);
#endif

	// If requested, we enable the default validation layers for debugging
	//if (settings.validation)
	if (true)
	{
		vks::debug::setupDebugging(instance);
	}

	// Physical device
	uint32_t gpuCount = 0;
	// Get number of available physical devices
	VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));
	if (gpuCount == 0) {
		vks::tools::exitFatal("No device with Vulkan support found", -1);
		return false;
	}
	// Enumerate devices
	std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
	err = vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data());
	if (err) {
		vks::tools::exitFatal("Could not enumerate physical devices : \n" + vks::tools::errorString(err), err);
		return false;
	}

	// GPU selection

	// Select physical device to be used for the Vulkan example
	// Defaults to the first device unless specified by command line
	uint32_t selectedDevice = 0;

#if !defined(VK_USE_PLATFORM_ANDROID_KHR)
	// GPU selection via command line argument
	if (commandLineParser.isSet("gpuselection")) {
		uint32_t index = commandLineParser.getValueAsInt("gpuselection", 0);
		if (index > gpuCount - 1) {
			std::cerr << "Selected device index " << index << " is out of range, reverting to device 0 (use -listgpus to show available Vulkan devices)" << "\n";
		} else {
			selectedDevice = index;
		}
	}

	std::cout << "*** Find an adequate physical device BEGIN ***\n";
	if (true) {
		bool isAdequate = false;
		std::cout << "Available Vulkan devices" << "\n";
		std::cout << "----------------------------------------------------\n";
		for (uint32_t i = 0; i < gpuCount; i++) {
			VkPhysicalDeviceProperties deviceProperties;
			vkGetPhysicalDeviceProperties(physicalDevices[i], &deviceProperties);
			std::cout << "\tDevice [" << i << "] : " << deviceProperties.deviceName << std::endl;
			std::cout << "\t\tType: " << vks::tools::physicalDeviceTypeString(deviceProperties.deviceType) << "\n";
			std::cout << "\t\tAPI: " << VK_API_VERSION_MAJOR(deviceProperties.apiVersion) << "." << VK_API_VERSION_MINOR(deviceProperties.apiVersion) << "." << VK_API_VERSION_PATCH(deviceProperties.apiVersion) << "\n";
			bool result = sg::checkDeviceExtensionSupport(physicalDevices[i], enabledDeviceExtensions);
			if (result) {
				selectedDevice = i;
				isAdequate = true;
				std::cout << "\t===> Adequate device found!\n";
				std::cout << "----------------------------------------------------\n";
				break;
			}
			std::cout << "----------------------------------------------------\n";
		}
		if (!isAdequate) exit(-1);
	}
	std::cout << "*** Find an adequate physical device END ***\n";
#endif

	physicalDevice = physicalDevices[selectedDevice];

	// Store properties (including limits), features and memory properties of the physical device (so that projects can check against them)
	vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
	vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);

	// Check for supporting Vulkan timestamp queries.
	deviceLimits = deviceProperties.limits;
	if (deviceLimits.timestampPeriod == 0)
	{
		throw std::runtime_error{ "The selected device does not support timestamp queries!" };
	}
	if (!deviceLimits.timestampComputeAndGraphics)
	{
		throw std::runtime_error{ "The selected device does not support for timestamps on all graphics and compute queues!" };
	}

	// Derived projects can override this to set actual features (based on above readings) to enable for logical device creation
	getEnabledFeatures();

	// Derived projects can enable extensions based on the list of supported extensions read from the physical device
	getEnabledExtensions();

	// Vulkan device creation
	// This is handled by a separate class that gets a logical device representation
	// and encapsulates functions related to a device
	vulkanDevice = new vks::VulkanDevice(physicalDevice);

	VkResult res = vulkanDevice->createLogicalDevice(enabledFeatures, enabledDeviceExtensions, deviceCreatepNextChain);
	if (res != VK_SUCCESS) {
		vks::tools::exitFatal("Could not create Vulkan device: \n" + vks::tools::errorString(res), res);
		return false;
	}
	device = vulkanDevice->logicalDevice;

	// Get a graphics queue from the device
	vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.graphics, 0, &graphicsQueue);
#if MULTIQUEUE
	vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.graphics, 1, &presentQueue);
#endif

	// Find a suitable depth and/or stencil format
	VkBool32 validFormat{ false };
	// Samples that make use of stencil will require a depth + stencil format, so we select from a different list
	if (requiresStencil) {
		validFormat = vks::tools::getSupportedDepthStencilFormat(physicalDevice, &depthFormat);
	} else {
		validFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &depthFormat);
	}
	assert(validFormat);

	swapChain.connect(instance, physicalDevice, device);

	// Create synchronization objects
	VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::semaphoreCreateInfo();


	// Create a semaphore used to synchronize image presentation
	// Ensures that the image is displayed before we start submitting new commands to the queue
	VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphores.presentComplete));
	// Create a semaphore used to synchronize command submission
	// Ensures that the image is not presented until all commands have been submitted and executed
	VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphores.renderComplete));


	// Set up submit info structure
	// Semaphores will stay the same during application lifetime
	// Command buffer submission info is set by each example
	submitInfo = vks::initializers::submitInfo();
	submitInfo.pWaitDstStageMask = &submitPipelineStages;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &semaphores.presentComplete;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &semaphores.renderComplete;

	return true;
}

#if defined(_WIN32)
// Win32 : Sets up a console window and redirects standard output to it
void VulkanRTBase::setupConsole(std::string title)
{
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	FILE *stream;
	freopen_s(&stream, "CONIN$", "r", stdin);
	freopen_s(&stream, "CONOUT$", "w+", stdout);
	freopen_s(&stream, "CONOUT$", "w+", stderr);
	// Enable flags so we can color the output
	HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD dwMode = 0;
	GetConsoleMode(consoleHandle, &dwMode);
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(consoleHandle, dwMode);
	SetConsoleTitle(TEXT(title.c_str()));
}

void VulkanRTBase::setupDPIAwareness()
{
	typedef HRESULT *(__stdcall *SetProcessDpiAwarenessFunc)(PROCESS_DPI_AWARENESS);

	HMODULE shCore = LoadLibraryA("Shcore.dll");
	if (shCore)
	{
		SetProcessDpiAwarenessFunc setProcessDpiAwareness =
			(SetProcessDpiAwarenessFunc)GetProcAddress(shCore, "SetProcessDpiAwareness");

		if (setProcessDpiAwareness != nullptr)
		{
			setProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
		}

		FreeLibrary(shCore);
	}
}

HWND VulkanRTBase::setupWindow(HINSTANCE hinstance, WNDPROC wndproc)
{
	this->windowInstance = hinstance;

	WNDCLASSEX wndClass;

	wndClass.cbSize = sizeof(WNDCLASSEX);
	wndClass.style = CS_HREDRAW | CS_VREDRAW;
	wndClass.lpfnWndProc = wndproc;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = hinstance;
	wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wndClass.lpszMenuName = NULL;
	wndClass.lpszClassName = name.c_str();
	wndClass.hIconSm = LoadIcon(NULL, IDI_WINLOGO);

	if (!RegisterClassEx(&wndClass))
	{
		std::cout << "Could not register window class!\n";
		fflush(stdout);
		exit(1);
	}

	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

	if (settings.fullscreen)
	{
		if ((width != (uint32_t)screenWidth) && (height != (uint32_t)screenHeight))
		{
			DEVMODE dmScreenSettings;
			memset(&dmScreenSettings, 0, sizeof(dmScreenSettings));
			dmScreenSettings.dmSize       = sizeof(dmScreenSettings);
			dmScreenSettings.dmPelsWidth  = width;
			dmScreenSettings.dmPelsHeight = height;
			dmScreenSettings.dmBitsPerPel = 32;
			dmScreenSettings.dmFields     = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
			if (ChangeDisplaySettings(&dmScreenSettings, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
			{
				if (MessageBox(NULL, "Fullscreen Mode not supported!\n Switch to window mode?", "Error", MB_YESNO | MB_ICONEXCLAMATION) == IDYES)
				{
					settings.fullscreen = false;
				}
				else
				{
					return nullptr;
				}
			}
			screenWidth = width;
			screenHeight = height;
		}

	}

	DWORD dwExStyle;
	DWORD dwStyle;

	if (settings.fullscreen)
	{
		dwExStyle = WS_EX_APPWINDOW;
		dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	}
	else
	{
		dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
		dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	}

	RECT windowRect;
	windowRect.left = 0L;
	windowRect.top = 0L;
	windowRect.right = settings.fullscreen ? (long)screenWidth : (long)width;
	windowRect.bottom = settings.fullscreen ? (long)screenHeight : (long)height;

	AdjustWindowRectEx(&windowRect, dwStyle, FALSE, dwExStyle);

	std::string windowTitle = getWindowTitle();
	window = CreateWindowEx(0,
		name.c_str(),
		windowTitle.c_str(),
		dwStyle | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		0,
		0,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		NULL,
		NULL,
		hinstance,
		NULL);

	if (!settings.fullscreen)
	{
		// Center on screen
		uint32_t x = (GetSystemMetrics(SM_CXSCREEN) - windowRect.right) / 2;
		uint32_t y = (GetSystemMetrics(SM_CYSCREEN) - windowRect.bottom) / 2;
		SetWindowPos(window, 0, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	}

	if (!window)
	{
		printf("Could not create window!\n");
		fflush(stdout);
		return nullptr;
	}

	ShowWindow(window, SW_SHOW);
	SetForegroundWindow(window);
	SetFocus(window);

	return window;
}

const char* cameraTypeToString(Camera::CameraType type) {
	switch (type) {
	case Camera::CameraType::lookat: return "lookat";
	case Camera::CameraType::firstperson: return "firstperson";
	case Camera::CameraType::SG_camera: return "SG_camera";
	default: return "Unknown";
	}
}

void VulkanRTBase::handleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CLOSE:
		prepared = false;
		DestroyWindow(hWnd);
		PostQuitMessage(0);
		break;
	case WM_PAINT:
		ValidateRect(window, NULL);
		break;
	case WM_KEYDOWN:
		switch (wParam)
		{
		case KEY_P:
			paused = !paused;
			break;
		case KEY_F1:
			UIOverlay.visible = !UIOverlay.visible;
			UIOverlay.updated = true;
			break;
		case KEY_F2:
			camera.type = static_cast<Camera::CameraType>((static_cast<int>(camera.type) + 1) % static_cast<int>(Camera::cameraSize));
			printf("camera type: %s\n", cameraTypeToString(camera.type));
			break;
		case KEY_ESCAPE:
			PostQuitMessage(0);
			break;
		}

		if (camera.type == Camera::firstperson || camera.type == Camera::SG_camera)
		{
			switch (wParam)
			{
			case KEY_W:
				camera.keys.forward = true;
				break;
			case KEY_S:
				camera.keys.backward = true;
				break;
			case KEY_A:
				camera.keys.left = true;
				break;
			case KEY_D:
				camera.keys.right = true;
				break;
			case KEY_P:
				printf("camera.setTranslation(glm::vec3(%f, %f, %f));\n", camera.position.x, camera.position.y, camera.position.z);
				printf("camera.setRotation(glm::vec3(%f, %f, %f));\n", camera.rotation.x, camera.rotation.y, camera.rotation.z);
				break;
			case KEY_E:
				camera.keys.up = true;
				break;
			case KEY_Q:
				camera.keys.down = true;
				break;
			case KEY_R:
				camera.setRotation(glm::vec3(-3.199999, -88.599998, 0.000000));
				camera.setTranslation(glm::vec3(-4.211443, 1.331187, -0.232099));
				break;
			case KEY_UP:
				camera.rotate(glm::vec3(1.0f, 0.0f, 0.0f));
				viewUpdated = true;
				break;
			case KEY_DOWN:
				camera.rotate(glm::vec3(-1.0f, 0.0f, 0.0f));
				viewUpdated = true;
				break;
			case KEY_LEFT:
				camera.rotate(glm::vec3(0.0f, 1.0f, 0.0f));
				viewUpdated = true;
				break;
			case KEY_RIGHT:
				camera.rotate(glm::vec3(0.0f, -1.0f, 0.0f));
				viewUpdated = true;
				break;
			case KEY_HYPHEN:
				camera.setMovementSpeed(camera.movementSpeed - 0.5f);
				printf("MovementSpeed (W,A,S,D,E,Q) %f\n", camera.movementSpeed);
				break;
			case KEY_EQUAL:
				camera.setMovementSpeed(camera.movementSpeed + 0.5f);
				printf("MovementSpeed (W,A,S,D,E,Q) %f\n", camera.movementSpeed);
				break;
			case KEY_LEFTBRACKET:
				camera.setRotationSpeed(camera.rotationSpeed - 0.5f);
				printf("setRotationSpeed (Mouse left,middle,right) %f\n", camera.rotationSpeed);
				break;
			case KEY_RIGHTBRACKET:
				camera.setRotationSpeed(camera.rotationSpeed + 0.5f);
				printf("setRotationSpeed (Mouse left,middle,right) %f\n", camera.rotationSpeed);
				break;
			}

		}

		keyPressed((uint32_t)wParam);
		break;
	case WM_KEYUP:
		if (camera.type == Camera::firstperson || camera.type == Camera::SG_camera)
		{
			switch (wParam)
			{
			case KEY_W:
				camera.keys.forward = false;
				break;
			case KEY_S:
				camera.keys.backward = false;
				break;
			case KEY_A:
				camera.keys.left = false;
				break;
			case KEY_D:
				camera.keys.right = false;
				break;
			case KEY_E:
				camera.keys.up = false;
				break;
			case KEY_Q:
				camera.keys.down = false;
				break;
			}
		}
		break;
	case WM_LBUTTONDOWN:
		mouseState.position = glm::vec2((float)LOWORD(lParam), (float)HIWORD(lParam));
		mouseState.buttons.left = true;
		break;
	case WM_RBUTTONDOWN:
		mouseState.position = glm::vec2((float)LOWORD(lParam), (float)HIWORD(lParam));
		mouseState.buttons.right = true;
		break;
	case WM_MBUTTONDOWN:
		mouseState.position = glm::vec2((float)LOWORD(lParam), (float)HIWORD(lParam));
		mouseState.buttons.middle = true;
		break;
	case WM_LBUTTONUP:
		mouseState.buttons.left = false;
		break;
	case WM_RBUTTONUP:
		mouseState.buttons.right = false;
		break;
	case WM_MBUTTONUP:
		mouseState.buttons.middle = false;
		break;
	case WM_MOUSEWHEEL:
	{
		
		short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);

		if (camera.type == Camera::SG_camera) {
#if Y_IS_UP
			glm::vec3 uVec = glm::vec3(1.0f, 0.0f, 0.0f);
			glm::vec3 vVec = glm::vec3(0.0f, 1.0f, 0.0f);
			glm::vec3 nVec = glm::vec3(0.0f, 0.0f, 1.0f);
#else N_IS_UP
			glm::vec3 uVec = glm::vec3(1.0f, 0.0f, 0.0f);
			glm::vec3 vVec = glm::vec3(0.0f, 0.0f, 1.0f);
			glm::vec3 nVec = glm::vec3(0.0f, -1.0f, 0.0f);
#endif

			if (glm::radians(camera.rotation.y) != 0)
				camera.rotateAxis(glm::radians(camera.rotation.y), &vVec, &nVec, &uVec);
			if (glm::radians(camera.rotation.x) != 0)
				camera.rotateAxis(glm::radians(camera.rotation.x), &uVec, &vVec, &nVec);
			if (glm::radians(camera.rotation.z) != 0)
				camera.rotateAxis(glm::radians(camera.rotation.z), &nVec, &uVec, &vVec);

			float z = wheelDelta * 0.01f;
			camera.position -= glm::vec3(nVec.x * z, nVec.y * z, nVec.z * z);
		}

		else camera.translate(glm::vec3(0.0f, 0.0f, (float)wheelDelta * 0.005f));

		viewUpdated = true;
		break;
	}
	case WM_MOUSEMOVE:
	{
		handleMouseMove(LOWORD(lParam), HIWORD(lParam));
		break;
	}
	case WM_SIZE:
		if ((prepared) && (wParam != SIZE_MINIMIZED))
		{
			if ((resizing) || ((wParam == SIZE_MAXIMIZED) || (wParam == SIZE_RESTORED)))
			{
				destWidth = LOWORD(lParam);
				destHeight = HIWORD(lParam);
				windowResize();
			}
		}
		break;
	case WM_GETMINMAXINFO:
	{
		LPMINMAXINFO minMaxInfo = (LPMINMAXINFO)lParam;
		minMaxInfo->ptMinTrackSize.x = 64;
		minMaxInfo->ptMinTrackSize.y = 64;
		break;
	}
	case WM_ENTERSIZEMOVE:
		resizing = true;
		break;
	case WM_EXITSIZEMOVE:
		resizing = false;
		break;
	}

	OnHandleMessage(hWnd, uMsg, wParam, lParam);
}
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
int32_t VulkanRTBase::handleAppInput(struct android_app* app, AInputEvent* event)
{
	VulkanRTBase* vulkanRTBase = reinterpret_cast<VulkanRTBase*>(app->userData);

	if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION)
	{
		int32_t eventSource = AInputEvent_getSource(event);
		switch (eventSource) {
			case AINPUT_SOURCE_JOYSTICK: {
				// Left thumbstick
				vulkanRTBase->gamePadState.axisLeft.x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
				vulkanRTBase->gamePadState.axisLeft.y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
				// Right thumbstick
				vulkanRTBase->gamePadState.axisRight.x = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
				vulkanRTBase->gamePadState.axisRight.y = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
				break;
			}

			case AINPUT_SOURCE_TOUCHSCREEN: {
				int32_t action = AMotionEvent_getAction(event);
				//int32_t classification = AMotionEvent_getClassification(event);
				int32_t pointerCount = AMotionEvent_getPointerCount(event);

				if (/*classification == AMOTION_EVENT_CLASSIFICATION_PINCH &&*/ pointerCount >= 2) {
					float x1 = AMotionEvent_getX(event, 0);
					float y1 = AMotionEvent_getY(event, 0);
					float x2 = AMotionEvent_getX(event, 1);
					float y2 = AMotionEvent_getY(event, 1);

					// check distance between two fingers
					float dx = x2 - x1;
					float dy = y2 - y1;
					float currentDistance = sqrt(dx * dx + dy * dy);

					if ((action & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_POINTER_DOWN) {
						// initial distance
						vulkanRTBase->initialDistance = currentDistance;
					}
					else if ((action & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_MOVE) {
						if (vulkanRTBase->initialDistance > 0) {
							if (currentDistance > vulkanRTBase->initialDistance * 1.01f) {
								//zoom in
                                float fov = vulkanRTBase->camera.getFov();
                                fov = fov - 3;
                                if (fov < 10.0f) fov = 10.0f;
								vulkanRTBase->camera.setPerspective(fov, (float)vulkanRTBase->width / (float)vulkanRTBase->height, 0.1f, 5000.0f);
							}
							else if (currentDistance < vulkanRTBase->initialDistance * 0.99f) {
								//zoom out
								float fov = vulkanRTBase->camera.getFov();
                                fov = fov + 3;
                                if (fov > 90.0f) fov = 90.0f;
								vulkanRTBase->camera.setPerspective(fov, (float)vulkanRTBase->width / (float)vulkanRTBase->height, 0.1f, 5000.0f);
							}
						}
                        vulkanRTBase->initialDistance = currentDistance;
					}
					else if ((action & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_POINTER_UP) {
						// reset distance when fingers are off
                        vulkanRTBase->initialDistance = -1.0f;
					}

                    return 1;
				}

                if(vulkanRTBase->initialDistance == -1.0f) {
                    if(action != AMOTION_EVENT_ACTION_UP) return 1;
                    else vulkanRTBase->initialDistance = 0.0f;
                }

				switch (action) {
					case AMOTION_EVENT_ACTION_UP: {
//                        if (vulkanRTBase->settings.overlay) {
//                            ImGuiIO& io = ImGui::GetIO();
//                            io.MouseDown[1] = false;
//                        }
						vulkanRTBase->lastTapTime = AMotionEvent_getEventTime(event);
						vulkanRTBase->touchPos.x = AMotionEvent_getX(event, 0);
						vulkanRTBase->touchPos.y = AMotionEvent_getY(event, 0);
						vulkanRTBase->touchTimer = 0.0;
						vulkanRTBase->touchDown = false;
						vulkanRTBase->doubleTouchDown = false;
						vulkanRTBase->camera.keys.up = false;
						vulkanRTBase->camera.keys.down = false;
						vulkanRTBase->camera.keys.forward = false;
						vulkanRTBase->camera.keys.backward = false;
						vulkanRTBase->camera.keys.right = false;
						vulkanRTBase->camera.keys.left = false;
						vulkanRTBase->mouseState.buttons.left = false;
						// Detect single tap
						/*int64_t eventTime = AMotionEvent_getEventTime(event);
						int64_t downTime = AMotionEvent_getDownTime(event);
						if (eventTime - downTime <= vks::android::TAP_TIMEOUT) {
							float deadZone = (160.f / vks::android::screenDensity) * vks::android::TAP_SLOP * vks::android::TAP_SLOP;
							float x = AMotionEvent_getX(event, 0) - vulkanRTBase->touchPos.x;
							float y = AMotionEvent_getY(event, 0) - vulkanRTBase->touchPos.y;
							if ((x * x + y * y) < deadZone) {
								vulkanRTBase->mouseState.buttons.left = true;
							}
						};*/

						return 1;
						break;
					}
					case AMOTION_EVENT_ACTION_DOWN: {
//                        if (vulkanRTBase->settings.overlay) {
//                            ImGuiIO& io = ImGui::GetIO();
//                            //io.MouseDown[1] = true;
//                            //io.MousePos = ImVec2(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
//						}
                        // Detect double tap
                        int64_t eventTime = AMotionEvent_getEventTime(event);
                        if (eventTime - vulkanRTBase->lastTapTime <=
                            vks::android::DOUBLE_TAP_TIMEOUT) {
                            float deadZone = (160.f / vks::android::screenDensity) *
                                             vks::android::DOUBLE_TAP_SLOP *
                                             vks::android::DOUBLE_TAP_SLOP;
                            float x = AMotionEvent_getX(event, 0) - vulkanRTBase->touchPos.x;
                            float y = AMotionEvent_getY(event, 0) - vulkanRTBase->touchPos.y;
                            if ((x * x + y * y) < deadZone) {
                                //vulkanRTBase->keyPressed(TOUCH_DOUBLE_TAP); find this function is empty
                                vulkanRTBase->doubleTouchDown = true;
                                vulkanRTBase->touchDown = false;
                            }
                        } else {
                            vulkanRTBase->touchDown = true;
                        }
						vulkanRTBase->mouseState.buttons.left = true;
                        vulkanRTBase->touchPos.x = AMotionEvent_getX(event, 0);
                        vulkanRTBase->touchPos.y = AMotionEvent_getY(event, 0);
                        vulkanRTBase->mouseState.position.x = AMotionEvent_getX(event, 0);
                        vulkanRTBase->mouseState.position.y = AMotionEvent_getY(event, 0);
                        break;
					}
					case AMOTION_EVENT_ACTION_MOVE: {
						bool handled = false;
						if (vulkanRTBase->settings.overlay) {
							ImGuiIO& io = ImGui::GetIO();
							handled = io.WantCaptureMouse && vulkanRTBase->UIOverlay.visible;
                            //io.MousePos = ImVec2(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
						}
						if (!handled) {
							int32_t eventX = AMotionEvent_getX(event, 0);
							int32_t eventY = AMotionEvent_getY(event, 0);

							float x = vulkanRTBase->touchPos.x - eventX;
							float y = vulkanRTBase->touchPos.y - eventY;
							float deadZone = vks::android::DOUBLE_TAP_SLOP * vks::android::DOUBLE_TAP_SLOP;

							if ((x * x + y * y) > deadZone) {
								//vulkanRTBase->keyPressed(TOUCH_DOUBLE_TAP); find this function is empty
								vulkanRTBase->doubleTouchDown = false;
								vulkanRTBase->touchDown = false;
							}

							if (eventX < ANativeWindow_getWidth(app->window) / 2) {
								if (((x < 0) ? -x : x) > vks::android::DOUBLE_TAP_SLOP) {
									bool movX = x > 0 ? 0 : 1;
									vulkanRTBase->camera.keys.right = movX;
									vulkanRTBase->camera.keys.left = !movX;
								}

								if (((y < 0) ? -y : y) > vks::android::DOUBLE_TAP_SLOP) {
									bool movZ = y > 0 ? 1 : 0;
									vulkanRTBase->camera.keys.forward = movZ;
									vulkanRTBase->camera.keys.backward = !movZ;
								}
							}
							else {
								float deltaX = (float)(y)*vulkanRTBase->camera.rotationSpeed * 0.5f;
								float deltaY = (float)(x)*vulkanRTBase->camera.rotationSpeed * 0.5f;

								vulkanRTBase->camera.rotate(glm::vec3(deltaX, 0.0f, 0.0f));
								vulkanRTBase->camera.rotate(glm::vec3(0.0f, deltaY, 0.0f));

								vulkanRTBase->touchPos.x = eventX;
								vulkanRTBase->touchPos.y = eventY;
							}
						}
						break;
					}
					default:
						return 1;
						break;
				}
			}

			return 1;
		}
	}

	if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY)
	{
		int32_t keyCode = AKeyEvent_getKeyCode((const AInputEvent*)event);
		int32_t action = AKeyEvent_getAction((const AInputEvent*)event);
		int32_t button = 0;

		if (action == AKEY_EVENT_ACTION_UP)
			return 0;

		switch (keyCode)
		{
		case AKEYCODE_BUTTON_A:
			vulkanRTBase->keyPressed(GAMEPAD_BUTTON_A);
			break;
		case AKEYCODE_BUTTON_B:
			vulkanRTBase->keyPressed(GAMEPAD_BUTTON_B);
			break;
		case AKEYCODE_BUTTON_X:
			vulkanRTBase->keyPressed(GAMEPAD_BUTTON_X);
			break;
		case AKEYCODE_BUTTON_Y:
			vulkanRTBase->keyPressed(GAMEPAD_BUTTON_Y);
			break;
		case AKEYCODE_1:							// support keyboards with no function keys
		case AKEYCODE_F1:
		case AKEYCODE_BUTTON_L1:
			vulkanRTBase->UIOverlay.visible = !vulkanRTBase->UIOverlay.visible;
			vulkanRTBase->UIOverlay.updated = true;
			break;
		case AKEYCODE_BUTTON_R1:
			vulkanRTBase->keyPressed(GAMEPAD_BUTTON_R1);
			break;

		case AKEYCODE_P:
		case AKEYCODE_BUTTON_START:
			vulkanRTBase->paused = !vulkanRTBase->paused;
			break;
		default:
			vulkanRTBase->keyPressed(keyCode);		// handle example-specific key press events
			break;
		};

		LOGD("Button %d pressed", keyCode);
	}

	return 0;
}

void VulkanRTBase::handleAppCommand(android_app * app, int32_t cmd)
{
	assert(app->userData != NULL);
	VulkanRTBase* vulkanRTBase = reinterpret_cast<VulkanRTBase*>(app->userData);
	switch (cmd)
	{
	case APP_CMD_SAVE_STATE:
		LOGD("APP_CMD_SAVE_STATE");
		/*
		vulkanRTBase->app->savedState = malloc(sizeof(struct saved_state));
		*((struct saved_state*)vulkanRTBase->app->savedState) = vulkanRTBase->state;
		vulkanRTBase->app->savedStateSize = sizeof(struct saved_state);
		*/
		break;
	case APP_CMD_INIT_WINDOW:
		LOGD("APP_CMD_INIT_WINDOW");
		if (androidApp->window != NULL)
		{
			if (vulkanRTBase->initVulkan()) {
				vulkanRTBase->prepare();
				assert(vulkanRTBase->prepared);
			}
			else {
				LOGE("Could not initialize Vulkan, exiting!");
				androidApp->destroyRequested = 1;
			}
		}
		else
		{
			LOGE("No window assigned!");
		}
		break;
	case APP_CMD_LOST_FOCUS:
		LOGD("APP_CMD_LOST_FOCUS");
		vulkanRTBase->focused = false;
		break;
	case APP_CMD_GAINED_FOCUS:
		LOGD("APP_CMD_GAINED_FOCUS");
		vulkanRTBase->focused = true;
		break;
	case APP_CMD_TERM_WINDOW:
		// Window is hidden or closed, clean up resources
		LOGD("APP_CMD_TERM_WINDOW");
		if (vulkanRTBase->prepared) {
			vulkanRTBase->swapChain.cleanup();
		}
		break;
	}
}

void* VulkanRTBase::setupWindow(void* view)
{
	this->view = view;
	return view;
}

//void VulkanRTBase::displayLinkOutputCb()
//{
//	if (prepared)
//		nextFrame();
//}

void VulkanRTBase::mouseDragged(float x, float y)
{
	handleMouseMove(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
}

void VulkanRTBase::windowWillResize(float x, float y)
{
	resizing = true;
	if (prepared)
	{
		destWidth = x;
		destHeight = y;
		windowResize();
	}
}

void VulkanRTBase::windowDidResize()
{
	resizing = false;
}
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
IDirectFBSurface *VulkanRTBase::setupWindow()
{
	DFBResult ret;
	int posx = 0, posy = 0;

	ret = DirectFBInit(NULL, NULL);
	if (ret)
	{
		std::cout << "Could not initialize DirectFB!\n";
		fflush(stdout);
		exit(1);
	}

	ret = DirectFBCreate(&dfb);
	if (ret)
	{
		std::cout << "Could not create main interface of DirectFB!\n";
		fflush(stdout);
		exit(1);
	}

	ret = dfb->GetDisplayLayer(dfb, DLID_PRIMARY, &layer);
	if (ret)
	{
		std::cout << "Could not get DirectFB display layer interface!\n";
		fflush(stdout);
		exit(1);
	}

	DFBDisplayLayerConfig layer_config;
	ret = layer->GetConfiguration(layer, &layer_config);
	if (ret)
	{
		std::cout << "Could not get DirectFB display layer configuration!\n";
		fflush(stdout);
		exit(1);
	}

	if (settings.fullscreen)
	{
		width = layer_config.width;
		height = layer_config.height;
	}
	else
	{
		if (layer_config.width > width)
			posx = (layer_config.width - width) / 2;
		if (layer_config.height > height)
			posy = (layer_config.height - height) / 2;
	}

	DFBWindowDescription desc;
	desc.flags = (DFBWindowDescriptionFlags)(DWDESC_WIDTH | DWDESC_HEIGHT | DWDESC_POSX | DWDESC_POSY);
	desc.width = width;
	desc.height = height;
	desc.posx = posx;
	desc.posy = posy;
	ret = layer->CreateWindow(layer, &desc, &window);
	if (ret)
	{
		std::cout << "Could not create DirectFB window interface!\n";
		fflush(stdout);
		exit(1);
	}

	ret = window->GetSurface(window, &surface);
	if (ret)
	{
		std::cout << "Could not get DirectFB surface interface!\n";
		fflush(stdout);
		exit(1);
	}

	ret = window->CreateEventBuffer(window, &event_buffer);
	if (ret)
	{
		std::cout << "Could not create DirectFB event buffer interface!\n";
		fflush(stdout);
		exit(1);
	}

	ret = window->SetOpacity(window, 0xFF);
	if (ret)
	{
		std::cout << "Could not set DirectFB window opacity!\n";
		fflush(stdout);
		exit(1);
	}

	return surface;
}

void VulkanRTBase::handleEvent(const DFBWindowEvent *event)
{
	switch (event->type)
	{
	case DWET_CLOSE:
		quit = true;
		break;
	case DWET_MOTION:
		handleMouseMove(event->x, event->y);
		break;
	case DWET_BUTTONDOWN:
		switch (event->button)
		{
		case DIBI_LEFT:
			mouseState.buttons.left = true;
			break;
		case DIBI_MIDDLE:
			mouseState.buttons.middle = true;
			break;
		case DIBI_RIGHT:
			mouseState.buttons.right = true;
			break;
		default:
			break;
		}
		break;
	case DWET_BUTTONUP:
		switch (event->button)
		{
		case DIBI_LEFT:
			mouseState.buttons.left = false;
			break;
		case DIBI_MIDDLE:
			mouseState.buttons.middle = false;
			break;
		case DIBI_RIGHT:
			mouseState.buttons.right = false;
			break;
		default:
			break;
		}
		break;
	case DWET_KEYDOWN:
		switch (event->key_symbol)
		{
			case KEY_W:
				camera.keys.up = true;
				break;
			case KEY_S:
				camera.keys.down = true;
				break;
			case KEY_A:
				camera.keys.left = true;
				break;
			case KEY_D:
				camera.keys.right = true;
				break;
			case KEY_P:
				paused = !paused;
				break;
			case KEY_F1:
				UIOverlay.visible = !UIOverlay.visible;
				UIOverlay.updated = true;
				break;
			default:
				break;
		}
		break;
	case DWET_KEYUP:
		switch (event->key_symbol)
		{
			case KEY_W:
				camera.keys.up = false;
				break;
			case KEY_S:
				camera.keys.down = false;
				break;
			case KEY_A:
				camera.keys.left = false;
				break;
			case KEY_D:
				camera.keys.right = false;
				break;
			case KEY_ESCAPE:
				quit = true;
				break;
			default:
				break;
		}
		keyPressed(event->key_symbol);
		break;
	case DWET_SIZE:
		destWidth = event->w;
		destHeight = event->h;
		windowResize();
		break;
	default:
		break;
	}
}
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
/*static*/void VulkanRTBase::registryGlobalCb(void *data,
		wl_registry *registry, uint32_t name, const char *interface,
		uint32_t version)
{
	VulkanRTBase *self = reinterpret_cast<VulkanRTBase *>(data);
	self->registryGlobal(registry, name, interface, version);
}

/*static*/void VulkanRTBase::seatCapabilitiesCb(void *data, wl_seat *seat,
		uint32_t caps)
{
	VulkanRTBase *self = reinterpret_cast<VulkanRTBase *>(data);
	self->seatCapabilities(seat, caps);
}

/*static*/void VulkanRTBase::pointerEnterCb(void *data,
		wl_pointer *pointer, uint32_t serial, wl_surface *surface,
		wl_fixed_t sx, wl_fixed_t sy)
{
}

/*static*/void VulkanRTBase::pointerLeaveCb(void *data,
		wl_pointer *pointer, uint32_t serial, wl_surface *surface)
{
}

/*static*/void VulkanRTBase::pointerMotionCb(void *data,
		wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
	VulkanRTBase *self = reinterpret_cast<VulkanRTBase *>(data);
	self->pointerMotion(pointer, time, sx, sy);
}
void VulkanRTBase::pointerMotion(wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy)
{
	handleMouseMove(wl_fixed_to_int(sx), wl_fixed_to_int(sy));
}

/*static*/void VulkanRTBase::pointerButtonCb(void *data,
		wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button,
		uint32_t state)
{
	VulkanRTBase *self = reinterpret_cast<VulkanRTBase *>(data);
	self->pointerButton(pointer, serial, time, button, state);
}

void VulkanRTBase::pointerButton(struct wl_pointer *pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
	switch (button)
	{
	case BTN_LEFT:
		mouseState.buttons.left = !!state;
		break;
	case BTN_MIDDLE:
		mouseState.buttons.middle = !!state;
		break;
	case BTN_RIGHT:
		mouseState.buttons.right = !!state;
		break;
	default:
		break;
	}
}

/*static*/void VulkanRTBase::pointerAxisCb(void *data,
		wl_pointer *pointer, uint32_t time, uint32_t axis,
		wl_fixed_t value)
{
	VulkanRTBase *self = reinterpret_cast<VulkanRTBase *>(data);
	self->pointerAxis(pointer, time, axis, value);
}

void VulkanRTBase::pointerAxis(wl_pointer *pointer, uint32_t time,
		uint32_t axis, wl_fixed_t value)
{
	double d = wl_fixed_to_double(value);
	switch (axis)
	{
	case REL_X:
		camera.translate(glm::vec3(0.0f, 0.0f, d * 0.005f));
		viewUpdated = true;
		break;
	default:
		break;
	}
}

/*static*/void VulkanRTBase::keyboardKeymapCb(void *data,
		struct wl_keyboard *keyboard, uint32_t format, int fd, uint32_t size)
{
}

/*static*/void VulkanRTBase::keyboardEnterCb(void *data,
		struct wl_keyboard *keyboard, uint32_t serial,
		struct wl_surface *surface, struct wl_array *keys)
{
}

/*static*/void VulkanRTBase::keyboardLeaveCb(void *data,
		struct wl_keyboard *keyboard, uint32_t serial,
		struct wl_surface *surface)
{
}

/*static*/void VulkanRTBase::keyboardKeyCb(void *data,
		struct wl_keyboard *keyboard, uint32_t serial, uint32_t time,
		uint32_t key, uint32_t state)
{
	VulkanRTBase *self = reinterpret_cast<VulkanRTBase *>(data);
	self->keyboardKey(keyboard, serial, time, key, state);
}

void VulkanRTBase::keyboardKey(struct wl_keyboard *keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	switch (key)
	{
	case KEY_W:
		camera.keys.up = !!state;
		break;
	case KEY_S:
		camera.keys.down = !!state;
		break;
	case KEY_A:
		camera.keys.left = !!state;
		break;
	case KEY_D:
		camera.keys.right = !!state;
		break;
	case KEY_P:
		if (state)
			paused = !paused;
		break;
	case KEY_F1:
		if (state) {
			UIOverlay.visible = !UIOverlay.visible;
			UIOverlay.updated = true;
		}
		break;
	case KEY_ESCAPE:
		quit = true;
		break;
	}

	if (state)
		keyPressed(key);
}

/*static*/void VulkanRTBase::keyboardModifiersCb(void *data,
		struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed,
		uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
}

void VulkanRTBase::seatCapabilities(wl_seat *seat, uint32_t caps)
{
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer)
	{
		pointer = wl_seat_get_pointer(seat);
		static const struct wl_pointer_listener pointer_listener =
		{ pointerEnterCb, pointerLeaveCb, pointerMotionCb, pointerButtonCb,
				pointerAxisCb, };
		wl_pointer_add_listener(pointer, &pointer_listener, this);
	}
	else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && pointer)
	{
		wl_pointer_destroy(pointer);
		pointer = nullptr;
	}

	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !keyboard)
	{
		keyboard = wl_seat_get_keyboard(seat);
		static const struct wl_keyboard_listener keyboard_listener =
		{ keyboardKeymapCb, keyboardEnterCb, keyboardLeaveCb, keyboardKeyCb,
				keyboardModifiersCb, };
		wl_keyboard_add_listener(keyboard, &keyboard_listener, this);
	}
	else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && keyboard)
	{
		wl_keyboard_destroy(keyboard);
		keyboard = nullptr;
	}
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
	xdg_wm_base_pong(shell, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	xdg_wm_base_ping,
};

void VulkanRTBase::registryGlobal(wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
	if (strcmp(interface, "wl_compositor") == 0)
	{
		compositor = (wl_compositor *) wl_registry_bind(registry, name,
				&wl_compositor_interface, 3);
	}
	else if (strcmp(interface, "xdg_wm_base") == 0)
	{
		shell = (xdg_wm_base *) wl_registry_bind(registry, name,
				&xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(shell, &xdg_wm_base_listener, nullptr);
	}
	else if (strcmp(interface, "wl_seat") == 0)
	{
		seat = (wl_seat *) wl_registry_bind(registry, name, &wl_seat_interface,
				1);

		static const struct wl_seat_listener seat_listener =
		{ seatCapabilitiesCb, };
		wl_seat_add_listener(seat, &seat_listener, this);
	}
}

/*static*/void VulkanRTBase::registryGlobalRemoveCb(void *data,
		struct wl_registry *registry, uint32_t name)
{
}

void VulkanRTBase::initWaylandConnection()
{
	display = wl_display_connect(NULL);
	if (!display)
	{
		std::cout << "Could not connect to Wayland display!\n";
		fflush(stdout);
		exit(1);
	}

	registry = wl_display_get_registry(display);
	if (!registry)
	{
		std::cout << "Could not get Wayland registry!\n";
		fflush(stdout);
		exit(1);
	}

	static const struct wl_registry_listener registry_listener =
	{ registryGlobalCb, registryGlobalRemoveCb };
	wl_registry_add_listener(registry, &registry_listener, this);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);
	if (!compositor || !shell)
	{
		std::cout << "Could not bind Wayland protocols!\n";
		fflush(stdout);
		exit(1);
	}
	if (!seat)
	{
		std::cout << "WARNING: Input handling not available!\n";
		fflush(stdout);
	}
}

void VulkanRTBase::setSize(int width, int height)
{
	if (width <= 0 || height <= 0)
		return;

	destWidth = width;
	destHeight = height;

	windowResize();
}

static void
xdg_surface_handle_configure(void *data, struct xdg_surface *surface,
			     uint32_t serial)
{
	VulkanRTBase *base = (VulkanRTBase *) data;

	xdg_surface_ack_configure(surface, serial);
	base->configured = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	xdg_surface_handle_configure,
};


static void
xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *toplevel,
			      int32_t width, int32_t height,
			      struct wl_array *states)
{
	VulkanRTBase *base = (VulkanRTBase *) data;

	base->setSize(width, height);
}

static void
xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
	VulkanRTBase *base = (VulkanRTBase *) data;

	base->quit = true;
}


static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	xdg_toplevel_handle_configure,
	xdg_toplevel_handle_close,
};


struct xdg_surface *VulkanRTBase::setupWindow()
{
	surface = wl_compositor_create_surface(compositor);
	xdg_surface = xdg_wm_base_get_xdg_surface(shell, surface);

	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, this);
	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, this);

	std::string windowTitle = getWindowTitle();
	xdg_toplevel_set_title(xdg_toplevel, windowTitle.c_str());
	wl_surface_commit(surface);
	return xdg_surface;
}

#elif defined(VK_USE_PLATFORM_XCB_KHR)

static inline xcb_intern_atom_reply_t* intern_atom_helper(xcb_connection_t *conn, bool only_if_exists, const char *str)
{
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(conn, only_if_exists, strlen(str), str);
	return xcb_intern_atom_reply(conn, cookie, NULL);
}

// Set up a window using XCB and request event types
xcb_window_t VulkanRTBase::setupWindow()
{
	uint32_t value_mask, value_list[32];

	window = xcb_generate_id(connection);

	value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	value_list[0] = screen->black_pixel;
	value_list[1] =
		XCB_EVENT_MASK_KEY_RELEASE |
		XCB_EVENT_MASK_KEY_PRESS |
		XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		XCB_EVENT_MASK_POINTER_MOTION |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE;

	if (settings.fullscreen)
	{
		width = destWidth = screen->width_in_pixels;
		height = destHeight = screen->height_in_pixels;
	}

	xcb_create_window(connection,
		XCB_COPY_FROM_PARENT,
		window, screen->root,
		0, 0, width, height, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual,
		value_mask, value_list);

	/* Magic code that will send notification when window is destroyed */
	xcb_intern_atom_reply_t* reply = intern_atom_helper(connection, true, "WM_PROTOCOLS");
	atom_wm_delete_window = intern_atom_helper(connection, false, "WM_DELETE_WINDOW");

	xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
		window, (*reply).atom, 4, 32, 1,
		&(*atom_wm_delete_window).atom);

	std::string windowTitle = getWindowTitle();
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
		window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
		title.size(), windowTitle.c_str());

	free(reply);

	/**
	 * Set the WM_CLASS property to display
	 * title in dash tooltip and application menu
	 * on GNOME and other desktop environments
	 */
	std::string wm_class;
	wm_class = wm_class.insert(0, name);
	wm_class = wm_class.insert(name.size(), 1, '\0');
	wm_class = wm_class.insert(name.size() + 1, title);
	wm_class = wm_class.insert(wm_class.size(), 1, '\0');
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, wm_class.size() + 2, wm_class.c_str());

	if (settings.fullscreen)
	{
		xcb_intern_atom_reply_t *atom_wm_state = intern_atom_helper(connection, false, "_NET_WM_STATE");
		xcb_intern_atom_reply_t *atom_wm_fullscreen = intern_atom_helper(connection, false, "_NET_WM_STATE_FULLSCREEN");
		xcb_change_property(connection,
				XCB_PROP_MODE_REPLACE,
				window, atom_wm_state->atom,
				XCB_ATOM_ATOM, 32, 1,
				&(atom_wm_fullscreen->atom));
		free(atom_wm_fullscreen);
		free(atom_wm_state);
	}

	xcb_map_window(connection, window);

	return(window);
}

// Initialize XCB connection
void VulkanRTBase::initxcbConnection()
{
	const xcb_setup_t *setup;
	xcb_screen_iterator_t iter;
	int scr;

	// xcb_connect always returns a non-NULL pointer to a xcb_connection_t,
	// even on failure. Callers need to use xcb_connection_has_error() to
	// check for failure. When finished, use xcb_disconnect() to close the
	// connection and free the structure.
	connection = xcb_connect(NULL, &scr);
	assert( connection );
	if( xcb_connection_has_error(connection) ) {
		printf("Could not find a compatible Vulkan ICD!\n");
		fflush(stdout);
		exit(1);
	}

	setup = xcb_get_setup(connection);
	iter = xcb_setup_roots_iterator(setup);
	while (scr-- > 0)
		xcb_screen_next(&iter);
	screen = iter.data;
}

void VulkanRTBase::handleEvent(const xcb_generic_event_t *event)
{
	switch (event->response_type & 0x7f)
	{
	case XCB_CLIENT_MESSAGE:
		if ((*(xcb_client_message_event_t*)event).data.data32[0] ==
			(*atom_wm_delete_window).atom) {
			quit = true;
		}
		break;
	case XCB_MOTION_NOTIFY:
	{
		xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;
		handleMouseMove((int32_t)motion->event_x, (int32_t)motion->event_y);
		break;
	}
	break;
	case XCB_BUTTON_PRESS:
	{
		xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
		if (press->detail == XCB_BUTTON_INDEX_1)
			mouseState.buttons.left = true;
		if (press->detail == XCB_BUTTON_INDEX_2)
			mouseState.buttons.middle = true;
		if (press->detail == XCB_BUTTON_INDEX_3)
			mouseState.buttons.right = true;
	}
	break;
	case XCB_BUTTON_RELEASE:
	{
		xcb_button_press_event_t *press = (xcb_button_press_event_t *)event;
		if (press->detail == XCB_BUTTON_INDEX_1)
			mouseState.buttons.left = false;
		if (press->detail == XCB_BUTTON_INDEX_2)
			mouseState.buttons.middle = false;
		if (press->detail == XCB_BUTTON_INDEX_3)
			mouseState.buttons.right = false;
	}
	break;
	case XCB_KEY_PRESS:
	{
		const xcb_key_release_event_t *keyEvent = (const xcb_key_release_event_t *)event;
		switch (keyEvent->detail)
		{
			case KEY_W:
				camera.keys.up = true;
				break;
			case KEY_S:
				camera.keys.down = true;
				break;
			case KEY_A:
				camera.keys.left = true;
				break;
			case KEY_D:
				camera.keys.right = true;
				break;
			case KEY_P:
				paused = !paused;
				break;
			case KEY_F1:
				UIOverlay.visible = !UIOverlay.visible;
				UIOverlay.updated = true;
				break;
		}
	}
	break;
	case XCB_KEY_RELEASE:
	{
		const xcb_key_release_event_t *keyEvent = (const xcb_key_release_event_t *)event;
		switch (keyEvent->detail)
		{
			case KEY_W:
				camera.keys.up = false;
				break;
			case KEY_S:
				camera.keys.down = false;
				break;
			case KEY_A:
				camera.keys.left = false;
				break;
			case KEY_D:
				camera.keys.right = false;
				break;
			case KEY_ESCAPE:
				quit = true;
				break;
		}
		keyPressed(keyEvent->detail);
	}
	break;
	case XCB_DESTROY_NOTIFY:
		quit = true;
		break;
	case XCB_CONFIGURE_NOTIFY:
	{
		const xcb_configure_notify_event_t *cfgEvent = (const xcb_configure_notify_event_t *)event;
		if ((prepared) && ((cfgEvent->width != width) || (cfgEvent->height != height)))
		{
				destWidth = cfgEvent->width;
				destHeight = cfgEvent->height;
				if ((destWidth > 0) && (destHeight > 0))
				{
					windowResize();
				}
		}
	}
	break;
	default:
		break;
	}
}
#elif defined(VK_USE_PLATFORM_SCREEN_QNX)
void VulkanRTBase::handleEvent()
{
	int size[2] = { 0, 0 };
	screen_window_t win;
	static int mouse_buttons = 0;
	int pos[2];
	int val;
	int keyflags;
	int rc;

	while (!screen_get_event(screen_context, screen_event, paused ? ~0 : 0)) {
		rc = screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_TYPE, &val);
		if (rc) {
			printf("Cannot get SCREEN_PROPERTY_TYPE of the event! (%s)\n", strerror(errno));
			fflush(stdout);
			quit = true;
			break;
		}
		if (val == SCREEN_EVENT_NONE) {
			break;
		}
		switch (val) {
			case SCREEN_EVENT_KEYBOARD:
				rc = screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_FLAGS, &keyflags);
				if (rc) {
					printf("Cannot get SCREEN_PROPERTY_FLAGS of the event! (%s)\n", strerror(errno));
					fflush(stdout);
					quit = true;
					break;
				}
				rc = screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_SYM, &val);
				if (rc) {
					printf("Cannot get SCREEN_PROPERTY_SYM of the event! (%s)\n", strerror(errno));
					fflush(stdout);
					quit = true;
					break;
				}
				if ((keyflags & KEY_SYM_VALID) == KEY_SYM_VALID) {
					switch (val) {
						case KEYCODE_ESCAPE:
							quit = true;
							break;
						case KEYCODE_W:
							if (keyflags & KEY_DOWN) {
								camera.keys.up = true;
							} else {
								camera.keys.up = false;
							}
							break;
						case KEYCODE_S:
							if (keyflags & KEY_DOWN) {
								camera.keys.down = true;
							} else {
								camera.keys.down = false;
							}
							break;
						case KEYCODE_A:
							if (keyflags & KEY_DOWN) {
								camera.keys.left = true;
							} else {
								camera.keys.left = false;
							}
							break;
						case KEYCODE_D:
							if (keyflags & KEY_DOWN) {
								camera.keys.right = true;
							} else {
								camera.keys.right = false;
							}
							break;
						case KEYCODE_P:
							paused = !paused;
							break;
						case KEYCODE_F1:
							UIOverlay.visible = !UIOverlay.visible;
							UIOverlay.updated = true;
							break;
						default:
							break;
					}

					if ((keyflags & KEY_DOWN) == KEY_DOWN) {
						if ((val >= 0x20) && (val <= 0xFF)) {
							keyPressed(val);
						}
					}
				}
				break;
			case SCREEN_EVENT_PROPERTY:
				rc = screen_get_event_property_pv(screen_event, SCREEN_PROPERTY_WINDOW, (void **)&win);
				if (rc) {
					printf("Cannot get SCREEN_PROPERTY_WINDOW of the event! (%s)\n", strerror(errno));
					fflush(stdout);
					quit = true;
					break;
				}
				rc = screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_NAME, &val);
				if (rc) {
					printf("Cannot get SCREEN_PROPERTY_NAME of the event! (%s)\n", strerror(errno));
					fflush(stdout);
					quit = true;
					break;
				}
				if (win == screen_window) {
					switch(val) {
						case SCREEN_PROPERTY_SIZE:
							rc = screen_get_window_property_iv(win, SCREEN_PROPERTY_SIZE, size);
							if (rc) {
								printf("Cannot get SCREEN_PROPERTY_SIZE of the window in the event! (%s)\n", strerror(errno));
								fflush(stdout);
								quit = true;
								break;
							}
							width = size[0];
							height = size[1];
							windowResize();
							break;
						default:
							/* We are not interested in any other events for now */
							break;
						}
				}
				break;
			case SCREEN_EVENT_POINTER:
				rc = screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_BUTTONS, &val);
				if (rc) {
					printf("Cannot get SCREEN_PROPERTY_BUTTONS of the event! (%s)\n", strerror(errno));
					fflush(stdout);
					quit = true;
					break;
				}
				if ((mouse_buttons & SCREEN_LEFT_MOUSE_BUTTON) == 0) {
					if ((val & SCREEN_LEFT_MOUSE_BUTTON) == SCREEN_LEFT_MOUSE_BUTTON) {
						mouseState.buttons.left = true;
					}
				} else {
					if ((val & SCREEN_LEFT_MOUSE_BUTTON) == 0) {
						mouseState.buttons.left = false;
					}
				}
				if ((mouse_buttons & SCREEN_RIGHT_MOUSE_BUTTON) == 0) {
					if ((val & SCREEN_RIGHT_MOUSE_BUTTON) == SCREEN_RIGHT_MOUSE_BUTTON) {
						mouseState.buttons.right = true;
					}
				} else {
					if ((val & SCREEN_RIGHT_MOUSE_BUTTON) == 0) {
						mouseState.buttons.right = false;
					}
				}
				if ((mouse_buttons & SCREEN_MIDDLE_MOUSE_BUTTON) == 0) {
					if ((val & SCREEN_MIDDLE_MOUSE_BUTTON) == SCREEN_MIDDLE_MOUSE_BUTTON) {
						mouseState.buttons.middle = true;
					}
				} else {
					if ((val & SCREEN_MIDDLE_MOUSE_BUTTON) == 0) {
						mouseState.buttons.middle = false;
					}
				}
				mouse_buttons = val;

				rc = screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_MOUSE_WHEEL, &val);
				if (rc) {
					printf("Cannot get SCREEN_PROPERTY_MOUSE_WHEEL of the event! (%s)\n", strerror(errno));
					fflush(stdout);
					quit = true;
					break;
				}
				if (val != 0) {
					camera.translate(glm::vec3(0.0f, 0.0f, (float)val * 0.005f));
					viewUpdated = true;
				}

				rc = screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_POSITION, pos);
				if (rc) {
					printf("Cannot get SCREEN_PROPERTY_DISPLACEMENT of the event! (%s)\n", strerror(errno));
					fflush(stdout);
					quit = true;
					break;
				}
				if ((pos[0] != 0) || (pos[1] != 0)) {
					handleMouseMove(pos[0], pos[1]);
				}
				updateOverlay();
				break;
		}
	}
}

void VulkanRTBase::setupWindow()
{
	const char *idstr = name.c_str();
	int size[2];
	int usage = SCREEN_USAGE_VULKAN;
	int rc;

	if (screen_pipeline_set) {
		usage |= SCREEN_USAGE_OVERLAY;
	}

	rc = screen_create_context(&screen_context, 0);
	if (rc) {
		printf("Cannot create QNX Screen context!\n");
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	rc = screen_create_window(&screen_window, screen_context);
	if (rc) {
		printf("Cannot create QNX Screen window!\n");
		fflush(stdout);
		exit(EXIT_FAILURE);
	}
	rc = screen_create_event(&screen_event);
	if (rc) {
		printf("Cannot create QNX Screen event!\n");
		fflush(stdout);
		exit(EXIT_FAILURE);
	}

	/* Set window caption */
	screen_set_window_property_cv(screen_window, SCREEN_PROPERTY_ID_STRING, strlen(idstr), idstr);

	/* Setup VULKAN usage flags */
	rc = screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_USAGE, &usage);
	if (rc) {
		printf("Cannot set SCREEN_USAGE_VULKAN flag!\n");
		fflush(stdout);
		exit(EXIT_FAILURE);
	}

	if ((width == 0) || (height == 0) || (settings.fullscreen) || use_window_size) {
		rc = screen_get_window_property_iv(screen_window, SCREEN_PROPERTY_SIZE, size);
		if (rc) {
			printf("Cannot obtain current window size!\n");
			fflush(stdout);
			exit(EXIT_FAILURE);
		}
		width = size[0];
		height = size[1];
	} else {
		size[0] = width;
		size[1] = height;
		rc = screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_SIZE, size);
		if (rc) {
			printf("Cannot set window size!\n");
			fflush(stdout);
			exit(EXIT_FAILURE);
		}
	}

	if (screen_pos_set) {
		rc = screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_POSITION, screen_pos);
		if (rc) {
			printf("Cannot set window position!\n");
			fflush(stdout);
			exit(EXIT_FAILURE);
		}
	}

	if (screen_pipeline_set) {
		rc = screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_PIPELINE, &screen_pipeline);
		if (rc) {
			printf("Cannot set pipeline id!\n");
			fflush(stdout);
			exit(EXIT_FAILURE);
		}
	}

	if (screen_zorder_set) {
		rc = screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_ZORDER, &screen_zorder);
		if (rc) {
			printf("Cannot set z-order of the window!\n");
			fflush(stdout);
			exit(EXIT_FAILURE);
		}
	}
}
#else
void VulkanRTBase::setupWindow()
{
}
#endif

void VulkanRTBase::keyPressed(uint32_t) {}

void VulkanRTBase::mouseMoved(double x, double y, bool & handled) {}

void VulkanRTBase::buildCommandBuffers() {}

void VulkanRTBase::createSynchronizationPrimitives()
{
	// Wait fences to sync command buffer access
	VkFenceCreateInfo fenceCreateInfo = vks::initializers::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
	waitFences.resize(swapChain.imageCount);
	for (auto& fence : waitFences) {
		VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence));
	}
}

void VulkanRTBase::createCommandPool()
{
	VkCommandPoolCreateInfo cmdPoolInfo = {};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.queueFamilyIndex = swapChain.queueNodeIndex;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &cmdPool));
}

void VulkanRTBase::setupDepthStencil()
{
	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = depthFormat;
	imageCI.extent = { width, height, 1 };
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 1;
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &depthStencil.image));
	VkMemoryRequirements memReqs{};
	vkGetImageMemoryRequirements(device, depthStencil.image, &memReqs);

	VkMemoryAllocateInfo memAllloc{};
	memAllloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memAllloc.allocationSize = memReqs.size;
	memAllloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAllloc, nullptr, &depthStencil.memory));
	VK_CHECK_RESULT(vkBindImageMemory(device, depthStencil.image, depthStencil.memory, 0));

	VkImageViewCreateInfo imageViewCI{};
	imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCI.image = depthStencil.image;
	imageViewCI.format = depthFormat;
	imageViewCI.subresourceRange.baseMipLevel = 0;
	imageViewCI.subresourceRange.levelCount = 1;
	imageViewCI.subresourceRange.baseArrayLayer = 0;
	imageViewCI.subresourceRange.layerCount = 1;
	imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	// Stencil aspect should only be set on depth + stencil formats (VK_FORMAT_D16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8_UINT
	if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT) {
		imageViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}
	VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &depthStencil.view));
}

void VulkanRTBase::setupFrameBuffer()
{
	// Create frame buffers for every swap chain image
	frameBuffers.resize(swapChain.imageCount);
	for (uint32_t i = 0; i < frameBuffers.size(); i++)
	{
		VkImageView attachments[2];

		// Depth/Stencil attachment is the same for all frame buffers
		attachments[1] = depthStencil.view;

		VkFramebufferCreateInfo frameBufferCreateInfo = {};
		frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCreateInfo.pNext = NULL;
		frameBufferCreateInfo.renderPass = renderPass;
		frameBufferCreateInfo.attachmentCount = 2;
		frameBufferCreateInfo.pAttachments = attachments;
		frameBufferCreateInfo.width = width;
		frameBufferCreateInfo.height = height;
		frameBufferCreateInfo.layers = 1;

		attachments[0] = swapChain.buffers[i].view;
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
	}
}

void VulkanRTBase::setupRenderPass()
{
	std::array<VkAttachmentDescription, 2> attachments = {};
	// Color attachment
	attachments[0].format = swapChain.colorFormat;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	// Depth attachment
	attachments[1].format = depthFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorReference = {};
	colorReference.attachment = 0;
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthReference = {};
	depthReference.attachment = 1;
	depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpassDescription = {};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = &depthReference;
	subpassDescription.inputAttachmentCount = 0;
	subpassDescription.pInputAttachments = nullptr;
	subpassDescription.preserveAttachmentCount = 0;
	subpassDescription.pPreserveAttachments = nullptr;
	subpassDescription.pResolveAttachments = nullptr;

	// Subpass dependencies for layout transitions
	std::array<VkSubpassDependency, 2> dependencies;

	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	dependencies[0].dependencyFlags = 0;

	dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].dstSubpass = 0;
	dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].srcAccessMask = 0;
	dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	dependencies[1].dependencyFlags = 0;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}

void VulkanRTBase::getEnabledFeatures() {}

void VulkanRTBase::getEnabledExtensions() {}

void VulkanRTBase::windowResize()
{
	if (!prepared)
	{
		return;
	}
	prepared = false;
	resized = true;

	// Ensure all operations on the device have been finished before destroying resources
	vkDeviceWaitIdle(device);

	// Recreate swap chain
	width = destWidth;
	height = destHeight;
	setupSwapChain();

	// Recreate the frame buffers
	vkDestroyImageView(device, depthStencil.view, nullptr);
	vkDestroyImage(device, depthStencil.image, nullptr);
	vkFreeMemory(device, depthStencil.memory, nullptr);
	setupDepthStencil();
	for (uint32_t i = 0; i < frameBuffers.size(); i++) {
		vkDestroyFramebuffer(device, frameBuffers[i], nullptr);
	}
	setupFrameBuffer();

	if ((width > 0.0f) && (height > 0.0f)) {
		if (settings.overlay) {
			UIOverlay.resize(width, height);
		}
	}

	// Command buffers need to be recreated as they may store
	// references to the recreated frame buffer
	destroyCommandBuffers();
	createCommandBuffers();
	buildCommandBuffers();

	// SRS - Recreate fences in case number of swapchain images has changed on resize
	for (auto& fence : waitFences) {
		vkDestroyFence(device, fence, nullptr);
	}
	createSynchronizationPrimitives();

	vkDeviceWaitIdle(device);

	if ((width > 0.0f) && (height > 0.0f)) {
		camera.updateAspectRatio((float)width / (float)height);
	}

	// Notify derived class
	windowResized();

	prepared = true;
}

void VulkanRTBase::handleMouseMove(int32_t x, int32_t y)
{
	int32_t dx = (int32_t)mouseState.position.x - x;
	int32_t dy = (int32_t)mouseState.position.y - y;

	dx *= -1;
	//dy *= -1;

	bool handled = false;

	if (settings.overlay) {
		ImGuiIO& io = ImGui::GetIO();
		handled = io.WantCaptureMouse && UIOverlay.visible;
	}
	mouseMoved((float)x, (float)y, handled);

	if (handled) {
		mouseState.position = glm::vec2((float)x, (float)y);
		return;
	}

	if (mouseState.buttons.left) {
		camera.rotate(glm::vec3(dy * camera.rotationSpeed * 0.1f, -dx * camera.rotationSpeed*0.1f, 0.0f));
		viewUpdated = true;
	}
	if (mouseState.buttons.right) {
		if (camera.type == Camera::SG_camera) {

			camera.rotate(glm::vec3(0.0f, 0.0f, (float)dx * camera.rotationSpeed * .05f));
		}
		else camera.translate(glm::vec3(-0.0f, 0.0f, dy * .005f));

		viewUpdated = true;
	}
	if (mouseState.buttons.middle) {
		if (camera.type == Camera::SG_camera) {
			glm::vec3 uVec = glm::vec3(1.0f, 0.0f, 0.0f);
			glm::vec3 vVec = glm::vec3(0.0f, 1.0f, 0.0f);
			glm::vec3 nVec = glm::vec3(0.0f, 0.0f, 1.0f);

			if (glm::radians(camera.rotation.y) != 0)
				camera.rotateAxis(glm::radians(camera.rotation.y), &vVec, &nVec, &uVec);
			if (glm::radians(camera.rotation.x) != 0)
				camera.rotateAxis(glm::radians(camera.rotation.x), &uVec, &vVec, &nVec);
			if (glm::radians(camera.rotation.z) != 0)
				camera.rotateAxis(glm::radians(camera.rotation.z), &nVec, &uVec, &vVec);

			float x = dx * camera.rotationSpeed * .5f;
			camera.position += glm::vec3(uVec.x * x, uVec.y * x, uVec.z * x);
			float y = dy * camera.rotationSpeed * .5f;
			camera.position += glm::vec3(vVec.x * y, vVec.y * y, vVec.z * y);

		}
		else camera.translate(glm::vec3(-dx * 0.005f, -dy * 0.005f, 0.0f));

		viewUpdated = true;
	}
	mouseState.position = glm::vec2((float)x, (float)y);
}

void VulkanRTBase::windowResized() {}

void VulkanRTBase::initSwapchain()
{
#if defined(_WIN32)
	swapChain.initSurface(windowInstance, window);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	swapChain.initSurface(androidApp->window);
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
	swapChain.initSurface(dfb, surface);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	swapChain.initSurface(display, surface);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	swapChain.initSurface(connection, window);
#elif defined(VK_USE_PLATFORM_HEADLESS_EXT)
	swapChain.initSurface(width, height);
#elif defined(VK_USE_PLATFORM_SCREEN_QNX)
	swapChain.initSurface(screen_context, screen_window);
#endif
}

void VulkanRTBase::setupSwapChain()
{
	swapChain.create(&width, &height, settings.vsync, settings.fullscreen);
}

void VulkanRTBase::OnUpdateUIOverlay(vks::UIOverlay *overlay) {}

#if defined(_WIN32)
void VulkanRTBase::OnHandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {};
#endif

// Loads a cubemap from a file, uploads it to the device and create all Vulkan resources required to display it
void VulkanRTBase::loadCubemap(std::string filename, VkFormat format)
{
	// Connecting the device to destroy the cube map later.
	cubeMap.device = vulkanDevice;

	ktxResult result;
	ktxTexture* ktxTexture;

#if defined(__ANDROID__)
	// Textures are stored inside the apk on Android (compressed)
	// So they need to be loaded via the asset manager
	AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
	if (!asset) {
		vks::tools::exitFatal("Could not load texture from " + filename + "\n\nMake sure the assets submodule has been checked out and is up-to-date.", -1);
	}
	size_t size = AAsset_getLength(asset);
	assert(size > 0);

	ktx_uint8_t* textureData = new ktx_uint8_t[size];
	AAsset_read(asset, textureData, size);
	AAsset_close(asset);
	result = ktxTexture_CreateFromMemory(textureData, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
	delete[] textureData;
#else
	if (!vks::tools::fileExists(filename)) {
		vks::tools::exitFatal("Could not load texture from " + filename + "\n\nMake sure the assets submodule has been checked out and is up-to-date.", -1);
	}
	result = ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
#endif
	assert(result == KTX_SUCCESS);

	// Get properties required for using and upload texture data from the ktx texture object
	cubeMap.width = ktxTexture->baseWidth;
	cubeMap.height = ktxTexture->baseHeight;
	cubeMap.mipLevels = ktxTexture->numLevels;
	ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
	ktx_size_t ktxTextureSize = ktxTexture_GetDataSize(ktxTexture);

	VkMemoryAllocateInfo memAllocInfo = vks::initializers::memoryAllocateInfo();
	VkMemoryRequirements memReqs;

	// Create a host-visible staging buffer that contains the raw image data
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;

	VkBufferCreateInfo bufferCreateInfo = vks::initializers::bufferCreateInfo();
	bufferCreateInfo.size = ktxTextureSize;
	// This buffer is used as a transfer source for the buffer copy
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VK_CHECK_RESULT(vkCreateBuffer(device, &bufferCreateInfo, nullptr, &stagingBuffer));

	// Get memory requirements for the staging buffer (alignment, memory type bits)
	vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);
	memAllocInfo.allocationSize = memReqs.size;
	// Get memory type index for a host visible buffer
	memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &stagingMemory));
	VK_CHECK_RESULT(vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0));

	// Copy texture data into staging buffer
	uint8_t* data;
	VK_CHECK_RESULT(vkMapMemory(device, stagingMemory, 0, memReqs.size, 0, (void**)&data));
	memcpy(data, ktxTextureData, ktxTextureSize);
	vkUnmapMemory(device, stagingMemory);

	// Create optimal tiled target image
	VkImageCreateInfo imageCreateInfo = vks::initializers::imageCreateInfo();
	imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
	imageCreateInfo.format = format;
	imageCreateInfo.mipLevels = cubeMap.mipLevels;
	imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageCreateInfo.extent = { cubeMap.width, cubeMap.height, 1 };
	imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	// Cube faces count as array layers in Vulkan
	imageCreateInfo.arrayLayers = 6;
	// This flag is required for cube map images
	imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &cubeMap.image));

	vkGetImageMemoryRequirements(device, cubeMap.image, &memReqs);

	memAllocInfo.allocationSize = memReqs.size;
	memAllocInfo.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VK_CHECK_RESULT(vkAllocateMemory(device, &memAllocInfo, nullptr, &cubeMap.deviceMemory));
	VK_CHECK_RESULT(vkBindImageMemory(device, cubeMap.image, cubeMap.deviceMemory, 0));

	VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

	// Setup buffer copy regions for each face including all of its miplevels
	std::vector<VkBufferImageCopy> bufferCopyRegions;
	uint32_t offset = 0;

	for (uint32_t face = 0; face < 6; face++)
	{
		for (uint32_t level = 0; level < cubeMap.mipLevels; level++)
		{
			// Calculate offset into staging buffer for the current mip level and face
			ktx_size_t offset;
			KTX_error_code ret = ktxTexture_GetImageOffset(ktxTexture, level, 0, face, &offset);
			assert(ret == KTX_SUCCESS);
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = level;
			bufferCopyRegion.imageSubresource.baseArrayLayer = face;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = ktxTexture->baseWidth >> level;
			bufferCopyRegion.imageExtent.height = ktxTexture->baseHeight >> level;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = offset;
			bufferCopyRegions.push_back(bufferCopyRegion);
		}
	}

	// Image barrier for optimal image (target)
	// Set initial layout for all array layers (faces) of the optimal (target) tiled texture
	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = cubeMap.mipLevels;
	subresourceRange.layerCount = 6;

	vks::tools::setImageLayout(
		copyCmd,
		cubeMap.image,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		subresourceRange);

	// Copy the cube map faces from the staging buffer to the optimal tiled image
	vkCmdCopyBufferToImage(
		copyCmd,
		stagingBuffer,
		cubeMap.image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		static_cast<uint32_t>(bufferCopyRegions.size()),
		bufferCopyRegions.data()
	);

	// Change texture image layout to shader read after all faces have been copied
	cubeMap.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vks::tools::setImageLayout(
		copyCmd,
		cubeMap.image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		cubeMap.imageLayout,
		subresourceRange);

	vulkanDevice->flushCommandBuffer(copyCmd, graphicsQueue, true);

	// Create sampler
	VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
	sampler.magFilter = VK_FILTER_LINEAR;
	sampler.minFilter = VK_FILTER_LINEAR;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler.addressModeV = sampler.addressModeU;
	sampler.addressModeW = sampler.addressModeU;
	sampler.mipLodBias = 0.0f;
	sampler.compareOp = VK_COMPARE_OP_NEVER;
	sampler.minLod = 0.0f;
	sampler.maxLod = static_cast<float>(cubeMap.mipLevels);
	sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	sampler.maxAnisotropy = 1.0f;
	if (vulkanDevice->features.samplerAnisotropy)
	{
		sampler.maxAnisotropy = vulkanDevice->properties.limits.maxSamplerAnisotropy;
		sampler.anisotropyEnable = VK_TRUE;
	}
	VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &cubeMap.sampler));

	// Create image view
	VkImageViewCreateInfo view = vks::initializers::imageViewCreateInfo();
	// Cube map view type
	view.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	view.format = format;
	view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	// 6 array layers (faces)
	view.subresourceRange.layerCount = 6;
	// Set number of mip levels
	view.subresourceRange.levelCount = cubeMap.mipLevels;
	view.image = cubeMap.image;
	VK_CHECK_RESULT(vkCreateImageView(device, &view, nullptr, &cubeMap.view));

	// Clean up staging resources
	vkFreeMemory(device, stagingMemory, nullptr);
	vkDestroyBuffer(device, stagingBuffer, nullptr);
	ktxTexture_Destroy(ktxTexture);
}

void VulkanRTBase::initCamera()
{
	camera.type = Camera::CameraType::SG_camera;
	camera.movementSpeed = 5.0f;
#ifndef __ANDROID__
	camera.rotationSpeed = 0.25f;
#endif
	camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 5000.0f);

	setCamera(0);
}

void VulkanRTBase::setCamera(uint32_t camIdx)
{
#if ASSET == 0
	switch (camIdx) {
	case 0:
		//camera.setTranslation(glm::vec3(1.509223, 9.325905, 8.422721));
		//camera.setRotation(glm::vec3(-12.999950, 15.799769, 2.337500));
		camera.setTranslation(glm::vec3(0.0f, -10.0f, 0.0f));
		camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
		// camera.setTranslation(glm::vec3(0.000000, 0.000000, 4.400000));
		// camera.setRotation(glm::vec3(0.000000, 0.000000, 0.000000));
		break;
	case 1:
		camera.setTranslation(glm::vec3(-2.964525, -1.904862, 3.256133));
		camera.setRotation(glm::vec3(24.400002, -36.774998, -0.187500));
		break;
	case 2:
		camera.setTranslation(glm::vec3(-3.914991, 0.601804, -0.239344));
		camera.setRotation(glm::vec3(-3.199999, -88.599998, 0.000000));
		break;
	}
#elif ASSET == 1
	switch (camIdx) {
	case 0:
		camera.setTranslation(glm::vec3(1.146842, 2.282518, 1.067378));
		camera.setRotation(glm::vec3(-22.524939, 58.374725, 0.000000));
		break;
	case 1:
		camera.setTranslation(glm::vec3(-6.497121, 1.637290, -1.421643));
		camera.setRotation(glm::vec3(10.925017, -102.249245, 0.000000));
		break;
	case 2:
		camera.setTranslation(glm::vec3(4.291043, 4.683933, -1.352913));
		camera.setRotation(glm::vec3(-20.874960, 106.026215, 0.000000));
		break;
	}
#elif ASSET == 2
	switch (camIdx) {
	case 0:
		camera.setTranslation(glm::vec3(-2.039184, -2.108208, 13.222129));
		camera.setRotation(glm::vec3(9.474999, 346.226501, 0.000000));
		break;
	case 1:
		camera.setTranslation(glm::vec3(3.786976, -1.408663, -4.825589));
		camera.setRotation(glm::vec3(2.899944, 504.574402, 0.000000));
		break;
	case 2:
		camera.setTranslation(glm::vec3(-4.241950, 2.714610, -3.181164));
		camera.setRotation(glm::vec3(-41.275059, 595.162048, 0.000000));
		break;
	}
#elif ASSET == 3
	switch (camIdx) {
	case 0:
		camera.setTranslation(glm::vec3(1.151980, 0.971871, 1.761554));
		camera.setRotation(glm::vec3(-14.949973, 393.002319, 0.000000));
		break;
	case 1:
		camera.setTranslation(glm::vec3(-1.495003, 1.402008, 1.792632));
		camera.setRotation(glm::vec3(-24.149887, 324.800903, 0.000000));
		break;
	case 2:
		camera.setTranslation(glm::vec3(0.003964, 1.457728, -2.351941));
		camera.setRotation(glm::vec3(-19.874851, 180.675659, 0.000000));
		break;
	}
#elif ASSET == 4
	switch (camIdx) {
	case 0:
		camera.setTranslation(glm::vec3(-21.893288, 6.842527, -2.459376));
		camera.setRotation(glm::vec3(-4.600013, -98.299873, 0.000000));
		break;
	case 1:
		camera.setTranslation(glm::vec3(45.261234, 5.415184, 30.707674));
		camera.setRotation(glm::vec3(3.950051, -110.674309, 0.000000));
		break;
	case 2:
		camera.setTranslation(glm::vec3(25.427555, 5.962574, -58.274254));
		camera.setRotation(glm::vec3(-0.999935, 144.175308, 0.000000));
		break;
	}
#endif
}
