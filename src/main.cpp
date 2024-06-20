#include <stdio.h>

#include <memory>
#include <iostream>
#include <fstream>
#include <string>

#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>

#include "VkBootstrap.h"

// #include "vk_mem_alloc.h"

#define EXAMPLE_BUILD_DIRECTORY "./shaders"

const int MAX_FRAMES_IN_FLIGHT = 3;

struct Init {
	GLFWwindow* window;
	vkb::Instance instance;
	vkb::InstanceDispatchTable inst_disp;
	VkSurfaceKHR surface;
	vkb::PhysicalDevice physical_device;
	vkb::Device device;
	vkb::DispatchTable disp;
	vkb::Swapchain swapchain;

	// VmaAllocator allocator;
};

struct RenderData {
	VkQueue graphics_queue;
	VkQueue present_queue;

	std::vector<VkImage> swapchain_images;
	std::vector<VkImageView> swapchain_image_views;
	std::vector<VkFramebuffer> framebuffers;

	VkRenderPass render_pass;
	VkPipelineLayout pipeline_layout;
	VkPipeline graphics_pipeline;

	VkCommandPool command_pool;
	std::vector<VkCommandBuffer> command_buffers;

	std::vector<VkSemaphore> available_semaphores;
	std::vector<VkSemaphore> finished_semaphore;
	std::vector<VkFence> in_flight_fences;
	std::vector<VkFence> image_in_flight;

	std::vector<VkBuffer> buffers;
	std::vector<VkDeviceMemory> buffersMemory;
	std::vector<void*> buffersMapped;

	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout setLayout {};

	std::vector<VkDescriptorSet> descriptorSets;

	size_t current_frame = 0;
};

// left/top/right/bottom borders
double edgeData[4] = {-2.0f, -2.0f, 2.0f, 2.0f};

GLFWwindow* create_window_glfw(const char* window_name = "", bool resize = true) {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	if (!resize) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	return glfwCreateWindow(1024, 1024, window_name, NULL, NULL);
}

void destroy_window_glfw(GLFWwindow* window) {
	glfwDestroyWindow(window);
	glfwTerminate();
}

VkSurfaceKHR create_surface_glfw(VkInstance instance, GLFWwindow* window, VkAllocationCallbacks* allocator = nullptr) {
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkResult err = glfwCreateWindowSurface(instance, window, allocator, &surface);
	if (err) {
		const char* error_msg;
		int ret = glfwGetError(&error_msg);
		if (ret != 0) {
			std::cout << ret << " ";
			if (error_msg != nullptr) std::cout << error_msg;
			std::cout << "\n";
		}
		surface = VK_NULL_HANDLE;
	}
	return surface;
}

int device_initialization(Init& init) {
	init.window = create_window_glfw("Vulkan Mandel", true);

	vkb::InstanceBuilder instance_builder;
	auto instance_ret = instance_builder.use_default_debug_messenger().request_validation_layers().build();
	if (!instance_ret) {
		std::cout << instance_ret.error().message() << "\n";
		return -1;
	}
	init.instance = instance_ret.value();

	init.inst_disp = init.instance.make_table();

	init.surface = create_surface_glfw(init.instance, init.window);

	vkb::PhysicalDeviceSelector phys_device_selector(init.instance);
	{
		VkPhysicalDeviceVulkan12Features features12 {
			.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
		};
		features12.bufferDeviceAddress = true;
		phys_device_selector.set_required_features_12(features12);

		VkPhysicalDeviceFeatures features {};
		features.shaderFloat64 = VK_TRUE;
		phys_device_selector.set_required_features(features);
	}
	auto phys_device_ret = phys_device_selector.set_surface(init.surface).select();
	if (!phys_device_ret) {
		std::cout << phys_device_ret.error().message() << "\n";
		return -1;
	}
	init.physical_device = phys_device_ret.value();

	vkb::DeviceBuilder device_builder{ init.physical_device };
	auto device_ret = device_builder.build();
	if (!device_ret) {
		std::cout << device_ret.error().message() << "\n";
		return -1;
	}
	init.device = device_ret.value();

	init.disp = init.device.make_table();

	return 0;
}

