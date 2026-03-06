#include "Application.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>

// ============================================================

const std::vector<Vertex> VERTS = {
  { { -.5f, -.5f,  .5f }, { 1, 0, 0 } }, { {  .5f, -.5f,  .5f }, { 0, 1, 0 } },
  { {  .5f,  .5f,  .5f }, { 0, 0, 1 } }, { { -.5f,  .5f,  .5f }, { 1, 1, 0 } },
  { { -.5f, -.5f, -.5f }, { 1, 0, 1 } }, { {  .5f, -.5f, -.5f }, { 0, 1, 1 } },
  { {  .5f,  .5f, -.5f }, { 1, 1, 1 } }, { { -.5f,  .5f, -.5f }, { .5f, .5f, .5f } },
};
const std::vector<uint16_t> IDXS = {
  0,1,2, 2,3,0,   // front
  5,4,7, 7,6,5,   // back
  4,0,3, 3,7,4,   // left
  1,5,6, 6,2,1,   // right
  3,2,6, 6,7,3,   // top
  4,5,1, 1,0,4,   // bottom
};

static std::vector<char> readFile (const char *path)
{
  std::ifstream f(path, std::ios::ate | std::ios::binary);
  if (!f.is_open()) { throw std::runtime_error(std::string("Failed to open: ") + path); }
  size_t size = f.tellg();
  std::vector<char> buf(size);
  f.seekg(0); f.read(buf.data(), size);
  return buf;
}

// ============================================================

void Application::initWindow ()
{
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Cube", nullptr, nullptr);
  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, [](GLFWwindow *w, int, int) {
    ((Application*)glfwGetWindowUserPointer(w))->framebufferResized = true;
  });
}

// ------ Instance ------

void Application::createInstance ()
{
  VkApplicationInfo ai { VK_STRUCTURE_TYPE_APPLICATION_INFO };
  ai.pApplicationName   = "VulkanCube";
  ai.applicationVersion = VK_MAKE_VERSION(1,0,0);
  ai.pEngineName        = "None";
  ai.apiVersion         = VK_API_VERSION_1_0;

  uint32_t glfwExtCnt; const char **glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCnt);
  VkInstanceCreateInfo ci { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
  ci.pApplicationInfo        = &ai;
  ci.enabledExtensionCount   = glfwExtCnt;
  ci.ppEnabledExtensionNames = glfwExts;
  if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateInstance failed"); }
}

// ------ Physical Device ------

QueueFamilies Application::findQueueFamilies (VkPhysicalDevice pd)
{
  uint32_t cnt; vkGetPhysicalDeviceQueueFamilyProperties(pd, &cnt, nullptr);
  std::vector<VkQueueFamilyProperties> props(cnt);
  vkGetPhysicalDeviceQueueFamilyProperties(pd, &cnt, props.data());
  std::optional<uint32_t> gfx, pres;
  for (uint32_t i = 0; i < cnt; i++)
  {
    if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { gfx = i; }
    VkBool32 sup; vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &sup);
    if (sup) { pres = i; }
    if (gfx && pres) { break; }
  }
  if (!gfx || !pres) { throw std::runtime_error("No suitable queue families"); }
  return { *gfx, *pres };
}

