#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <vector>

// ============================================================

const uint32_t WIDTH = 800, HEIGHT = 600;
const int      MAX_FRAMES = 2;

struct Vertex { glm::vec3 pos, color; };

struct UBO { glm::mat4 model, view, proj; };

struct QueueFamilies { uint32_t graphics, present; };

// ============================================================

class Application
{
public:
  void run () { initWindow(); initVulkan(); mainLoop(); cleanup(); }

private:
  GLFWwindow *window = nullptr;
  bool        framebufferResized = false;

  VkInstance               instance;
  VkSurfaceKHR             surface;
  VkPhysicalDevice         physDev = VK_NULL_HANDLE;
  VkDevice                 dev;
  VkQueue                  graphicsQ, presentQ;
  QueueFamilies            qf;

  VkSwapchainKHR           swapchain;
  std::vector<VkImage>     scImages;
  VkFormat                 scFormat;
  VkExtent2D               scExtent;
  std::vector<VkImageView> scViews;

  // Ray tracing output image
  VkImage        storageImg  = VK_NULL_HANDLE;
  VkDeviceMemory storageMem  = VK_NULL_HANDLE;
  VkImageView    storageView = VK_NULL_HANDLE;

  VkDescriptorSetLayout    descLayout;
  VkPipelineLayout         pipeLayout;
  VkPipeline               pipeline;

  VkCommandPool                cmdPool;
  std::vector<VkCommandBuffer> cmdBufs;

  VkBuffer       vertBuf, idxBuf;
  VkDeviceMemory vertMem, idxMem;

  std::vector<VkBuffer>       uboBufs;
  std::vector<VkDeviceMemory> uboMems;
  std::vector<void*>          uboMapped;

  VkDescriptorPool             descPool;
  std::vector<VkDescriptorSet> descSets;

  std::vector<VkSemaphore> imgAvail, renderDone;
  std::vector<VkFence>     inFlight;
  uint32_t frame = 0;

  // Acceleration structures
  VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
  VkBuffer       blasBuf; VkDeviceMemory blasMem;

  VkAccelerationStructureKHR tlas = VK_NULL_HANDLE;
  VkBuffer       tlasBuf; VkDeviceMemory tlasMem;
  VkBuffer       instanceBuf; VkDeviceMemory instanceMem; void *instanceMapped;
  VkBuffer       tlasScratchBuf; VkDeviceMemory tlasScratchMem;

  // Shader binding table
  VkBuffer       sbtBuf; VkDeviceMemory sbtMem;
  VkStridedDeviceAddressRegionKHR sbtRgen {}, sbtMiss {}, sbtHit {}, sbtCall {};

  // RT function pointers (loaded at runtime)
  PFN_vkGetBufferDeviceAddressKHR                pfnGetBufferAddress   = nullptr;
  PFN_vkCreateAccelerationStructureKHR           pfnCreateAS           = nullptr;
  PFN_vkDestroyAccelerationStructureKHR          pfnDestroyAS          = nullptr;
  PFN_vkGetAccelerationStructureBuildSizesKHR    pfnGetASBuildSizes    = nullptr;
  PFN_vkCmdBuildAccelerationStructuresKHR        pfnCmdBuildAS         = nullptr;
  PFN_vkGetAccelerationStructureDeviceAddressKHR pfnGetASDeviceAddress = nullptr;
  PFN_vkCreateRayTracingPipelinesKHR             pfnCreateRTPipelines  = nullptr;
  PFN_vkGetRayTracingShaderGroupHandlesKHR       pfnGetRTGroupHandles  = nullptr;
  PFN_vkCmdTraceRaysKHR                         pfnCmdTraceRays       = nullptr;

  void initWindow ();

  void createInstance ();

  QueueFamilies findQueueFamilies (VkPhysicalDevice pd);
  bool isDeviceSuitable (VkPhysicalDevice pd);
  void pickPhysicalDevice ();

  void createLogicalDevice ();
  void loadRTFunctions ();

  VkSurfaceFormatKHR chooseSurfaceFormat ();
  VkPresentModeKHR   choosePresentMode ();
  VkExtent2D         chooseExtent (const VkSurfaceCapabilitiesKHR &caps);
  void createSwapchain ();

  VkImageView createImageView (VkImage img, VkFormat fmt, VkImageAspectFlags aspect);
  void createImageViews ();

  uint32_t findMemoryType (uint32_t filter, VkMemoryPropertyFlags flags);
  void createImage (uint32_t w, uint32_t h, VkFormat fmt, VkImageTiling tiling,
                    VkImageUsageFlags usage, VkMemoryPropertyFlags memProps,
                    VkImage &img, VkDeviceMemory &mem);

  void createStorageImage ();

  void createDescriptorSetLayout ();

  VkShaderModule createShaderModule (const std::vector<char> &code);
  void createRTPipeline ();

  void createBuffer (VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                     VkBuffer &buf, VkDeviceMemory &mem, bool deviceAddr = false);
  VkCommandBuffer beginOneTimeCmd ();
  void endOneTimeCmd (VkCommandBuffer cb);
  void copyBuffer (VkBuffer src, VkBuffer dst, VkDeviceSize size);
  void createVertexBuffer ();
  void createIndexBuffer ();
  void createUniformBuffers ();

  VkDeviceAddress getBufferAddress (VkBuffer buf);
  void createBLAS ();
  void createTLAS ();
  void createSBT ();

  void createDescriptorPool ();
  void createDescriptorSets ();
  void updateStorageImageDescriptor ();

  void createCommandPool ();
  void createCommandBuffers ();
  void recordCommandBuffer (VkCommandBuffer cb, uint32_t imgIdx);

  void createSyncObjects ();

  void cleanupSwapchain ();
  void recreateSwapchain ();

  void updateUBO (uint32_t fi);
  void drawFrame ();

  void initVulkan ();
  void mainLoop ();
  void cleanup ();
};