int create_swapchain(Init& init) {

	vkb::SwapchainBuilder swapchain_builder{ init.device };
	// swapchain_builder.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR);
	auto swap_ret = swapchain_builder.set_old_swapchain(init.swapchain).build();
	if (!swap_ret) {
		std::cout << swap_ret.error().message() << " " << swap_ret.vk_result() << "\n";
		return -1;
	}
	vkb::destroy_swapchain(init.swapchain);
	init.swapchain = swap_ret.value();
	return 0;
}

int get_queues(Init& init, RenderData& data) {
	auto gq = init.device.get_queue(vkb::QueueType::graphics);
	if (!gq.has_value()) {
		std::cout << "failed to get graphics queue: " << gq.error().message() << "\n";
		return -1;
	}
	data.graphics_queue = gq.value();

	auto pq = init.device.get_queue(vkb::QueueType::present);
	if (!pq.has_value()) {
		std::cout << "failed to get present queue: " << pq.error().message() << "\n";
		return -1;
	}
	data.present_queue = pq.value();
	return 0;
}

int create_render_pass(Init& init, RenderData& data) {
	VkAttachmentDescription color_attachment = {};
	color_attachment.format = init.swapchain.image_format;
	color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
	color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference color_attachment_ref = {};
	color_attachment_ref.attachment = 0;
	color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_attachment_ref;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo render_pass_info = {};
	render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	render_pass_info.attachmentCount = 1;
	render_pass_info.pAttachments = &color_attachment;
	render_pass_info.subpassCount = 1;
	render_pass_info.pSubpasses = &subpass;
	render_pass_info.dependencyCount = 1;
	render_pass_info.pDependencies = &dependency;

	if (init.disp.createRenderPass(&render_pass_info, nullptr, &data.render_pass) != VK_SUCCESS) {
		std::cout << "failed to create render pass\n";
		return -1; // failed to create render pass!
	}
	return 0;
}

std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}

	size_t file_size = (size_t)file.tellg();
	std::vector<char> buffer(file_size);

	file.seekg(0);
	file.read(buffer.data(), static_cast<std::streamsize>(file_size));

	file.close();

	return buffer;
}

VkShaderModule createShaderModule(Init& init, const std::vector<char>& code) {
	VkShaderModuleCreateInfo create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create_info.codeSize = code.size();
	create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule shaderModule;
	if (init.disp.createShaderModule(&create_info, nullptr, &shaderModule) != VK_SUCCESS) {
		return VK_NULL_HANDLE; // failed to create shader module
	}

	return shaderModule;
}