bool Application::isDeviceSuitable (VkPhysicalDevice pd)
{
  uint32_t cnt; vkEnumerateDeviceExtensionProperties(pd, nullptr, &cnt, nullptr);
  std::vector<VkExtensionProperties> exts(cnt);
  vkEnumerateDeviceExtensionProperties(pd, nullptr, &cnt, exts.data());
  for (auto &e : exts)
  {
    if (!strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
      { try { findQueueFamilies(pd); return true; } catch (...) { } }
  }
  return false;
}

void Application::pickPhysicalDevice ()
{
  uint32_t cnt; vkEnumeratePhysicalDevices(instance, &cnt, nullptr);
  std::vector<VkPhysicalDevice> devs(cnt);
  vkEnumeratePhysicalDevices(instance, &cnt, devs.data());
  for (auto pd : devs) { if (isDeviceSuitable(pd)) { physDev = pd; return; } }
  throw std::runtime_error("No suitable GPU found");
}

// ------ Logical Device ------

void Application::createLogicalDevice ()
{
  qf = findQueueFamilies(physDev);
  std::set<uint32_t> uniqueQ = { qf.graphics, qf.present };
  float prio = 1.0f;
  std::vector<VkDeviceQueueCreateInfo> qcis;
  for (uint32_t q : uniqueQ)
  {
    VkDeviceQueueCreateInfo ci { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    ci.queueFamilyIndex = q; ci.queueCount = 1; ci.pQueuePriorities = &prio;
    qcis.push_back(ci);
  }
  const char *ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  VkPhysicalDeviceFeatures feats { };
  VkDeviceCreateInfo ci { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
  ci.queueCreateInfoCount    = qcis.size();
  ci.pQueueCreateInfos       = qcis.data();
  ci.enabledExtensionCount   = 1;
  ci.ppEnabledExtensionNames = &ext;
  ci.pEnabledFeatures        = &feats;
  if (vkCreateDevice(physDev, &ci, nullptr, &dev) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateDevice failed"); }
  vkGetDeviceQueue(dev, qf.graphics, 0, &graphicsQ);
  vkGetDeviceQueue(dev, qf.present,  0, &presentQ);
}

// ------ Swapchain ------

VkSurfaceFormatKHR Application::chooseSurfaceFormat ()
{
  uint32_t cnt; vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &cnt, nullptr);
  std::vector<VkSurfaceFormatKHR> fmts(cnt);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &cnt, fmts.data());
  for (auto &f : fmts)
    { if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { return f; } }
  return fmts[0];
}

VkPresentModeKHR Application::choosePresentMode ()
{
  uint32_t cnt; vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &cnt, nullptr);
  std::vector<VkPresentModeKHR> modes(cnt);
  vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &cnt, modes.data());
  for (auto m : modes) { if (m == VK_PRESENT_MODE_MAILBOX_KHR) { return m; } }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Application::chooseExtent (const VkSurfaceCapabilitiesKHR &caps)
{
  if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) { return caps.currentExtent; }
  int w, h; glfwGetFramebufferSize(window, &w, &h);
  return { std::clamp((uint32_t)w, caps.minImageExtent.width,  caps.maxImageExtent.width),
           std::clamp((uint32_t)h, caps.minImageExtent.height, caps.maxImageExtent.height) };
}

