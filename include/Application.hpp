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

struct Vertex
{
  glm::vec3 pos, color;
  static VkVertexInputBindingDescription binding ()
  { return { 0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX }; }
  static std::array<VkVertexInputAttributeDescription, 2> attribs ()
  { return { { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) },
               { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color) } } }; }
};

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

  VkImage        depthImage;
  VkDeviceMemory depthMem;
  VkImageView    depthView;

  VkRenderPass             renderPass;
  VkDescriptorSetLayout    descLayout;
  VkPipelineLayout         pipeLayout;
  VkPipeline               pipeline;
  std::vector<VkFramebuffer> framebuffers;

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

  void initWindow ();

  void createInstance ();

  QueueFamilies findQueueFamilies (VkPhysicalDevice pd);
  bool isDeviceSuitable (VkPhysicalDevice pd);
  void pickPhysicalDevice ();

  void createLogicalDevice ();

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

  VkFormat findDepthFormat ();
  void createDepthResources ();

  void createRenderPass ();
  void createDescriptorSetLayout ();

  VkShaderModule createShaderModule (const std::vector<char> &code);
  void createGraphicsPipeline ();

  void createFramebuffers ();

  void createBuffer (VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                     VkBuffer &buf, VkDeviceMemory &mem);
  void copyBuffer (VkBuffer src, VkBuffer dst, VkDeviceSize size);
  void createVertexBuffer ();
  void createIndexBuffer ();
  void createUniformBuffers ();

  void createDescriptorPool ();
  void createDescriptorSets ();

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
