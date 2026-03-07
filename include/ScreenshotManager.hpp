#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <cstdint>

// ============================================================
// ScreenshotManager — owns all screenshot state and GPU resources.
//
// Lifecycle:
//   init()                — call once after Vulkan device + ImGui are ready
//   onSwapchainRecreate() — call after swapchain + storageImg are rebuilt
//   update()              — call each frame before ImGui phase (handles triggers)
//   drawPopups()          — call during ImGui phase (before recordCommandBuffer)
//   recordBeforeUI()      — in recordCommandBuffer, before ImGui render pass
//   recordAfterUI()       — in recordCommandBuffer, after ImGui render pass
//   processAfterPresent() — call after vkQueuePresentKHR each frame
//   cleanup()             — call before vkDestroyDevice
// ============================================================

class ScreenshotManager
{
public:
  // Stable Vulkan handles passed at init — not owned
  struct VkCtx
  {
    VkDevice         dev;
    VkPhysicalDevice physDev;
    VkQueue          queue;
    VkCommandPool    cmdPool;
  };

  enum class Mode   { SCENE_ONLY, WITH_UI };
  enum class Naming { AUTO, CUSTOM };

  void init               (const VkCtx &ctx, VkFormat scFmt, VkExtent2D ext,
                           const std::vector<VkImage> &scImgs, VkImage storageImg);
  void onSwapchainRecreate(VkFormat scFmt, VkExtent2D ext,
                           const std::vector<VkImage> &scImgs, VkImage storageImg);
  void cleanup            ();

  // Key binding triggers
  void requestCapture   (const std::string &defaultSuffix);   // scene-only auto-save
  void requestComposite (const std::string &defaultSuffix);   // with-UI auto-save
  void openOptions      (const std::string &defaultSuffix);   // open options popup

  bool isPopupOpen        () const { return popupOpen; }
  bool isRenderNeeded     () const { return renderNeeded; }
  void closePopup         ()       { if (popupVisible) { requestClose = true; } }

  // Per-frame flow (in call order each frame):
  void update             ();                                  // handle trigger flags
  void drawPopups         ();                                  // ImGui popups
  void recordBeforeUI     (VkCommandBuffer cb);                // scene preview copy
  void recordAfterUI      (VkCommandBuffer cb, uint32_t imgIdx); // composite preview + capture
  void processAfterPresent();                                  // read back composite

private:
  VkCtx  ctx    {};
  bool   inited = false;

  // Swapchain-dependent (updated on recreate)
  VkFormat             scFmt      = VK_FORMAT_UNDEFINED;
  VkExtent2D           scExt      {};
  std::vector<VkImage> scImgs;
  VkImage              storageImg = VK_NULL_HANDLE;

  // Trigger state
  bool        captureRequested   = false;
  bool        compositeRequested = false;
  bool        optionsOpen        = false;
  bool        optionsCapture     = false;
  bool        requestClose              = false;   // request popup close from outside ImGui frame
  bool        captureCompositeNextFrame = false;   // capture composite from swapchain this frame (popup skipped)
  bool        renderNeeded              = false;   // re-render scene once (e.g. after resize)
  bool        conflictIsCustom = false;
  std::string capturePathStr;
  std::string pendingSuffix;

  // Options popup state
  Mode    mode   = Mode::SCENE_ONLY;
  Naming  naming = Naming::AUTO;
  char    optionsSuffix  [256] = {};
  char    optionsDir     [512] = {};
  char    optionsFilename[256] = {};

  // Options overwrite confirmation (inline — replaces buttons when file exists)
  bool                 awaitingOverwriteConfirm = false;

  // Conflict / saved pixels
  std::string          conflictPath;
  char                 popupSuffix[256] = {};
  std::vector<uint8_t> pixels;
  uint32_t             pixW = 0, pixH = 0;

  // Composite capture staging buffer (With UI mode)
  bool           pendingComposite = false;
  VkBuffer       compBuf = VK_NULL_HANDLE;
  VkDeviceMemory compMem = VK_NULL_HANDLE;

  // Preview images — always at SHADER_READ_ONLY_OPTIMAL for ImGui sampling
  // scenePreview:    storageImg copy (Scene Only); updated while popup is open
  // compositePreview: swapchain blit (With UI); frozen at last pre-popup frame
  bool            popupOpen          = false;   // true only during the current frame's ImGui/record phase
  bool            popupVisible       = false;   // durable: true while popup is showing across frames
  VkSampler       sampler            = VK_NULL_HANDLE;

  VkImage         scenePreviewImg    = VK_NULL_HANDLE;
  VkDeviceMemory  scenePreviewMem    = VK_NULL_HANDLE;
  VkImageView     scenePreviewView   = VK_NULL_HANDLE;
  VkDescriptorSet sceneDescSet       = VK_NULL_HANDLE;

  VkImage         compPreviewImg     = VK_NULL_HANDLE;
  VkDeviceMemory  compPreviewMem     = VK_NULL_HANDLE;
  VkImageView     compPreviewView    = VK_NULL_HANDLE;
  VkDescriptorSet compPreviewDescSet = VK_NULL_HANDLE;

  // Screenshot logic
  int         nextIndex    ();
  std::string buildAutoPath(const std::string &suffix);
  void        capture      (const std::string &path);
  void        save         (const std::string &path);

  // ImGui popup draw
  void drawOptionsPopup ();

  // Preview resource management
  void createSampler           ();
  void createScenePreview      ();
  void destroyScenePreview     ();
  void updateSceneDescriptor   ();
  void createCompositePreview  ();
  void destroyCompositePreview ();
  void updateCompositeDescriptor();

  // Low-level Vulkan helpers (implemented using ctx handles)
  uint32_t        findMemoryType (uint32_t filter, VkMemoryPropertyFlags flags);
  void            createBuffer   (VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags props,
                                  VkBuffer &buf, VkDeviceMemory &mem);
  void            createImage    (uint32_t w, uint32_t h, VkFormat fmt,
                                  VkImageUsageFlags usage,
                                  VkImage &img, VkDeviceMemory &mem);
  VkImageView     createImageView(VkImage img, VkFormat fmt);
  VkCommandBuffer beginOneTimeCmd();
  void            endOneTimeCmd  (VkCommandBuffer cb);
  void            transitionImage(VkCommandBuffer cb, VkImage img,
                                  VkImageLayout oldLayout,  VkImageLayout newLayout,
                                  VkAccessFlags srcAccess,  VkAccessFlags dstAccess,
                                  VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);
};