void Application::createSwapchain ()
{
  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev, surface, &caps);
  auto fmt  = chooseSurfaceFormat();
  auto mode = choosePresentMode();
  auto ext  = chooseExtent(caps);
  uint32_t imgCnt = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) { imgCnt = std::min(imgCnt, caps.maxImageCount); }

  VkSwapchainCreateInfoKHR ci { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
  ci.surface          = surface;
  ci.minImageCount    = imgCnt;
  ci.imageFormat      = fmt.format;
  ci.imageColorSpace  = fmt.colorSpace;
  ci.imageExtent      = ext;
  ci.imageArrayLayers = 1;
  ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  uint32_t qis[] = { qf.graphics, qf.present };
  if (qf.graphics != qf.present)
  {
    ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
    ci.queueFamilyIndexCount = 2;
    ci.pQueueFamilyIndices   = qis;
  }
  else { ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; }
  ci.preTransform   = caps.currentTransform;
  ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  ci.presentMode    = mode;
  ci.clipped        = VK_TRUE;
  if (vkCreateSwapchainKHR(dev, &ci, nullptr, &swapchain) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateSwapchainKHR failed"); }

  vkGetSwapchainImagesKHR(dev, swapchain, &imgCnt, nullptr);
  scImages.resize(imgCnt);
  vkGetSwapchainImagesKHR(dev, swapchain, &imgCnt, scImages.data());
  scFormat = fmt.format; scExtent = ext;
}

// ------ Image Views ------

VkImageView Application::createImageView (VkImage img, VkFormat fmt, VkImageAspectFlags aspect)
{
  VkImageViewCreateInfo ci { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
  ci.image            = img;
  ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
  ci.format           = fmt;
  ci.subresourceRange = { aspect, 0, 1, 0, 1 };
  VkImageView view;
  if (vkCreateImageView(dev, &ci, nullptr, &view) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateImageView failed"); }
  return view;
}

void Application::createImageViews ()
{
  scViews.resize(scImages.size());
  for (size_t i = 0; i < scImages.size(); i++)
    { scViews[i] = createImageView(scImages[i], scFormat, VK_IMAGE_ASPECT_COLOR_BIT); }
}

// ------ Memory + Images ------

uint32_t Application::findMemoryType (uint32_t filter, VkMemoryPropertyFlags flags)
{
  VkPhysicalDeviceMemoryProperties props;
  vkGetPhysicalDeviceMemoryProperties(physDev, &props);
  for (uint32_t i = 0; i < props.memoryTypeCount; i++)
    { if ((filter & (1<<i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) { return i; } }
  throw std::runtime_error("No suitable memory type");
}

void Application::createImage (uint32_t w, uint32_t h, VkFormat fmt, VkImageTiling tiling,
                  VkImageUsageFlags usage, VkMemoryPropertyFlags memProps,
                  VkImage &img, VkDeviceMemory &mem)
{
  VkImageCreateInfo ci { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
  ci.imageType      = VK_IMAGE_TYPE_2D;
  ci.extent         = { w, h, 1 };
  ci.mipLevels      = 1;
  ci.arrayLayers    = 1;
  ci.format         = fmt;
  ci.tiling         = tiling;
  ci.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  ci.usage          = usage;
  ci.samples        = VK_SAMPLE_COUNT_1_BIT;
  ci.sharingMode    = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateImage(dev, &ci, nullptr, &img) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateImage failed"); }
  VkMemoryRequirements req; vkGetImageMemoryRequirements(dev, img, &req);
  VkMemoryAllocateInfo ai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
  ai.allocationSize  = req.size;
  ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, memProps);
  if (vkAllocateMemory(dev, &ai, nullptr, &mem) != VK_SUCCESS)
    { throw std::runtime_error("vkAllocateMemory failed"); }
  vkBindImageMemory(dev, img, mem, 0);
}

// ------ Depth ------

VkFormat Application::findDepthFormat ()
{
  for (auto fmt : { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT })
  {
    VkFormatProperties p; vkGetPhysicalDeviceFormatProperties(physDev, fmt, &p);
    if (p.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { return fmt; }
  }
  throw std::runtime_error("No suitable depth format");
}

void Application::createDepthResources ()
{
  VkFormat fmt = findDepthFormat();
  createImage(scExtent.width, scExtent.height, fmt, VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
              depthImage, depthMem);
  depthView = createImageView(depthImage, fmt, VK_IMAGE_ASPECT_DEPTH_BIT);
}

// ------ Render Pass ------

void Application::createRenderPass ()
{
  VkAttachmentDescription color { }, depth { };
  color.format         = scFormat;
  color.samples        = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  depth.format         = findDepthFormat();
  depth.samples        = VK_SAMPLE_COUNT_1_BIT;
  depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorRef { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
  VkAttachmentReference depthRef { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

  VkSubpassDescription sub { };
  sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount    = 1;
  sub.pColorAttachments       = &colorRef;
  sub.pDepthStencilAttachment = &depthRef;

  VkSubpassDependency dep { };
  dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass    = 0;
  dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  std::array<VkAttachmentDescription, 2> atts = { color, depth };
  VkRenderPassCreateInfo ci { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
  ci.attachmentCount = atts.size();
  ci.pAttachments    = atts.data();
  ci.subpassCount    = 1;
  ci.pSubpasses      = &sub;
  ci.dependencyCount = 1;
  ci.pDependencies   = &dep;
  if (vkCreateRenderPass(dev, &ci, nullptr, &renderPass) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateRenderPass failed"); }
}

// ------ Descriptor Set Layout ------

void Application::createDescriptorSetLayout ()
{
  VkDescriptorSetLayoutBinding b { };
  b.binding         = 0;
  b.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  b.descriptorCount = 1;
  b.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;
  VkDescriptorSetLayoutCreateInfo ci { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
  ci.bindingCount = 1;
  ci.pBindings    = &b;
  if (vkCreateDescriptorSetLayout(dev, &ci, nullptr, &descLayout) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateDescriptorSetLayout failed"); }
}

// ------ Pipeline ------

VkShaderModule Application::createShaderModule (const std::vector<char> &code)
{
  VkShaderModuleCreateInfo ci { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
  ci.codeSize = code.size();
  ci.pCode    = (const uint32_t*)code.data();
  VkShaderModule m;
  if (vkCreateShaderModule(dev, &ci, nullptr, &m) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateShaderModule failed"); }
  return m;
}

void Application::createGraphicsPipeline ()
{
  auto vc = readFile("shaders/compiled/shader.vert.spv");
  auto fc = readFile("shaders/compiled/shader.frag.spv");
  VkShaderModule vm = createShaderModule(vc), fm = createShaderModule(fc);

  VkPipelineShaderStageCreateInfo stages[2] = { };
  stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vm;
  stages[0].pName  = "main";
  stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fm;
  stages[1].pName  = "main";

  auto bind  = Vertex::binding();
  auto attrs = Vertex::attribs();
  VkPipelineVertexInputStateCreateInfo vi { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
  vi.vertexBindingDescriptionCount   = 1;
  vi.pVertexBindingDescriptions      = &bind;
  vi.vertexAttributeDescriptionCount = attrs.size();
  vi.pVertexAttributeDescriptions    = attrs.data();

  VkPipelineInputAssemblyStateCreateInfo ia { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  // Dynamic viewport/scissor so no pipeline rebuild on resize
  VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
  VkPipelineDynamicStateCreateInfo dyn { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates    = dynStates;

  VkPipelineViewportStateCreateInfo vs { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
  vs.viewportCount = 1;
  vs.scissorCount  = 1;

  VkPipelineRasterizationStateCreateInfo rs { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode    = VK_CULL_MODE_BACK_BIT;
  rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth   = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo ds { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
  ds.depthTestEnable  = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp   = VK_COMPARE_OP_LESS;

  VkPipelineColorBlendAttachmentState cba { };
  cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
  cb.attachmentCount = 1;
  cb.pAttachments    = &cba;

  VkPipelineLayoutCreateInfo pli { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
  pli.setLayoutCount = 1;
  pli.pSetLayouts    = &descLayout;
  if (vkCreatePipelineLayout(dev, &pli, nullptr, &pipeLayout) != VK_SUCCESS)
    { throw std::runtime_error("vkCreatePipelineLayout failed"); }

  VkGraphicsPipelineCreateInfo ci { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
  ci.stageCount          = 2;
  ci.pStages             = stages;
  ci.pVertexInputState   = &vi;
  ci.pInputAssemblyState = &ia;
  ci.pViewportState      = &vs;
  ci.pRasterizationState = &rs;
  ci.pMultisampleState   = &ms;
  ci.pDepthStencilState  = &ds;
  ci.pColorBlendState    = &cb;
  ci.pDynamicState       = &dyn;
  ci.layout              = pipeLayout;
  ci.renderPass          = renderPass;
  ci.subpass             = 0;
  if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateGraphicsPipelines failed"); }

  vkDestroyShaderModule(dev, vm, nullptr);
  vkDestroyShaderModule(dev, fm, nullptr);
}

// ------ Framebuffers ------

void Application::createFramebuffers ()
{
  framebuffers.resize(scViews.size());
  for (size_t i = 0; i < scViews.size(); i++)
  {
    std::array<VkImageView, 2> atts = { scViews[i], depthView };
    VkFramebufferCreateInfo ci { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    ci.renderPass      = renderPass;
    ci.attachmentCount = atts.size();
    ci.pAttachments    = atts.data();
    ci.width           = scExtent.width;
    ci.height          = scExtent.height;
    ci.layers          = 1;
    if (vkCreateFramebuffer(dev, &ci, nullptr, &framebuffers[i]) != VK_SUCCESS)
      { throw std::runtime_error("vkCreateFramebuffer failed"); }
  }
}

// ------ Buffers ------

void Application::createBuffer (VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                   VkBuffer &buf, VkDeviceMemory &mem)
{
  VkBufferCreateInfo ci { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
  ci.size        = size;
  ci.usage       = usage;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(dev, &ci, nullptr, &buf) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateBuffer failed"); }
  VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, buf, &req);
  VkMemoryAllocateInfo ai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
  ai.allocationSize  = req.size;
  ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
  if (vkAllocateMemory(dev, &ai, nullptr, &mem) != VK_SUCCESS)
    { throw std::runtime_error("vkAllocateMemory failed"); }
  vkBindBufferMemory(dev, buf, mem, 0);
}

void Application::copyBuffer (VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
  VkCommandBufferAllocateInfo ai { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
  ai.commandPool        = cmdPool;
  ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cb; vkAllocateCommandBuffers(dev, &ai, &cb);
  VkCommandBufferBeginInfo bi { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cb, &bi);
  VkBufferCopy region { 0, 0, size }; vkCmdCopyBuffer(cb, src, dst, 1, &region);
  vkEndCommandBuffer(cb);
  VkSubmitInfo si { VK_STRUCTURE_TYPE_SUBMIT_INFO };
  si.commandBufferCount = 1;
  si.pCommandBuffers    = &cb;
  vkQueueSubmit(graphicsQ, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQ);
  vkFreeCommandBuffers(dev, cmdPool, 1, &cb);
}

void Application::createVertexBuffer ()
{
  VkDeviceSize size = sizeof(VERTS[0]) * VERTS.size();
  VkBuffer stageBuf; VkDeviceMemory stageMem;
  createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stageBuf, stageMem);
  void *data; vkMapMemory(dev, stageMem, 0, size, 0, &data);
  memcpy(data, VERTS.data(), size); vkUnmapMemory(dev, stageMem);
  createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertBuf, vertMem);
  copyBuffer(stageBuf, vertBuf, size);
  vkDestroyBuffer(dev, stageBuf, nullptr); vkFreeMemory(dev, stageMem, nullptr);
}

void Application::createIndexBuffer ()
{
  VkDeviceSize size = sizeof(IDXS[0]) * IDXS.size();
  VkBuffer stageBuf; VkDeviceMemory stageMem;
  createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stageBuf, stageMem);
  void *data; vkMapMemory(dev, stageMem, 0, size, 0, &data);
  memcpy(data, IDXS.data(), size); vkUnmapMemory(dev, stageMem);
  createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, idxBuf, idxMem);
  copyBuffer(stageBuf, idxBuf, size);
  vkDestroyBuffer(dev, stageBuf, nullptr); vkFreeMemory(dev, stageMem, nullptr);
}

void Application::createUniformBuffers ()
{
  VkDeviceSize size = sizeof(UBO);
  uboBufs.resize(MAX_FRAMES); uboMems.resize(MAX_FRAMES); uboMapped.resize(MAX_FRAMES);
  for (int i = 0; i < MAX_FRAMES; i++)
  {
    createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 uboBufs[i], uboMems[i]);
    vkMapMemory(dev, uboMems[i], 0, size, 0, &uboMapped[i]);
  }
}

// ------ Descriptors ------

void Application::createDescriptorPool ()
{
  VkDescriptorPoolSize ps { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (uint32_t)MAX_FRAMES };
  VkDescriptorPoolCreateInfo ci { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
  ci.poolSizeCount = 1;
  ci.pPoolSizes    = &ps;
  ci.maxSets       = MAX_FRAMES;
  if (vkCreateDescriptorPool(dev, &ci, nullptr, &descPool) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateDescriptorPool failed"); }
}

void Application::createDescriptorSets ()
{
  std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES, descLayout);
  VkDescriptorSetAllocateInfo ai { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
  ai.descriptorPool     = descPool;
  ai.descriptorSetCount = MAX_FRAMES;
  ai.pSetLayouts        = layouts.data();
  descSets.resize(MAX_FRAMES);
  if (vkAllocateDescriptorSets(dev, &ai, descSets.data()) != VK_SUCCESS)
    { throw std::runtime_error("vkAllocateDescriptorSets failed"); }
  for (int i = 0; i < MAX_FRAMES; i++)
  {
    VkDescriptorBufferInfo bi { uboBufs[i], 0, sizeof(UBO) };
    VkWriteDescriptorSet w { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet          = descSets[i];
    w.dstBinding      = 0;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.descriptorCount = 1;
    w.pBufferInfo     = &bi;
    vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
  }
}

// ------ Command Pool + Buffers ------

void Application::createCommandPool ()
{
  VkCommandPoolCreateInfo ci { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
  ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  ci.queueFamilyIndex = qf.graphics;
  if (vkCreateCommandPool(dev, &ci, nullptr, &cmdPool) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateCommandPool failed"); }
}

void Application::createCommandBuffers ()
{
  cmdBufs.resize(MAX_FRAMES);
  VkCommandBufferAllocateInfo ai { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
  ai.commandPool        = cmdPool;
  ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = MAX_FRAMES;
  if (vkAllocateCommandBuffers(dev, &ai, cmdBufs.data()) != VK_SUCCESS)
    { throw std::runtime_error("vkAllocateCommandBuffers failed"); }
}

void Application::recordCommandBuffer (VkCommandBuffer cb, uint32_t imgIdx)
{
  VkCommandBufferBeginInfo bi { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
  if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS)
    { throw std::runtime_error("vkBeginCommandBuffer failed"); }

  std::array<VkClearValue, 2> clears { };
  clears[0].color        = { 0.05f, 0.05f, 0.1f, 1.0f };
  clears[1].depthStencil = { 1.0f, 0 };

  VkRenderPassBeginInfo rpi { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
  rpi.renderPass      = renderPass;
  rpi.framebuffer     = framebuffers[imgIdx];
  rpi.renderArea      = { { 0, 0 }, scExtent };
  rpi.clearValueCount = clears.size();
  rpi.pClearValues    = clears.data();
  vkCmdBeginRenderPass(cb, &rpi, VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

  VkViewport vp { 0, 0, (float)scExtent.width, (float)scExtent.height, 0, 1 };
  VkRect2D   sc { { 0, 0 }, scExtent };
  vkCmdSetViewport(cb, 0, 1, &vp);
  vkCmdSetScissor(cb, 0, 1, &sc);

  VkBuffer vbufs[] = { vertBuf }; VkDeviceSize offsets[] = { 0 };
  vkCmdBindVertexBuffers(cb, 0, 1, vbufs, offsets);
  vkCmdBindIndexBuffer(cb, idxBuf, 0, VK_INDEX_TYPE_UINT16);
  vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeLayout,
                          0, 1, &descSets[frame], 0, nullptr);
  vkCmdDrawIndexed(cb, (uint32_t)IDXS.size(), 1, 0, 0, 0);

  vkCmdEndRenderPass(cb);
  if (vkEndCommandBuffer(cb) != VK_SUCCESS)
    { throw std::runtime_error("vkEndCommandBuffer failed"); }
}

// ------ Sync Objects ------

void Application::createSyncObjects ()
{
  imgAvail.resize(MAX_FRAMES); renderDone.resize(MAX_FRAMES); inFlight.resize(MAX_FRAMES);
  VkSemaphoreCreateInfo sci { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
  VkFenceCreateInfo     fci { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (int i = 0; i < MAX_FRAMES; i++)
  {
    if (vkCreateSemaphore(dev, &sci, nullptr, &imgAvail[i])   != VK_SUCCESS ||
        vkCreateSemaphore(dev, &sci, nullptr, &renderDone[i]) != VK_SUCCESS ||
        vkCreateFence(dev, &fci, nullptr, &inFlight[i])       != VK_SUCCESS)
      { throw std::runtime_error("Failed to create sync objects"); }
  }
}

// ------ Swapchain Resize ------

void Application::cleanupSwapchain ()
{
  vkDestroyImageView(dev, depthView, nullptr);
  vkDestroyImage(dev, depthImage, nullptr);
  vkFreeMemory(dev, depthMem, nullptr);
  for (auto fb : framebuffers) { vkDestroyFramebuffer(dev, fb, nullptr); }
  for (auto iv : scViews)      { vkDestroyImageView(dev, iv, nullptr); }
  vkDestroySwapchainKHR(dev, swapchain, nullptr);
}

void Application::recreateSwapchain ()
{
  int w = 0, h = 0;
  while (w == 0 || h == 0) { glfwGetFramebufferSize(window, &w, &h); glfwWaitEvents(); }
  vkDeviceWaitIdle(dev);
  cleanupSwapchain();
  createSwapchain(); createImageViews(); createDepthResources(); createFramebuffers();
}

// ------ UBO Update ------

void Application::updateUBO (uint32_t fi)
{
  static auto start = std::chrono::high_resolution_clock::now();
  float t = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - start).count();
  UBO ubo;
  ubo.model = glm::rotate(glm::mat4(1.0f), t * glm::radians(90.0f), glm::vec3(0.5f, 1.0f, 0.0f));
  ubo.view  = glm::lookAt(glm::vec3(2, 2, 2), glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));
  ubo.proj  = glm::perspective(glm::radians(45.0f), (float)scExtent.width / scExtent.height, 0.1f, 10.0f);
  ubo.proj[1][1] *= -1; // flip Y for Vulkan NDC
  memcpy(uboMapped[fi], &ubo, sizeof(ubo));
}

// ------ Draw Frame ------

void Application::drawFrame ()
{
  vkWaitForFences(dev, 1, &inFlight[frame], VK_TRUE, UINT64_MAX);

  uint32_t imgIdx;
  VkResult res = vkAcquireNextImageKHR(dev, swapchain, UINT64_MAX, imgAvail[frame], VK_NULL_HANDLE, &imgIdx);
  if (res == VK_ERROR_OUT_OF_DATE_KHR)
    { recreateSwapchain(); return; }
  else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
    { throw std::runtime_error("vkAcquireNextImageKHR failed"); }

  updateUBO(frame);
  vkResetFences(dev, 1, &inFlight[frame]);
  vkResetCommandBuffer(cmdBufs[frame], 0);
  recordCommandBuffer(cmdBufs[frame], imgIdx);

  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si { VK_STRUCTURE_TYPE_SUBMIT_INFO };
  si.waitSemaphoreCount   = 1;
  si.pWaitSemaphores      = &imgAvail[frame];
  si.pWaitDstStageMask    = &waitStage;
  si.commandBufferCount   = 1;
  si.pCommandBuffers      = &cmdBufs[frame];
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores    = &renderDone[frame];
  if (vkQueueSubmit(graphicsQ, 1, &si, inFlight[frame]) != VK_SUCCESS)
    { throw std::runtime_error("vkQueueSubmit failed"); }

  VkPresentInfoKHR pi { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores    = &renderDone[frame];
  pi.swapchainCount     = 1;
  pi.pSwapchains        = &swapchain;
  pi.pImageIndices      = &imgIdx;
  res = vkQueuePresentKHR(presentQ, &pi);
  if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || framebufferResized)
    { framebufferResized = false; recreateSwapchain(); }
  else if (res != VK_SUCCESS) { throw std::runtime_error("vkQueuePresentKHR failed"); }

  frame = (frame + 1) % MAX_FRAMES;
}

// ------ Init / Main Loop / Cleanup ------

void Application::initVulkan ()
{
  createInstance();
  if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
    { throw std::runtime_error("glfwCreateWindowSurface failed"); }
  pickPhysicalDevice();
  createLogicalDevice();
  createSwapchain();
  createImageViews();
  createRenderPass();
  createDescriptorSetLayout();
  createGraphicsPipeline();
  createCommandPool();
  createDepthResources();
  createFramebuffers();
  createVertexBuffer();
  createIndexBuffer();
  createUniformBuffers();
  createDescriptorPool();
  createDescriptorSets();
  createCommandBuffers();
  createSyncObjects();
}

void Application::mainLoop ()
{
  while (!glfwWindowShouldClose(window)) { glfwPollEvents(); drawFrame(); }
  vkDeviceWaitIdle(dev);
}

void Application::cleanup ()
{
  cleanupSwapchain();
  for (int i = 0; i < MAX_FRAMES; i++)
  {
    vkDestroyBuffer(dev, uboBufs[i], nullptr); vkFreeMemory(dev, uboMems[i], nullptr);
    vkDestroySemaphore(dev, imgAvail[i], nullptr);
    vkDestroySemaphore(dev, renderDone[i], nullptr);
    vkDestroyFence(dev, inFlight[i], nullptr);
  }
  vkDestroyDescriptorPool(dev, descPool, nullptr);
  vkDestroyDescriptorSetLayout(dev, descLayout, nullptr);
  vkDestroyBuffer(dev, idxBuf, nullptr);  vkFreeMemory(dev, idxMem, nullptr);
  vkDestroyBuffer(dev, vertBuf, nullptr); vkFreeMemory(dev, vertMem, nullptr);
  vkDestroyPipeline(dev, pipeline, nullptr);
  vkDestroyPipelineLayout(dev, pipeLayout, nullptr);
  vkDestroyRenderPass(dev, renderPass, nullptr);
  vkDestroyCommandPool(dev, cmdPool, nullptr);
  vkDestroyDevice(dev, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);
  glfwDestroyWindow(window);
  glfwTerminate();
}