int create_graphics_pipeline(Init& init, RenderData& data) {
	auto vert_code = readFile(std::string(EXAMPLE_BUILD_DIRECTORY) + "/shader.vert.spv");
	auto frag_code = readFile(std::string(EXAMPLE_BUILD_DIRECTORY) + "/shader.frag.spv");

	VkShaderModule vert_module = createShaderModule(init, vert_code);
	VkShaderModule frag_module = createShaderModule(init, frag_code);
	if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
		std::cout << "failed to create shader module\n";
		return -1; // failed to create shader modules
	}

	VkPipelineShaderStageCreateInfo vert_stage_info = {};
	vert_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vert_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vert_stage_info.module = vert_module;
	vert_stage_info.pName = "main";

	VkPipelineShaderStageCreateInfo frag_stage_info = {};
	frag_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	frag_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	frag_stage_info.module = frag_module;
	frag_stage_info.pName = "main";

	VkPipelineShaderStageCreateInfo shader_stages[] = { vert_stage_info, frag_stage_info };

	VkPipelineVertexInputStateCreateInfo vertex_input_info = {};
	vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_info.vertexBindingDescriptionCount = 0;
	vertex_input_info.vertexAttributeDescriptionCount = 0;

	VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
	input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	input_assembly.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)init.swapchain.extent.width;
	viewport.height = (float)init.swapchain.extent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = init.swapchain.extent;

	VkPipelineViewportStateCreateInfo viewport_state = {};
	viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state.viewportCount = 1;
	viewport_state.pViewports = &viewport;
	viewport_state.scissorCount = 1;
	viewport_state.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask =
	VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo color_blending = {};
	color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	color_blending.logicOpEnable = VK_FALSE;
	color_blending.logicOp = VK_LOGIC_OP_COPY;
	color_blending.attachmentCount = 1;
	color_blending.pAttachments = &colorBlendAttachment;
	color_blending.blendConstants[0] = 0.0f;
	color_blending.blendConstants[1] = 0.0f;
	color_blending.blendConstants[2] = 0.0f;
	color_blending.blendConstants[3] = 0.0f;

	VkDescriptorSetLayoutBinding binding {};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding
	};
	if (vkCreateDescriptorSetLayout(init.device, &setLayoutCreateInfo, nullptr, &data.setLayout) != VK_SUCCESS) {
		std::cout << "vkCreateDescriptorSetLayout failed" << std::endl;
		throw;
	}

	VkDescriptorPoolSize poolSize {};
	poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSize.descriptorCount = MAX_FRAMES_IN_FLIGHT;

	VkDescriptorPoolCreateInfo poolInfo {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = MAX_FRAMES_IN_FLIGHT,
		.poolSizeCount = 1,
		.pPoolSizes = &poolSize,
	};

	if (vkCreateDescriptorPool(init.device, &poolInfo, nullptr, &data.descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Descriptor pool creation failed!");

	std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, data.setLayout);
	VkDescriptorSetAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = data.descriptorPool,
		.descriptorSetCount = MAX_FRAMES_IN_FLIGHT,
		.pSetLayouts = layouts.data(),
	};

	data.descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
	if (vkAllocateDescriptorSets(init.device, &allocInfo, data.descriptorSets.data()) != VK_SUCCESS)
		throw std::runtime_error("Descriptor Set creation failed!");

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VkDescriptorBufferInfo bufferInfo = {
			.buffer = data.buffers[i],
			.offset = 0,
			.range = VK_WHOLE_SIZE,
		};

		VkWriteDescriptorSet descriptorWrite = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = data.descriptorSets[i],
			.dstBinding = 0,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &bufferInfo,
		};

		vkUpdateDescriptorSets(init.device, 1, &descriptorWrite, 0, nullptr);
	}

	VkPipelineLayoutCreateInfo pipeline_layout_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &data.setLayout
	};
	if (init.disp.createPipelineLayout(&pipeline_layout_info, nullptr, &data.pipeline_layout) != VK_SUCCESS)
		throw std::runtime_error("failed to create pipeline layout\n");

	std::vector<VkDynamicState> dynamic_states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamic_info = {};
	dynamic_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_info.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
	dynamic_info.pDynamicStates = dynamic_states.data();

	VkGraphicsPipelineCreateInfo pipeline_info = {};
	pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipeline_info.stageCount = 2;
	pipeline_info.pStages = shader_stages;
	pipeline_info.pVertexInputState = &vertex_input_info;
	pipeline_info.pInputAssemblyState = &input_assembly;
	pipeline_info.pViewportState = &viewport_state;
	pipeline_info.pRasterizationState = &rasterizer;
	pipeline_info.pMultisampleState = &multisampling;
	pipeline_info.pColorBlendState = &color_blending;
	pipeline_info.pDynamicState = &dynamic_info;
	pipeline_info.layout = data.pipeline_layout;
	pipeline_info.renderPass = data.render_pass;
	pipeline_info.subpass = 0;
	pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

	if (init.disp.createGraphicsPipelines(VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &data.graphics_pipeline) != VK_SUCCESS) {
		std::cout << "failed to create pipline\n";
		return -1; // failed to create graphics pipeline
	}

	init.disp.destroyShaderModule(frag_module, nullptr);
	init.disp.destroyShaderModule(vert_module, nullptr);
	return 0;
}

