#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "KeyBindings.hpp"
#include "Settings.hpp"
#include "ControlPanel.hpp"
#include "ScreenshotManager.hpp"
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <vector>

// ============================================================

const uint32_t WIDTH = 1280, HEIGHT = 800;
const int      MAX_FRAMES = 2;

struct Vertex
{
  glm::vec3 pos, color;
};

struct UBO
{
  glm::mat4 model, view, proj;
};

struct QueueFamilies
{
  uint32_t graphics, present;
};

// ============================================================

class Application
{
  public:
  void run()
  {
    initWindow();
    initVulkan();
    setupSignalHandlers();
    mainLoop();
    cleanup();
  }

  private:
  GLFWwindow *window             = nullptr;
  bool        framebufferResized = false;

  Settings          settings;
  ControlPanel      controlPanel{ settings };
  ScreenshotManager screenshotMgr;

  KeyBindings keys;
  float       fps = 0.0f;

  // Camera — spherical coordinates (Z-up), orbiting camTarget
  float     camTheta   = 0.785f;               // azimuthal angle (radians, around Z axis)
  float     camPhi     = 0.615f;               // elevation angle (radians, from XY plane)
  float     camDist    = 3.46f;                // distance from camTarget  (≈ length of (2,2,2))
  glm::vec3 camTarget  = { 0.0f, 0.0f, 0.0f }; // look-at / orbit pivot
  bool      mouseDown  = false;                // left button: orbit
  bool      middleDown = false;                // middle button: pan
  double    lastMouseX = 0.0, lastMouseY = 0.0;

  VkInstance       instance;
  VkSurfaceKHR     surface;
  VkPhysicalDevice physDev = VK_NULL_HANDLE;
  VkDevice         dev;
  VkQueue          graphicsQ, presentQ;
  QueueFamilies    qf;

  VkSwapchainKHR           swapchain;
  std::vector<VkImage>     scImages;
  VkFormat                 scFormat;
  VkExtent2D               scExtent;
  std::vector<VkImageView> scViews;
  uint32_t                 scMinImageCount = 2;

  // Ray tracing output image
  VkImage        storageImg  = VK_NULL_HANDLE;
  VkDeviceMemory storageMem  = VK_NULL_HANDLE;
  VkImageView    storageView = VK_NULL_HANDLE;

  VkDescriptorSetLayout descLayout;
  VkPipelineLayout      pipeLayout;
  VkPipeline            pipeline;

  VkCommandPool                cmdPool;
  std::vector<VkCommandBuffer> cmdBufs;

  // AABB buffer for analytic sphere intersection
  VkBuffer       aabbBuf = VK_NULL_HANDLE;
  VkDeviceMemory aabbMem = VK_NULL_HANDLE;

  std::vector<VkBuffer>       uboBufs;
  std::vector<VkDeviceMemory> uboMems;
  std::vector<void *>         uboMapped;

  std::vector<VkBuffer>       paramsBufs;
  std::vector<VkDeviceMemory> paramsMems;
  std::vector<void *>         paramsMapped;

  VkDescriptorPool             descPool;
  std::vector<VkDescriptorSet> descSets;

  std::vector<VkSemaphore> imgAvail, renderDone;
  std::vector<VkFence>     inFlight;
  uint32_t                 frame = 0;

  // Acceleration structures
  VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
  VkBuffer                   blasBuf;
  VkDeviceMemory             blasMem;

  VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
  VkBuffer                   tlasBuf;
  VkDeviceMemory             tlasMem;
  VkBuffer                   instanceBuf;
  VkDeviceMemory             instanceMem;
  void                      *instanceMapped;
  VkBuffer                   tlasScratchBuf;
  VkDeviceMemory             tlasScratchMem;

  // Shader binding table
  VkBuffer                        sbtBuf;
  VkDeviceMemory                  sbtMem;
  VkStridedDeviceAddressRegionKHR sbtRgen{}, sbtMiss{}, sbtHit{}, sbtCall{};

  // RT function pointers (loaded at runtime)
  PFN_vkGetBufferDeviceAddressKHR                pfnGetBufferAddress   = nullptr;
  PFN_vkCreateAccelerationStructureKHR           pfnCreateAS           = nullptr;
  PFN_vkDestroyAccelerationStructureKHR          pfnDestroyAS          = nullptr;
  PFN_vkGetAccelerationStructureBuildSizesKHR    pfnGetASBuildSizes    = nullptr;
  PFN_vkCmdBuildAccelerationStructuresKHR        pfnCmdBuildAS         = nullptr;
  PFN_vkGetAccelerationStructureDeviceAddressKHR pfnGetASDeviceAddress = nullptr;
  PFN_vkCreateRayTracingPipelinesKHR             pfnCreateRTPipelines  = nullptr;
  PFN_vkGetRayTracingShaderGroupHandlesKHR       pfnGetRTGroupHandles  = nullptr;
  PFN_vkCmdTraceRaysKHR                          pfnCmdTraceRays       = nullptr;

  void initWindow();
  void setupKeyBindings();
  void setupSignalHandlers();

  void createInstance();

  QueueFamilies findQueueFamilies(VkPhysicalDevice pd);
  bool          isDeviceSuitable(VkPhysicalDevice pd);
  void          pickPhysicalDevice();

  void createLogicalDevice();
  void loadRTFunctions();

  VkSurfaceFormatKHR chooseSurfaceFormat();
  VkPresentModeKHR   choosePresentMode();
  VkExtent2D         chooseExtent(const VkSurfaceCapabilitiesKHR &caps);
  void               createSwapchain();

  VkImageView createImageView(VkImage img, VkFormat fmt, VkImageAspectFlags aspect);
  void        createImageViews();

  uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags flags);
  void     createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags memProps,
                       VkImage &img, VkDeviceMemory &mem);

  void createStorageImage();

  void createDescriptorSetLayout();

  VkShaderModule createShaderModule(const std::vector<char> &code);
  void           createRTPipeline();

  void            createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer &buf, VkDeviceMemory &mem,
                               bool deviceAddr = false);
  VkCommandBuffer beginOneTimeCmd();
  void            endOneTimeCmd(VkCommandBuffer cb);
  void            copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
  void            createAABBBuffer();
  void            createUniformBuffers();

  VkDeviceAddress getBufferAddress(VkBuffer buf);
  void            createBLAS();
  void            createTLAS();
  void            createSBT();

  void createDescriptorPool();
  void createDescriptorSets();
  void updateStorageImageDescriptor();

  void createCommandPool();
  void createCommandBuffers();
  void recordCommandBuffer(VkCommandBuffer cb, uint32_t imgIdx);

  void createSyncObjects();

  void cleanupSwapchain();
  void recreateSwapchain();

  void updateUBO(uint32_t fi);
  void drawFrame();

  void initVulkan();
  void mainLoop();
  void cleanup();
};
