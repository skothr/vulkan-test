#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>

#include "Settings.hpp"

struct ImFont;

// ============================================================
// ControlPanel — owns the Dear ImGui context, Vulkan descriptor
// pool, render pass, and per-swapchain-image framebuffers.
//
// Lifecycle:
//   init()              — call once after Vulkan device is ready
//   beginFrame() / draw()  — call every frame before recordCommandBuffer
//   record(cb, imgIdx) — call inside recordCommandBuffer (after blit)
//   onSwapchainRecreate() — call after recreateSwapchain
//   cleanup()           — call before vkDestroyDevice
// ============================================================

class ControlPanel
{
public:
  explicit ControlPanel (Settings &settings);
  ~ControlPanel () = default;

  void init (GLFWwindow *win, VkInstance inst, VkPhysicalDevice physDev, VkDevice dev,
             uint32_t queueFamily, VkQueue queue, VkCommandPool pool,
             VkFormat scFormat, const std::vector<VkImageView> &scViews,
             VkExtent2D extent, uint32_t minImageCount);

  void onSwapchainRecreate (VkDevice dev, VkFormat scFormat,
                            const std::vector<VkImageView> &scViews, VkExtent2D extent);

  void cleanup (VkDevice dev);

  struct DebugInfo { float fps; float camTheta, camPhi, camDist; };

  void beginFrame ();
  void draw       (const DebugInfo &dbg);
  void record     (VkCommandBuffer cb, uint32_t imgIdx, VkExtent2D extent);

  // Returns true only after init(); safe to call before init().
  bool wantsMouse    () const;
  bool wantsKeyboard () const;

  VkRenderPass renderPass () const { return imguiPass; }

private:
  Settings        &settings;
  bool             inited    = false;
  VkDescriptorPool imguiPool = VK_NULL_HANDLE;
  VkRenderPass     imguiPass = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers;
  ImFont          *fontDefault = nullptr;   // Roboto 15 px — UI panels
  ImFont          *fontLarge   = nullptr;   // Roboto 30 px — FPS overlay

  void createRenderPass    (VkDevice dev, VkFormat fmt);
  void createFramebuffers  (VkDevice dev, const std::vector<VkImageView> &views, VkExtent2D extent);
  void destroyFramebuffers (VkDevice dev);

  void drawDebugOverlay      (const DebugInfo &dbg);
  void drawSurfaceSection    ();
  void drawMaterialSection   ();
  void drawSunSection        ();
  void drawPointLightSection ();
  void drawFloorSection      ();
  void drawSkySection        ();
  void drawRenderingSection  ();
  void drawCameraSection     ();
  void drawAnimationSection  ();
};