int create_transfer_buffers(Init& init, RenderData& data) {
	data.buffers.resize(MAX_FRAMES_IN_FLIGHT);
	data.buffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
	data.buffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

	uint32_t memoryIndex = 0xFFFFFFFF;

	std::cout << "Creating transfer buffers…" << std::endl;

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size  = sizeof(edgeData);
		bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkResult ret = vkCreateBuffer(init.device, &bufferInfo, nullptr, &data.buffers[i]);
		if (ret != VK_SUCCESS)
			throw std::runtime_error("Buffer creation failed!");

		VkMemoryRequirements memreq;
		vkGetBufferMemoryRequirements(init.device, data.buffers[i], &memreq);

		if (memoryIndex == 0xFFFFFFFF) {
			VkPhysicalDeviceMemoryProperties memProperties;
			vkGetPhysicalDeviceMemoryProperties(init.physical_device, &memProperties);

			for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
				if ((memreq.memoryTypeBits & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
					memoryIndex = i;
					break;
				}
			}
		}
		if (memoryIndex == 0xFFFFFFFF)
			throw std::runtime_error("No suitable memory type found!");

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memreq.size;
		allocInfo.memoryTypeIndex = memoryIndex;

		if (vkAllocateMemory(init.device, &allocInfo, nullptr, &data.buffersMemory[i]) != VK_SUCCESS)
			throw std::runtime_error("Failed to allocate memory for buffer!");

		vkBindBufferMemory(init.device, data.buffers[i], data.buffersMemory[i], 0);

		vkMapMemory(init.device, data.buffersMemory[i], 0, sizeof(edgeData), 0, &data.buffersMapped[i]);
	}

	return 0;
}

int create_framebuffers(Init& init, RenderData& data) {
	data.swapchain_images = init.swapchain.get_images().value();
	data.swapchain_image_views = init.swapchain.get_image_views().value();

	data.framebuffers.resize(data.swapchain_image_views.size());

	for (size_t i = 0; i < data.swapchain_image_views.size(); i++) {
		VkImageView attachments[] = { data.swapchain_image_views[i] };

		VkFramebufferCreateInfo framebuffer_info = {};
		framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebuffer_info.renderPass = data.render_pass;
		framebuffer_info.attachmentCount = 1;
		framebuffer_info.pAttachments = attachments;
		framebuffer_info.width = init.swapchain.extent.width;
		framebuffer_info.height = init.swapchain.extent.height;
		framebuffer_info.layers = 1;

		if (init.disp.createFramebuffer(&framebuffer_info, nullptr, &data.framebuffers[i]) != VK_SUCCESS) {
			return -1; // failed to create framebuffer
		}
	}
	return 0;
}

int create_command_pool(Init& init, RenderData& data) {
	VkCommandPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.queueFamilyIndex = init.device.get_queue_index(vkb::QueueType::graphics).value();

	if (init.disp.createCommandPool(&pool_info, nullptr, &data.command_pool) != VK_SUCCESS) {
		std::cout << "failed to create command pool\n";
		return -1; // failed to create command pool
	}
	return 0;
}

