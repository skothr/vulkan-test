#include "Application.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
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

static const char *RT_EXTS[] = {
  VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
  VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
  VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
  VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
  VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
  VK_KHR_SPIRV_1_4_EXTENSION_NAME,
  VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
};
static constexpr uint32_t RT_EXT_COUNT = sizeof(RT_EXTS) / sizeof(RT_EXTS[0]);

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
  window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Cube (RT)", nullptr, nullptr);
  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, [](GLFWwindow *w, int, int) {
    ((Application*)glfwGetWindowUserPointer(w))->framebufferResized = true;
  });
  glfwSetKeyCallback(window, [](GLFWwindow *w, int key, int /*scancode*/, int action, int mods) {
    auto *app = (Application*)glfwGetWindowUserPointer(w);
    if (action == GLFW_PRESS && !app->controlPanel.wantsKeyboard())
      { app->keys.process(key, mods); }
  });

  // Left-click drag: orbit camera
  glfwSetMouseButtonCallback(window, [](GLFWwindow *w, int button, int action, int /*mods*/) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) { return; }
    auto *app = (Application*)glfwGetWindowUserPointer(w);
    if (app->controlPanel.wantsMouse()) { return; }
    app->mouseDown = (action == GLFW_PRESS);
    if (app->mouseDown)
    {
      // Disable cursor for unlimited drag range (no edge clamping)
      glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
      glfwGetCursorPos(w, &app->lastMouseX, &app->lastMouseY);
    }
    else { glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL); }
  });

  glfwSetCursorPosCallback(window, [](GLFWwindow *w, double x, double y) {
    auto *app = (Application*)glfwGetWindowUserPointer(w);
    if (!app->mouseDown) { return; }
    float dx = (float)(x - app->lastMouseX);
    float dy = (float)(y - app->lastMouseY);
    app->lastMouseX = x;
    app->lastMouseY = y;
    // Horizontal drag → orbit around world Z (ground-parallel, no roll)
    // Vertical drag   → change elevation; dy sign flipped so drag-down = look-down
    float minPhi = glm::asin(glm::clamp(0.4f / app->camDist, -1.0f, 1.0f));
    app->camTheta -= dx * app->settings.sensitivity;
    app->camPhi    = glm::clamp(app->camPhi + dy * app->settings.sensitivity, minPhi, 1.4f);
  });

  // Scroll: zoom in/out (10 % per notch)
  glfwSetScrollCallback(window, [](GLFWwindow *w, double /*dx*/, double dy) {
    auto *app    = (Application*)glfwGetWindowUserPointer(w);
    if (app->controlPanel.wantsMouse()) { return; }
    app->camDist = glm::clamp(app->camDist * (1.0f - (float)dy * app->settings.zoomSpeed), 0.5f, 20.0f);
    // Re-clamp phi in case the new distance puts the camera below the ground
    float minPhi = glm::asin(glm::clamp(0.4f / app->camDist, -1.0f, 1.0f));
    app->camPhi  = glm::max(app->camPhi, minPhi);
  });
}

// ------ Key Bindings ------

void Application::setupKeyBindings ()
{
  keys.bind(GLFW_KEY_Q,      GLFW_MOD_CONTROL, "Ctrl+Q    Quit",
            [this]() { glfwSetWindowShouldClose(window, GLFW_TRUE); });
  keys.bind(GLFW_KEY_ESCAPE, 0,                "Escape    Quit",
            [this]() { glfwSetWindowShouldClose(window, GLFW_TRUE); });
  keys.bind(GLFW_KEY_D,      GLFW_MOD_ALT,     "Alt+D     Cycle debug overlay (off / FPS / verbose)",
            [this]() { settings.debugLevel = (settings.debugLevel + 1) % 3; });
  keys.bind(GLFW_KEY_F1,     0,                "F1        Show key bindings",
            [this]() { keys.printHelp(); });

  keys.printHelp();  // print on startup so the user knows what's available
}

// ------ Instance ------

void Application::createInstance ()
{
  VkApplicationInfo ai { VK_STRUCTURE_TYPE_APPLICATION_INFO };
  ai.pApplicationName   = "VulkanCubeRT";
  ai.applicationVersion = VK_MAKE_VERSION(1,0,0);
  ai.pEngineName        = "None";
  ai.apiVersion         = VK_API_VERSION_1_2;

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

  std::set<std::string> required(RT_EXTS, RT_EXTS + RT_EXT_COUNT);
  for (auto &e : exts) { required.erase(e.extensionName); }
  if (!required.empty()) { return false; }

  try { findQueueFamilies(pd); } catch (...) { return false; }
  return true;
}