int create_command_buffers(Init& init, RenderData& data) {
	data.command_buffers.resize(data.framebuffers.size());

	VkCommandBufferAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = data.command_pool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = (uint32_t)data.command_buffers.size();

	if (init.disp.allocateCommandBuffers(&allocInfo, data.command_buffers.data()) != VK_SUCCESS) {
		return -1; // failed to allocate command buffers;
	}

	for (size_t i = 0; i < data.command_buffers.size(); i++) {
		VkCommandBufferBeginInfo begin_info = {};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		if (init.disp.beginCommandBuffer(data.command_buffers[i], &begin_info) != VK_SUCCESS) {
			return -1; // failed to begin recording command buffer
		}

		VkRenderPassBeginInfo render_pass_info = {};
		render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		render_pass_info.renderPass = data.render_pass;
		render_pass_info.framebuffer = data.framebuffers[i];
		render_pass_info.renderArea.offset = { 0, 0 };
		render_pass_info.renderArea.extent = init.swapchain.extent;
		VkClearValue clearColor{ { { 0.0f, 0.0f, 0.0f, 1.0f } } };
		render_pass_info.clearValueCount = 1;
		render_pass_info.pClearValues = &clearColor;

		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = (float)init.swapchain.extent.width;
		viewport.height = (float)init.swapchain.extent.height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = init.swapchain.extent;

		// {
		// 	init.disp.cmdPushConstants(data.command_buffers[i], data.pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(edgeData), edgeData);
		// }

		init.disp.cmdSetViewport(data.command_buffers[i], 0, 1, &viewport);
		init.disp.cmdSetScissor(data.command_buffers[i], 0, 1, &scissor);

		init.disp.cmdBeginRenderPass(data.command_buffers[i], &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);

		init.disp.cmdBindPipeline(data.command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, data.graphics_pipeline);

		init.disp.cmdBindDescriptorSets(data.command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, data.pipeline_layout, 0, 1, &data.descriptorSets[i], 0, nullptr);

		init.disp.cmdDraw(data.command_buffers[i], 6, 1, 0, 0);

		init.disp.cmdEndRenderPass(data.command_buffers[i]);

		if (init.disp.endCommandBuffer(data.command_buffers[i]) != VK_SUCCESS) {
			std::cout << "failed to record command buffer\n";
			return -1; // failed to record command buffer!
		}
	}
	return 0;
}

int create_sync_objects(Init& init, RenderData& data) {
	data.available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
	data.finished_semaphore.resize(MAX_FRAMES_IN_FLIGHT);
	data.in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);
	data.image_in_flight.resize(init.swapchain.image_count, VK_NULL_HANDLE);

	VkSemaphoreCreateInfo semaphore_info = {};
	semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fence_info = {};
	fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		if (init.disp.createSemaphore(&semaphore_info, nullptr, &data.available_semaphores[i]) != VK_SUCCESS ||
			init.disp.createSemaphore(&semaphore_info, nullptr, &data.finished_semaphore[i]) != VK_SUCCESS ||
			init.disp.createFence(&fence_info, nullptr, &data.in_flight_fences[i]) != VK_SUCCESS) {
			std::cout << "failed to create sync objects\n";
		return -1; // failed to create synchronization objects for a frame
			}
	}
	return 0;
}

int recreate_swapchain(Init& init, RenderData& data) {
	init.disp.deviceWaitIdle();

	init.disp.destroyCommandPool(data.command_pool, nullptr);

	for (auto framebuffer : data.framebuffers) {
		init.disp.destroyFramebuffer(framebuffer, nullptr);
	}

	init.swapchain.destroy_image_views(data.swapchain_image_views);

	if (0 != create_swapchain(init)) return -1;
	if (0 != create_framebuffers(init, data)) return -1;
	if (0 != create_command_pool(init, data)) return -1;
	if (0 != create_command_buffers(init, data)) return -1;
	return 0;
}

int draw_frame(Init& init, RenderData& data) {
	init.disp.waitForFences(1, &data.in_flight_fences[data.current_frame], VK_TRUE, UINT64_MAX);

	uint32_t image_index = 0;
	VkResult result = init.disp.acquireNextImageKHR(
		init.swapchain, UINT64_MAX, data.available_semaphores[data.current_frame], VK_NULL_HANDLE, &image_index);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		return recreate_swapchain(init, data);
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		std::cout << "failed to acquire swapchain image. Error " << result << "\n";
		return -1;
	}

	if (data.image_in_flight[image_index] != VK_NULL_HANDLE) {
		init.disp.waitForFences(1, &data.image_in_flight[image_index], VK_TRUE, UINT64_MAX);
	}
	data.image_in_flight[image_index] = data.in_flight_fences[data.current_frame];

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore wait_semaphores[] = { data.available_semaphores[data.current_frame] };
	VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = wait_semaphores;
	submitInfo.pWaitDstStageMask = wait_stages;

	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &data.command_buffers[image_index];

	VkSemaphore signal_semaphores[] = { data.finished_semaphore[data.current_frame] };
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signal_semaphores;

	init.disp.resetFences(1, &data.in_flight_fences[data.current_frame]);

	memcpy(data.buffersMapped[data.current_frame], &edgeData, sizeof(edgeData));

	if (init.disp.queueSubmit(data.graphics_queue, 1, &submitInfo, data.in_flight_fences[data.current_frame]) != VK_SUCCESS) {
		std::cout << "failed to submit draw command buffer\n";
		return -1; //"failed to submit draw command buffer
	}

	VkPresentInfoKHR present_info = {};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

	present_info.waitSemaphoreCount = 1;
	present_info.pWaitSemaphores = signal_semaphores;

	VkSwapchainKHR swapChains[] = { init.swapchain };
	present_info.swapchainCount = 1;
	present_info.pSwapchains = swapChains;

	present_info.pImageIndices = &image_index;

	result = init.disp.queuePresentKHR(data.present_queue, &present_info);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		return recreate_swapchain(init, data);
	} else if (result != VK_SUCCESS) {
		std::cout << "failed to present swapchain image\n";
		return -1;
	}

	data.current_frame = (data.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	return 0;
}

void cleanup(Init& init, RenderData& data) {
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		init.disp.destroySemaphore(data.finished_semaphore[i], nullptr);
		init.disp.destroySemaphore(data.available_semaphores[i], nullptr);
		init.disp.destroyFence(data.in_flight_fences[i], nullptr);
	}

	init.disp.destroyCommandPool(data.command_pool, nullptr);

	for (auto framebuffer : data.framebuffers) {
		init.disp.destroyFramebuffer(framebuffer, nullptr);
	}

	init.disp.destroyPipeline(data.graphics_pipeline, nullptr);
	init.disp.destroyPipelineLayout(data.pipeline_layout, nullptr);
	init.disp.destroyRenderPass(data.render_pass, nullptr);

	init.swapchain.destroy_image_views(data.swapchain_image_views);

	for (auto b : data.buffers)
		vkDestroyBuffer(init.device, b, nullptr);
	for (auto m : data.buffersMemory)
		vkFreeMemory(init.device, m, nullptr);

	// apparently the descriptor pool will free them for us
	// vkFreeDescriptorSets(init.device, data.descriptorPool, data.descriptorSets.size(), data.descriptorSets.data());

	vkDestroyDescriptorPool(init.device, data.descriptorPool, nullptr);

	vkDestroyDescriptorSetLayout(init.device, data.setLayout, nullptr);

	vkb::destroy_swapchain(init.swapchain);
	vkb::destroy_device(init.device);
	vkb::destroy_surface(init.instance, init.surface);
	vkb::destroy_instance(init.instance);
	destroy_window_glfw(init.window);
}

Init init;
RenderData render_data;

double center[2] = {0, 0};
double zoom = 1;
double perpixel = 1.0/512;

bool mouseDrag = false;
double mousePos[2] = {};
double mousePoint[2] = {};
double mouseGrabPoint[2] = {};

static inline double dmap(double oldmin, double oldmax, double newmin, double newmax, double value) {
	return (value - oldmin) * (newmax - newmin) / (oldmax - oldmin) + newmin;
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos)
{
	mousePos[0] = xpos / init.swapchain.extent.width;
	mousePos[1] = ypos / init.swapchain.extent.height;

	mousePoint[0] = dmap(0, 1, center[0] - 2.0/zoom, center[0] + 2.0/zoom, mousePos[0]);
	mousePoint[1] = dmap(0, 1, center[1] - 2.0/zoom, center[1] + 2.0/zoom, mousePos[1]);

	std::cout << "Mouse move " << mousePoint[0] << "," << mousePoint[1] << std::endl;

	if (mouseDrag) {
		center[0] += (mouseGrabPoint[0] - mousePoint[0]);
		center[1] += (mouseGrabPoint[1] - mousePoint[1]);
		// center[0] = (mouseGrabPoint[0] - mousePos[0]) * -4.0/zoom + 2.0/zoom;
		// center[1] = (mouseGrabPoint[1] - mousePos[1]) * -4.0/zoom + 2.0/zoom;

		// double newMousePoint[2] = {
		// 	dmap(0, 1, center[0] - 2.0/zoom, center[0] + 2.0/zoom, mousePos[0]),
		// 	dmap(0, 1, center[1] - 2.0/zoom, center[1] + 2.0/zoom, mousePos[1]),
		// };


		// std::cout << "\t\t" << newMousePoint[0] << "," << newMousePoint[1] << " should equal " << mouseGrabPoint[0] << "," << mouseGrabPoint[1] << "; distance = " << (mouseGrabPoint[0] - newMousePoint[0]) << "," << (mouseGrabPoint[1] - newMousePoint[1]) << std::endl;
	}
}