void Application::pickPhysicalDevice ()
{
  uint32_t cnt; vkEnumeratePhysicalDevices(instance, &cnt, nullptr);
  std::vector<VkPhysicalDevice> devs(cnt);
  vkEnumeratePhysicalDevices(instance, &cnt, devs.data());
  for (auto pd : devs) { if (isDeviceSuitable(pd)) { physDev = pd; return; } }
  throw std::runtime_error("No suitable GPU found (ray tracing not supported)");
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

  // Feature chain (pNext linked list)
  VkPhysicalDeviceBufferDeviceAddressFeaturesKHR bdaFeats {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR };
  bdaFeats.bufferDeviceAddress = VK_TRUE;

  VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeats {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
  asFeats.accelerationStructure = VK_TRUE;
  asFeats.pNext = &bdaFeats;

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtFeats {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
  rtFeats.rayTracingPipeline = VK_TRUE;
  rtFeats.pNext = &asFeats;

  VkDeviceCreateInfo ci { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
  ci.pNext                   = &rtFeats;
  ci.queueCreateInfoCount    = (uint32_t)qcis.size();
  ci.pQueueCreateInfos       = qcis.data();
  ci.enabledExtensionCount   = RT_EXT_COUNT;
  ci.ppEnabledExtensionNames = RT_EXTS;
  if (vkCreateDevice(physDev, &ci, nullptr, &dev) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateDevice failed"); }
  vkGetDeviceQueue(dev, qf.graphics, 0, &graphicsQ);
  vkGetDeviceQueue(dev, qf.present,  0, &presentQ);
}

void Application::loadRTFunctions ()
{
#define GET(m, fn) m = (PFN_vk##fn)vkGetDeviceProcAddr(dev, "vk" #fn)
  GET(pfnGetBufferAddress,   GetBufferDeviceAddressKHR);
  GET(pfnCreateAS,           CreateAccelerationStructureKHR);
  GET(pfnDestroyAS,          DestroyAccelerationStructureKHR);
  GET(pfnGetASBuildSizes,    GetAccelerationStructureBuildSizesKHR);
  GET(pfnCmdBuildAS,         CmdBuildAccelerationStructuresKHR);
  GET(pfnGetASDeviceAddress, GetAccelerationStructureDeviceAddressKHR);
  GET(pfnCreateRTPipelines,  CreateRayTracingPipelinesKHR);
  GET(pfnGetRTGroupHandles,  GetRayTracingShaderGroupHandlesKHR);
  GET(pfnCmdTraceRays,       CmdTraceRaysKHR);
#undef GET
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
  scMinImageCount = caps.minImageCount;
  uint32_t imgCnt = caps.minImageCount + 1;
  if (caps.maxImageCount > 0) { imgCnt = std::min(imgCnt, caps.maxImageCount); }

  VkSwapchainCreateInfoKHR ci { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
  ci.surface          = surface;
  ci.minImageCount    = imgCnt;
  ci.imageFormat      = fmt.format;
  ci.imageColorSpace  = fmt.colorSpace;
  ci.imageExtent      = ext;
  ci.imageArrayLayers = 1;
  // TRANSFER_DST_BIT: blit from ray tracing storage image into swapchain
  ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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

// ------ Storage Image ------

void Application::createStorageImage ()
{
  if (storageView) { vkDestroyImageView(dev, storageView, nullptr); }
  if (storageImg)  { vkDestroyImage(dev, storageImg, nullptr); }
  if (storageMem)  { vkFreeMemory(dev, storageMem, nullptr); }

  createImage(scExtent.width, scExtent.height,
              VK_FORMAT_R8G8B8A8_UNORM,
              VK_IMAGE_TILING_OPTIMAL,
              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
              storageImg, storageMem);
  storageView = createImageView(storageImg, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
}

// ------ Descriptor Set Layout ------

void Application::createDescriptorSetLayout ()
{
  // binding 0: TLAS         (raygen)
  // binding 1: storage img  (raygen)
  // binding 2: UBO          (raygen)
  // binding 3: vertex SSBO  (closest-hit)
  // binding 4: index SSBO   (closest-hit)
  VkDescriptorSetLayoutBinding bindings[5] = {};
  bindings[0].binding        = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags     = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

  bindings[1].binding        = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags     = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

  bindings[2].binding        = 2;
  bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  bindings[2].descriptorCount = 1;
  bindings[2].stageFlags     = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

  bindings[3].binding        = 3;
  bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[3].descriptorCount = 1;
  bindings[3].stageFlags     = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

  bindings[4].binding        = 4;
  bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  bindings[4].descriptorCount = 1;
  bindings[4].stageFlags     = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

  VkDescriptorSetLayoutCreateInfo ci { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
  ci.bindingCount = 5;
  ci.pBindings    = bindings;
  if (vkCreateDescriptorSetLayout(dev, &ci, nullptr, &descLayout) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateDescriptorSetLayout failed"); }
}

// ------ RT Pipeline ------

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

void Application::createRTPipeline ()
{
  auto rgenCode  = readFile("shaders/compiled/shader.rgen.spv");
  auto rmissCode = readFile("shaders/compiled/shader.rmiss.spv");
  auto rchitCode = readFile("shaders/compiled/shader.rchit.spv");

  VkShaderModule rgenMod  = createShaderModule(rgenCode);
  VkShaderModule rmissMod = createShaderModule(rmissCode);
  VkShaderModule rchitMod = createShaderModule(rchitCode);

  // 3 shader stages
  VkPipelineShaderStageCreateInfo stages[3] = {};
  stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage  = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
  stages[0].module = rgenMod;  stages[0].pName = "main";

  stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage  = VK_SHADER_STAGE_MISS_BIT_KHR;
  stages[1].module = rmissMod; stages[1].pName = "main";

  stages[2].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[2].stage  = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
  stages[2].module = rchitMod; stages[2].pName = "main";

  // 3 shader groups: raygen (0), miss (1), hit (2)
  VkRayTracingShaderGroupCreateInfoKHR groups[3] = {};
  // Group 0: raygen (general)
  groups[0].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
  groups[0].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
  groups[0].generalShader      = 0;
  groups[0].closestHitShader   = VK_SHADER_UNUSED_KHR;
  groups[0].anyHitShader       = VK_SHADER_UNUSED_KHR;
  groups[0].intersectionShader = VK_SHADER_UNUSED_KHR;
  // Group 1: miss (general)
  groups[1].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
  groups[1].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
  groups[1].generalShader      = 1;
  groups[1].closestHitShader   = VK_SHADER_UNUSED_KHR;
  groups[1].anyHitShader       = VK_SHADER_UNUSED_KHR;
  groups[1].intersectionShader = VK_SHADER_UNUSED_KHR;
  // Group 2: closest hit (triangles)
  groups[2].sType              = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
  groups[2].type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
  groups[2].generalShader      = VK_SHADER_UNUSED_KHR;
  groups[2].closestHitShader   = 2;
  groups[2].anyHitShader       = VK_SHADER_UNUSED_KHR;
  groups[2].intersectionShader = VK_SHADER_UNUSED_KHR;

  VkPushConstantRange pcRange {};
  pcRange.stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
  pcRange.offset     = 0;
  pcRange.size       = sizeof(PushConsts);

  VkPipelineLayoutCreateInfo pli { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
  pli.setLayoutCount         = 1;
  pli.pSetLayouts            = &descLayout;
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges    = &pcRange;
  if (vkCreatePipelineLayout(dev, &pli, nullptr, &pipeLayout) != VK_SUCCESS)
    { throw std::runtime_error("vkCreatePipelineLayout failed"); }

  // Query device limit so the slider in the UI can go up to that value safely
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
  VkPhysicalDeviceProperties2 props2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
  props2.pNext = &rtProps;
  vkGetPhysicalDeviceProperties2(physDev, &props2);

  VkRayTracingPipelineCreateInfoKHR ci { VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
  ci.stageCount                   = 3;
  ci.pStages                      = stages;
  ci.groupCount                   = 3;
  ci.pGroups                      = groups;
  ci.maxPipelineRayRecursionDepth = rtProps.maxRayRecursionDepth;
  ci.layout                       = pipeLayout;
  if (pfnCreateRTPipelines(dev, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateRayTracingPipelinesKHR failed"); }

  vkDestroyShaderModule(dev, rgenMod,  nullptr);
  vkDestroyShaderModule(dev, rmissMod, nullptr);
  vkDestroyShaderModule(dev, rchitMod, nullptr);
}

// ------ Buffers ------

void Application::createBuffer (VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                                 VkBuffer &buf, VkDeviceMemory &mem, bool deviceAddr)
{
  VkBufferCreateInfo ci { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
  ci.size        = size;
  ci.usage       = usage;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(dev, &ci, nullptr, &buf) != VK_SUCCESS)
    { throw std::runtime_error("vkCreateBuffer failed"); }
  VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, buf, &req);

  VkMemoryAllocateFlagsInfo mafi { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
  mafi.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

  VkMemoryAllocateInfo ai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
  ai.allocationSize  = req.size;
  ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
  if (deviceAddr) { ai.pNext = &mafi; }
  if (vkAllocateMemory(dev, &ai, nullptr, &mem) != VK_SUCCESS)
    { throw std::runtime_error("vkAllocateMemory failed"); }
  vkBindBufferMemory(dev, buf, mem, 0);
}

VkCommandBuffer Application::beginOneTimeCmd ()
{
  VkCommandBufferAllocateInfo ai { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
  ai.commandPool        = cmdPool;
  ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cb; vkAllocateCommandBuffers(dev, &ai, &cb);
  VkCommandBufferBeginInfo bi { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cb, &bi);
  return cb;
}

void Application::endOneTimeCmd (VkCommandBuffer cb)
{
  vkEndCommandBuffer(cb);
  VkSubmitInfo si { VK_STRUCTURE_TYPE_SUBMIT_INFO };
  si.commandBufferCount = 1; si.pCommandBuffers = &cb;
  vkQueueSubmit(graphicsQ, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(graphicsQ);
  vkFreeCommandBuffers(dev, cmdPool, 1, &cb);
}

void Application::copyBuffer (VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
  VkCommandBuffer cb = beginOneTimeCmd();
  VkBufferCopy region { 0, 0, size }; vkCmdCopyBuffer(cb, src, dst, 1, &region);
  endOneTimeCmd(cb);
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

  // Needs STORAGE (for hit shader), TRANSFER_DST, AS_BUILD_INPUT, SHADER_DEVICE_ADDRESS
  createBuffer(size,
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
               vertBuf, vertMem, true);
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

  createBuffer(size,
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
               idxBuf, idxMem, true);
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

// ------ Device Address Helper ------

VkDeviceAddress Application::getBufferAddress (VkBuffer buf)
{
  VkBufferDeviceAddressInfoKHR info { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR };
  info.buffer = buf;
  return pfnGetBufferAddress(dev, &info);
}

// ------ BLAS ------

void Application::createBLAS ()
{
  VkAccelerationStructureGeometryTrianglesDataKHR triangles {
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR };
  triangles.vertexFormat             = VK_FORMAT_R32G32B32_SFLOAT;
  triangles.vertexData.deviceAddress = getBufferAddress(vertBuf);
  triangles.vertexStride             = sizeof(Vertex);
  triangles.maxVertex                = (uint32_t)VERTS.size() - 1;
  triangles.indexType                = VK_INDEX_TYPE_UINT16;
  triangles.indexData.deviceAddress  = getBufferAddress(idxBuf);

  VkAccelerationStructureGeometryKHR geom { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
  geom.geometryType       = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  geom.geometry.triangles = triangles;
  geom.flags              = VK_GEOMETRY_OPAQUE_BIT_KHR;

  VkAccelerationStructureBuildGeometryInfoKHR buildInfo {
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
  buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  buildInfo.geometryCount = 1;
  buildInfo.pGeometries   = &geom;

  uint32_t primCount = (uint32_t)IDXS.size() / 3;
  VkAccelerationStructureBuildSizesInfoKHR sizeInfo {
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
  pfnGetASBuildSizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                     &buildInfo, &primCount, &sizeInfo);

  createBuffer(sizeInfo.accelerationStructureSize,
               VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, blasBuf, blasMem, true);

  VkAccelerationStructureCreateInfoKHR asci { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
  asci.buffer = blasBuf;
  asci.size   = sizeInfo.accelerationStructureSize;
  asci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
  pfnCreateAS(dev, &asci, nullptr, &blas);

  VkBuffer scratchBuf; VkDeviceMemory scratchMem;
  createBuffer(sizeInfo.buildScratchSize,
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, scratchBuf, scratchMem, true);

  buildInfo.mode                     = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  buildInfo.dstAccelerationStructure = blas;
  buildInfo.scratchData.deviceAddress = getBufferAddress(scratchBuf);

  VkAccelerationStructureBuildRangeInfoKHR range { primCount, 0, 0, 0 };
  const VkAccelerationStructureBuildRangeInfoKHR *pRange = &range;

  VkCommandBuffer cb = beginOneTimeCmd();
  pfnCmdBuildAS(cb, 1, &buildInfo, &pRange);
  endOneTimeCmd(cb);

  vkDestroyBuffer(dev, scratchBuf, nullptr); vkFreeMemory(dev, scratchMem, nullptr);
}

// ------ TLAS ------

void Application::createTLAS ()
{
  // Persistently mapped instance buffer (updated every frame with model matrix)
  VkDeviceSize instSize = sizeof(VkAccelerationStructureInstanceKHR);
  createBuffer(instSize,
               VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               instanceBuf, instanceMem, true);
  vkMapMemory(dev, instanceMem, 0, instSize, 0, &instanceMapped);

  // Initialize with identity transform
  VkAccelerationStructureDeviceAddressInfoKHR blasAddrInfo {
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
  blasAddrInfo.accelerationStructure = blas;

  VkAccelerationStructureInstanceKHR inst {};
  inst.transform                              = { 1,0,0,0, 0,1,0,0, 0,0,1,0 };
  inst.instanceCustomIndex                    = 0;
  inst.mask                                   = 0xFF;
  inst.instanceShaderBindingTableRecordOffset = 0;
  inst.flags                                  = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  inst.accelerationStructureReference         = pfnGetASDeviceAddress(dev, &blasAddrInfo);
  memcpy(instanceMapped, &inst, sizeof(inst));

  // Query build sizes
  VkAccelerationStructureGeometryInstancesDataKHR instData {
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
  instData.arrayOfPointers    = VK_FALSE;
  instData.data.deviceAddress = getBufferAddress(instanceBuf);

  VkAccelerationStructureGeometryKHR geom { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
  geom.geometryType        = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  geom.geometry.instances  = instData;

  VkAccelerationStructureBuildGeometryInfoKHR buildInfo {
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
  buildInfo.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  buildInfo.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  buildInfo.geometryCount = 1;
  buildInfo.pGeometries   = &geom;

  uint32_t instanceCount = 1;
  VkAccelerationStructureBuildSizesInfoKHR sizeInfo {
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
  pfnGetASBuildSizes(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                     &buildInfo, &instanceCount, &sizeInfo);

  createBuffer(sizeInfo.accelerationStructureSize,
               VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tlasBuf, tlasMem, true);

  VkAccelerationStructureCreateInfoKHR asci { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
  asci.buffer = tlasBuf;
  asci.size   = sizeInfo.accelerationStructureSize;
  asci.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  pfnCreateAS(dev, &asci, nullptr, &tlas);

  // Keep scratch buffer alive for per-frame TLAS rebuilds
  createBuffer(sizeInfo.buildScratchSize,
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tlasScratchBuf, tlasScratchMem, true);

  // Initial build
  buildInfo.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  buildInfo.dstAccelerationStructure  = tlas;
  buildInfo.scratchData.deviceAddress = getBufferAddress(tlasScratchBuf);

  VkAccelerationStructureBuildRangeInfoKHR range { 1, 0, 0, 0 };
  const VkAccelerationStructureBuildRangeInfoKHR *pRange = &range;

  VkCommandBuffer cb = beginOneTimeCmd();
  pfnCmdBuildAS(cb, 1, &buildInfo, &pRange);
  endOneTimeCmd(cb);
}

// ------ SBT ------

void Application::createSBT ()
{
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR };
  VkPhysicalDeviceProperties2 props2 { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
  props2.pNext = &rtProps;
  vkGetPhysicalDeviceProperties2(physDev, &props2);

  uint32_t handleSize      = rtProps.shaderGroupHandleSize;
  uint32_t handleAlign     = rtProps.shaderGroupHandleAlignment;
  uint32_t baseAlign       = rtProps.shaderGroupBaseAlignment;
  uint32_t handleAligned   = (handleSize + handleAlign - 1) & ~(handleAlign - 1);
  uint32_t groupCount      = 3; // rgen, miss, hit

  // Section offsets aligned to baseAlignment
  uint32_t rgenOff = 0;
  uint32_t missOff = ((handleAligned + baseAlign - 1) / baseAlign) * baseAlign;
  uint32_t hitOff  = missOff + ((handleAligned + baseAlign - 1) / baseAlign) * baseAlign;
  uint32_t sbtTotal = hitOff + handleAligned;

  // Fetch raw handles from the pipeline
  std::vector<uint8_t> handles(groupCount * handleSize);
  if (pfnGetRTGroupHandles(dev, pipeline, 0, groupCount, handles.size(), handles.data()) != VK_SUCCESS)
    { throw std::runtime_error("vkGetRayTracingShaderGroupHandlesKHR failed"); }

  createBuffer(sbtTotal,
               VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               sbtBuf, sbtMem, true);

  void *mapped; vkMapMemory(dev, sbtMem, 0, sbtTotal, 0, &mapped);
  uint8_t *d = (uint8_t*)mapped;
  memcpy(d + rgenOff, handles.data() + 0 * handleSize, handleSize);
  memcpy(d + missOff, handles.data() + 1 * handleSize, handleSize);
  memcpy(d + hitOff,  handles.data() + 2 * handleSize, handleSize);
  vkUnmapMemory(dev, sbtMem);

  VkDeviceAddress base = getBufferAddress(sbtBuf);
  sbtRgen = { base + rgenOff, handleAligned, handleAligned };
  sbtMiss = { base + missOff, handleAligned, handleAligned };
  sbtHit  = { base + hitOff,  handleAligned, handleAligned };
  sbtCall = {};
}

// ------ Descriptors ------

void Application::createDescriptorPool ()
{
  VkDescriptorPoolSize sizes[4] = {
    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, (uint32_t)MAX_FRAMES },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              (uint32_t)MAX_FRAMES },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             (uint32_t)MAX_FRAMES },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             (uint32_t)MAX_FRAMES * 2 },
  };
  VkDescriptorPoolCreateInfo ci { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
  ci.poolSizeCount = 4;
  ci.pPoolSizes    = sizes;
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
    // binding 0: TLAS
    VkWriteDescriptorSetAccelerationStructureKHR tlasWrite {
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
    tlasWrite.accelerationStructureCount = 1;
    tlasWrite.pAccelerationStructures    = &tlas;

    VkWriteDescriptorSet w0 { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w0.pNext           = &tlasWrite;
    w0.dstSet          = descSets[i];
    w0.dstBinding      = 0;
    w0.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    w0.descriptorCount = 1;

    // binding 1: storage image
    VkDescriptorImageInfo imgInfo { VK_NULL_HANDLE, storageView, VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet w1 { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w1.dstSet          = descSets[i];
    w1.dstBinding      = 1;
    w1.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w1.descriptorCount = 1;
    w1.pImageInfo      = &imgInfo;

    // binding 2: UBO
    VkDescriptorBufferInfo uboInfo { uboBufs[i], 0, sizeof(UBO) };
    VkWriteDescriptorSet w2 { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w2.dstSet          = descSets[i];
    w2.dstBinding      = 2;
    w2.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w2.descriptorCount = 1;
    w2.pBufferInfo     = &uboInfo;

    // binding 3: vertex SSBO
    VkDescriptorBufferInfo vertInfo { vertBuf, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet w3 { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w3.dstSet          = descSets[i];
    w3.dstBinding      = 3;
    w3.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w3.descriptorCount = 1;
    w3.pBufferInfo     = &vertInfo;

    // binding 4: index SSBO
    VkDescriptorBufferInfo idxInfo { idxBuf, 0, VK_WHOLE_SIZE };
    VkWriteDescriptorSet w4 { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w4.dstSet          = descSets[i];
    w4.dstBinding      = 4;
    w4.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w4.descriptorCount = 1;
    w4.pBufferInfo     = &idxInfo;

    VkWriteDescriptorSet writes[] = { w0, w1, w2, w3, w4 };
    vkUpdateDescriptorSets(dev, 5, writes, 0, nullptr);
  }
}

void Application::updateStorageImageDescriptor ()
{
  for (int i = 0; i < MAX_FRAMES; i++)
  {
    VkDescriptorImageInfo imgInfo { VK_NULL_HANDLE, storageView, VK_IMAGE_LAYOUT_GENERAL };
    VkWriteDescriptorSet w { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    w.dstSet          = descSets[i];
    w.dstBinding      = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w.descriptorCount = 1;
    w.pImageInfo      = &imgInfo;
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

  // --- Rebuild TLAS with updated model transform ---
  // Barrier: host write of instance buffer -> AS read
  VkMemoryBarrier hostBarrier { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
  hostBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
  hostBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  vkCmdPipelineBarrier(cb,
    VK_PIPELINE_STAGE_HOST_BIT,
    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
    0, 1, &hostBarrier, 0, nullptr, 0, nullptr);

  VkAccelerationStructureGeometryInstancesDataKHR instData {
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR };
  instData.arrayOfPointers    = VK_FALSE;
  instData.data.deviceAddress = getBufferAddress(instanceBuf);

  VkAccelerationStructureGeometryKHR tlasGeom { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
  tlasGeom.geometryType        = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  tlasGeom.geometry.instances  = instData;

  VkAccelerationStructureBuildGeometryInfoKHR tlasBuild {
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
  tlasBuild.type                      = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
  tlasBuild.flags                     = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
  tlasBuild.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
  tlasBuild.dstAccelerationStructure  = tlas;
  tlasBuild.geometryCount             = 1;
  tlasBuild.pGeometries               = &tlasGeom;
  tlasBuild.scratchData.deviceAddress = getBufferAddress(tlasScratchBuf);

  VkAccelerationStructureBuildRangeInfoKHR range { 1, 0, 0, 0 };
  const VkAccelerationStructureBuildRangeInfoKHR *pRange = &range;
  pfnCmdBuildAS(cb, 1, &tlasBuild, &pRange);

  // Barrier: AS write -> ray tracing read
  VkMemoryBarrier asBarrier { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
  asBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
  asBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
  vkCmdPipelineBarrier(cb,
    VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
    0, 1, &asBarrier, 0, nullptr, 0, nullptr);

  // --- Transition storage image: UNDEFINED -> GENERAL ---
  VkImageMemoryBarrier toGeneral { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
  toGeneral.srcAccessMask       = 0;
  toGeneral.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
  toGeneral.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
  toGeneral.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
  toGeneral.image               = storageImg;
  toGeneral.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  vkCmdPipelineBarrier(cb,
    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
    0, 0, nullptr, 0, nullptr, 1, &toGeneral);

  // --- Trace rays ---
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline);
  vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeLayout,
                          0, 1, &descSets[frame], 0, nullptr);
  auto pc = settings.toPushConsts();
  vkCmdPushConstants(cb, pipeLayout,
                     VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR,
                     0, sizeof(pc), &pc);
  pfnCmdTraceRays(cb, &sbtRgen, &sbtMiss, &sbtHit, &sbtCall,
                  scExtent.width, scExtent.height, 1);

  // --- Transition storage -> TRANSFER_SRC, swapchain -> TRANSFER_DST ---
  VkImageMemoryBarrier copyBarriers[2] = {};
  copyBarriers[0].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  copyBarriers[0].srcAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
  copyBarriers[0].dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT;
  copyBarriers[0].oldLayout        = VK_IMAGE_LAYOUT_GENERAL;
  copyBarriers[0].newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  copyBarriers[0].image            = storageImg;
  copyBarriers[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

  copyBarriers[1].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  copyBarriers[1].srcAccessMask    = 0;
  copyBarriers[1].dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
  copyBarriers[1].oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
  copyBarriers[1].newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  copyBarriers[1].image            = scImages[imgIdx];
  copyBarriers[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

  vkCmdPipelineBarrier(cb,
    VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
    0, 0, nullptr, 0, nullptr, 2, copyBarriers);

  // --- Blit storage image to swapchain (handles format differences) ---
  VkImageBlit blit {};
  blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
  blit.srcOffsets[1]  = { (int32_t)scExtent.width, (int32_t)scExtent.height, 1 };
  blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
  blit.dstOffsets[1]  = { (int32_t)scExtent.width, (int32_t)scExtent.height, 1 };
  vkCmdBlitImage(cb,
    storageImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    scImages[imgIdx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    1, &blit, VK_FILTER_NEAREST);

  // --- Transition swapchain: TRANSFER_DST -> COLOR_ATTACHMENT_OPTIMAL (for ImGui pass) ---
  VkImageMemoryBarrier toColorAtt { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
  toColorAtt.srcAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
  toColorAtt.dstAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  toColorAtt.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toColorAtt.newLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  toColorAtt.image            = scImages[imgIdx];
  toColorAtt.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  vkCmdPipelineBarrier(cb,
    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    0, 0, nullptr, 0, nullptr, 1, &toColorAtt);

  // --- ImGui render pass (overlays UI, transitions swapchain -> PRESENT_SRC_KHR) ---
  controlPanel.record(cb, imgIdx, scExtent);

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
  vkDestroyImageView(dev, storageView, nullptr); storageView = VK_NULL_HANDLE;
  vkDestroyImage(dev, storageImg, nullptr);      storageImg  = VK_NULL_HANDLE;
  vkFreeMemory(dev, storageMem, nullptr);        storageMem  = VK_NULL_HANDLE;
  for (auto iv : scViews) { vkDestroyImageView(dev, iv, nullptr); }
  vkDestroySwapchainKHR(dev, swapchain, nullptr);
}

void Application::recreateSwapchain ()
{
  int w = 0, h = 0;
  while (w == 0 || h == 0) { glfwGetFramebufferSize(window, &w, &h); glfwWaitEvents(); }
  vkDeviceWaitIdle(dev);
  cleanupSwapchain();
  createSwapchain(); createImageViews(); createStorageImage();
  updateStorageImageDescriptor();
  controlPanel.onSwapchainRecreate(dev, scFormat, scViews, scExtent);
}

// ------ UBO Update ------

void Application::updateUBO (uint32_t fi)
{
  static auto start = std::chrono::high_resolution_clock::now();
  float t = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - start).count();

  UBO ubo;
  ubo.model = settings.autoRotate
    ? glm::rotate(glm::mat4(1.0f), t * glm::radians(90.0f) * settings.rotSpeed, glm::vec3(0.5f, 1.0f, 0.0f))
    : glm::mat4(1.0f);
  glm::vec3 camPos = { camDist * glm::cos(camPhi) * glm::cos(camTheta),
                       camDist * glm::cos(camPhi) * glm::sin(camTheta),
                       camDist * glm::sin(camPhi) };
  ubo.view  = glm::lookAt(camPos, glm::vec3(0, 0, 0), glm::vec3(0, 0, 1));
  ubo.proj  = glm::perspective(glm::radians(45.0f), (float)scExtent.width / scExtent.height, 0.1f, 10.0f);
  ubo.proj[1][1] *= -1;
  memcpy(uboMapped[fi], &ubo, sizeof(ubo));

  // Write model matrix into TLAS instance (transposed: glm is column-major, VkTransformMatrix is row-major)
  glm::mat4 t4 = glm::transpose(ubo.model);
  VkAccelerationStructureInstanceKHR *inst = (VkAccelerationStructureInstanceKHR*)instanceMapped;
  memcpy(&inst->transform, &t4, sizeof(VkTransformMatrixKHR));
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
  controlPanel.beginFrame();
  controlPanel.draw({ fps, camTheta, camPhi, camDist });
  vkResetFences(dev, 1, &inFlight[frame]);
  vkResetCommandBuffer(cmdBufs[frame], 0);
  recordCommandBuffer(cmdBufs[frame], imgIdx);

  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
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

  // FPS tracking — update window title every second when debug overlay is on
  static auto   fpsStart      = std::chrono::high_resolution_clock::now();
  static uint32_t fpsFrameCount = 0;
  fpsFrameCount++;
  float elapsed = std::chrono::duration<float>(
    std::chrono::high_resolution_clock::now() - fpsStart).count();
  if (elapsed >= 1.0f)
  {
    fps           = fpsFrameCount / elapsed;
    fpsFrameCount = 0;
    fpsStart      = std::chrono::high_resolution_clock::now();
  }
}

// ------ Init / Main Loop / Cleanup ------

void Application::initVulkan ()
{
  createInstance();
  if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
    { throw std::runtime_error("glfwCreateWindowSurface failed"); }
  pickPhysicalDevice();
  createLogicalDevice();
  loadRTFunctions();
  createSwapchain();
  createImageViews();
  createCommandPool();
  createStorageImage();
  createVertexBuffer();
  createIndexBuffer();
  createUniformBuffers();
  createBLAS();
  createTLAS();
  createDescriptorSetLayout();
  createRTPipeline();
  createSBT();
  createDescriptorPool();
  createDescriptorSets();
  createCommandBuffers();
  createSyncObjects();
  controlPanel.init(window, instance, physDev, dev,
                    qf.graphics, graphicsQ, cmdPool,
                    scFormat, scViews, scExtent, scMinImageCount);
  setupKeyBindings();
}

void Application::mainLoop ()
{
  while (!glfwWindowShouldClose(window)) { glfwPollEvents(); drawFrame(); }
  vkDeviceWaitIdle(dev);
}

void Application::cleanup ()
{
  controlPanel.cleanup(dev);
  cleanupSwapchain();
  for (int i = 0; i < MAX_FRAMES; i++)
  {
    vkDestroyBuffer(dev, uboBufs[i], nullptr); vkFreeMemory(dev, uboMems[i], nullptr);
    vkDestroySemaphore(dev, imgAvail[i], nullptr);
    vkDestroySemaphore(dev, renderDone[i], nullptr);
    vkDestroyFence(dev, inFlight[i], nullptr);
  }
  vkDestroyBuffer(dev, sbtBuf, nullptr);         vkFreeMemory(dev, sbtMem, nullptr);
  pfnDestroyAS(dev, tlas, nullptr);
  vkDestroyBuffer(dev, tlasScratchBuf, nullptr); vkFreeMemory(dev, tlasScratchMem, nullptr);
  vkUnmapMemory(dev, instanceMem);
  vkDestroyBuffer(dev, instanceBuf, nullptr);    vkFreeMemory(dev, instanceMem, nullptr);
  vkDestroyBuffer(dev, tlasBuf, nullptr);        vkFreeMemory(dev, tlasMem, nullptr);
  pfnDestroyAS(dev, blas, nullptr);
  vkDestroyBuffer(dev, blasBuf, nullptr);        vkFreeMemory(dev, blasMem, nullptr);
  vkDestroyDescriptorPool(dev, descPool, nullptr);
  vkDestroyDescriptorSetLayout(dev, descLayout, nullptr);
  vkDestroyBuffer(dev, idxBuf, nullptr);         vkFreeMemory(dev, idxMem, nullptr);
  vkDestroyBuffer(dev, vertBuf, nullptr);        vkFreeMemory(dev, vertMem, nullptr);
  vkDestroyPipeline(dev, pipeline, nullptr);
  vkDestroyPipelineLayout(dev, pipeLayout, nullptr);
  vkDestroyCommandPool(dev, cmdPool, nullptr);
  vkDestroyDevice(dev, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);
  glfwDestroyWindow(window);
  glfwTerminate();
}