void cursor_enter_callback(GLFWwindow* window, int entered)
{
	if (entered)
	{
		// The cursor entered the content area of the window
	}
	else
	{
		// The cursor left the content area of the window
	}
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT)
	{
		if (action == GLFW_PRESS) {
			mouseGrabPoint[0] = mousePoint[0];
			mouseGrabPoint[1] = mousePoint[1];
			mouseDrag = true;

			std::cout << "Mouse Down, grabs " << mouseGrabPoint[0] << "," << mouseGrabPoint[1] << std::endl;
		}
		else if (action == GLFW_RELEASE) {
			std::cout << "Mouse Up" << std::endl;
			mouseDrag = false;
		}
	}
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	std::cout << "Scroll " << xoffset << "," << yoffset << std::endl;
	if (yoffset != 0) {
		// mouseGrabPoint[0] = mousePoint[0];
		// mouseGrabPoint[1] = mousePoint[1];

		zoom *= 1 + (yoffset * 0.1);

		// mousePoint[0] = dmap(0, 1, center[0] - 2.0/zoom, center[0] + 2.0/zoom, mousePos[0]);
		// mousePoint[1] = dmap(0, 1, center[1] - 2.0/zoom, center[1] + 2.0/zoom, mousePos[1]);

		// center[0] += (mouseGrabPoint[0] - mousePoint[0]);
		// center[1] += (mouseGrabPoint[1] - mousePoint[1]);
	}

	std::cout << "Zoom is now " << zoom << std::endl;
}

int main() {
	if (0 != device_initialization(init)) return -1;
	if (0 != create_swapchain(init)) return -1;
	if (0 != get_queues(init, render_data)) return -1;
	if (0 != create_render_pass(init, render_data)) return -1;
	if (0 != create_transfer_buffers(init, render_data)) return -1;
	if (0 != create_graphics_pipeline(init, render_data)) return -1;
	if (0 != create_framebuffers(init, render_data)) return -1;
	if (0 != create_command_pool(init, render_data)) return -1;
	if (0 != create_command_buffers(init, render_data)) return -1;
	if (0 != create_sync_objects(init, render_data)) return -1;

	glfwSetCursorPosCallback(init.window, cursor_position_callback);
	glfwSetCursorEnterCallback(init.window, cursor_enter_callback);
	glfwSetMouseButtonCallback(init.window, mouse_button_callback);
	glfwSetScrollCallback(init.window, scroll_callback);

	while (!glfwWindowShouldClose(init.window)) {
		glfwPollEvents();

		{
			edgeData[0] = center[0] - perpixel * init.swapchain.extent.width / zoom;
			edgeData[1] = center[1] - perpixel * init.swapchain.extent.height / zoom;
			edgeData[2] = center[0] + perpixel * init.swapchain.extent.width / zoom;
			edgeData[3] = center[1] + perpixel * init.swapchain.extent.height / zoom;
		}
		if (0) {
			center[0] -= 0.001f;
			zoom *= 1.001f;
			edgeData[0] = center[0] - 2.0f/zoom;
			edgeData[1] = center[1] - 2.0f/zoom;
			edgeData[2] = center[0] + 2.0f/zoom;
			edgeData[3] = center[1] + 2.0f/zoom;
		}

		int res = draw_frame(init, render_data);
		if (res != 0) {
			std::cout << "failed to draw frame \n";
			return -1;
		}
	}
	init.disp.deviceWaitIdle();

	cleanup(init, render_data);
	return 0;
}
