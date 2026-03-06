#include "ControlPanel.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <stdexcept>

// ============================================================

ControlPanel::ControlPanel (Settings &settings) : settings(settings) {}

// ============================================================
// Internal Vulkan helpers
// ============================================================

void ControlPanel::createRenderPass (VkDevice dev, VkFormat fmt)
{
  // LOAD_OP_LOAD: keep the blit result underneath the UI
  VkAttachmentDescription att {};
  att.format         = fmt;
  att.samples        = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
  att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
  att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;  // ready for present when done

  VkAttachmentReference ref { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

  VkSubpassDescription sub {};
  sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments    = &ref;

  // Ensure the blit is finished before we start writing color
  VkSubpassDependency dep {};
  dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass    = 0;
  dep.srcStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
  dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo ci { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
  ci.attachmentCount = 1; ci.pAttachments  = &att;
  ci.subpassCount    = 1; ci.pSubpasses    = &sub;
  ci.dependencyCount = 1; ci.pDependencies = &dep;
  if (vkCreateRenderPass(dev, &ci, nullptr, &imguiPass) != VK_SUCCESS)
    { throw std::runtime_error("ControlPanel: vkCreateRenderPass failed"); }
}

void ControlPanel::createFramebuffers (VkDevice dev,
                                       const std::vector<VkImageView> &views,
                                       VkExtent2D extent)
{
  framebuffers.resize(views.size());
  for (size_t i = 0; i < views.size(); i++)
  {
    VkFramebufferCreateInfo ci { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    ci.renderPass      = imguiPass;
    ci.attachmentCount = 1;
    ci.pAttachments    = &views[i];
    ci.width           = extent.width;
    ci.height          = extent.height;
    ci.layers          = 1;
    if (vkCreateFramebuffer(dev, &ci, nullptr, &framebuffers[i]) != VK_SUCCESS)
      { throw std::runtime_error("ControlPanel: vkCreateFramebuffer failed"); }
  }
}

void ControlPanel::destroyFramebuffers (VkDevice dev)
{
  for (auto fb : framebuffers) { vkDestroyFramebuffer(dev, fb, nullptr); }
  framebuffers.clear();
}

// ============================================================
// Public API
// ============================================================

void ControlPanel::init (GLFWwindow *win, VkInstance inst, VkPhysicalDevice physDev, VkDevice dev,
                         uint32_t queueFamily, VkQueue queue, VkCommandPool pool,
                         VkFormat scFormat, const std::vector<VkImageView> &scViews,
                         VkExtent2D extent, uint32_t minImageCount)
{
  // Descriptor pool for ImGui's internal use
  VkDescriptorPoolSize poolSizes[] = {
    { VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 },
  };
  VkDescriptorPoolCreateInfo poolCI { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
  poolCI.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolCI.maxSets       = 1000;
  poolCI.poolSizeCount = (uint32_t)(sizeof(poolSizes) / sizeof(poolSizes[0]));
  poolCI.pPoolSizes    = poolSizes;
  if (vkCreateDescriptorPool(dev, &poolCI, nullptr, &imguiPool) != VK_SUCCESS)
    { throw std::runtime_error("ControlPanel: vkCreateDescriptorPool failed"); }

  createRenderPass(dev, scFormat);
  createFramebuffers(dev, scViews, extent);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui::GetIO().IniFilename = nullptr;  // don't persist window layout

  ImGui_ImplGlfw_InitForVulkan(win, true);

  ImGui_ImplVulkan_InitInfo info {};
  info.Instance       = inst;
  info.PhysicalDevice = physDev;
  info.Device         = dev;
  info.QueueFamily    = queueFamily;
  info.Queue          = queue;
  info.PipelineCache  = VK_NULL_HANDLE;
  info.DescriptorPool = imguiPool;
  info.Subpass        = 0;
  info.MinImageCount  = minImageCount;
  info.ImageCount     = (uint32_t)scViews.size();
  info.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
  ImGui_ImplVulkan_Init(&info, imguiPass);

  // Upload font textures via a one-time command buffer
  VkCommandBufferAllocateInfo cbai { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
  cbai.commandPool        = pool;
  cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  VkCommandBuffer cb;
  vkAllocateCommandBuffers(dev, &cbai, &cb);
  VkCommandBufferBeginInfo bi { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cb, &bi);
  ImGui_ImplVulkan_CreateFontsTexture(cb);
  vkEndCommandBuffer(cb);
  VkSubmitInfo si { VK_STRUCTURE_TYPE_SUBMIT_INFO };
  si.commandBufferCount = 1; si.pCommandBuffers = &cb;
  vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);
  vkFreeCommandBuffers(dev, pool, 1, &cb);
  ImGui_ImplVulkan_DestroyFontUploadObjects();

  inited = true;
}

void ControlPanel::onSwapchainRecreate (VkDevice dev, VkFormat scFormat,
                                        const std::vector<VkImageView> &scViews,
                                        VkExtent2D extent)
{
  destroyFramebuffers(dev);
  vkDestroyRenderPass(dev, imguiPass, nullptr);
  createRenderPass(dev, scFormat);
  createFramebuffers(dev, scViews, extent);
  ImGui_ImplVulkan_SetMinImageCount((uint32_t)scViews.size());
}

void ControlPanel::cleanup (VkDevice dev)
{
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  destroyFramebuffers(dev);
  if (imguiPass)  { vkDestroyRenderPass(dev, imguiPass, nullptr);      imguiPass  = VK_NULL_HANDLE; }
  if (imguiPool)  { vkDestroyDescriptorPool(dev, imguiPool, nullptr);  imguiPool  = VK_NULL_HANDLE; }
  inited = false;
}

bool ControlPanel::wantsMouse    () const { return inited && ImGui::GetIO().WantCaptureMouse; }
bool ControlPanel::wantsKeyboard () const { return inited && ImGui::GetIO().WantCaptureKeyboard; }

// ============================================================
// Per-frame UI sections
// ============================================================

void ControlPanel::drawMaterialSection ()
{
  if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGui::SliderFloat("IOR",       &settings.ior,        1.0f, 3.0f);
  ImGui::SliderFloat("Tint",      &settings.tintAmount, 0.0f, 1.0f);
  ImGui::SliderInt  ("Max Depth", &settings.maxDepth,   1,    8);
}

void ControlPanel::drawAnimationSection ()
{
  if (!ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGui::Checkbox   ("Auto Rotate", &settings.autoRotate);
  ImGui::SliderFloat("Rot Speed",   &settings.rotSpeed, 0.0f, 5.0f);
}

void ControlPanel::drawCameraSection ()
{
  if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGui::SliderFloat("Sensitivity", &settings.sensitivity, 0.001f, 0.02f, "%.3f");
  ImGui::SliderFloat("Zoom Speed",  &settings.zoomSpeed,   0.01f,  0.5f,  "%.2f");
}

void ControlPanel::drawLightingSection ()
{
  if (!ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGui::SliderFloat("Azimuth",   &settings.sunAzimuth,   -3.14159f, 3.14159f, "%.2f rad");
  ImGui::SliderFloat("Elevation", &settings.sunElevation,  0.0f,     1.5707f,  "%.2f rad");
  ImGui::SliderFloat("Intensity", &settings.sunIntensity,  0.0f,     5.0f);
}

void ControlPanel::beginFrame ()
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ControlPanel::drawDebugOverlay (const DebugInfo &dbg)
{
  if (settings.debugLevel == 0) { return; }

  const float    PAD = 10.0f;
  ImGuiWindowFlags flags =
    ImGuiWindowFlags_NoDecoration   | ImGuiWindowFlags_AlwaysAutoResize |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoNav           | ImGuiWindowFlags_NoMove;

  ImGui::SetNextWindowPos(ImVec2(PAD, PAD), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.55f);
  ImGui::Begin("##debug", nullptr, flags);

  ImGui::Text("FPS  %.1f   (%.2f ms)", dbg.fps, 1000.0f / dbg.fps);

  if (settings.debugLevel >= 2)
  {
    ImGui::Separator();
    ImGui::Text("Camera  θ %.2f  φ %.2f  d %.2f",
                dbg.camTheta, dbg.camPhi, dbg.camDist);
    ImGui::Text("IOR %.2f   Tint %.2f   Depth %d",
                settings.ior, settings.tintAmount, settings.maxDepth);
    ImGui::Text("Sun  az %.2f  el %.2f  int %.2f",
                settings.sunAzimuth, settings.sunElevation, settings.sunIntensity);
  }

  ImGui::End();
}

void ControlPanel::draw (const DebugInfo &dbg)
{
  drawDebugOverlay(dbg);

  // Settings panel — anchored to the upper-right on first appearance
  ImGuiIO &io = ImGui::GetIO();
  ImGui::SetNextWindowPos (ImVec2(io.DisplaySize.x - 310.0f, 10.0f), ImGuiCond_Once);
  ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f), ImGuiCond_Once);
  ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoSavedSettings);
  drawMaterialSection();
  drawAnimationSection();
  drawCameraSection();
  drawLightingSection();
  ImGui::End();
}

void ControlPanel::record (VkCommandBuffer cb, uint32_t imgIdx, VkExtent2D extent)
{
  ImGui::Render();

  VkClearValue clearVal {};
  VkRenderPassBeginInfo bi { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
  bi.renderPass        = imguiPass;
  bi.framebuffer       = framebuffers[imgIdx];
  bi.renderArea.extent = extent;
  bi.clearValueCount   = 1;
  bi.pClearValues      = &clearVal;
  vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
  vkCmdEndRenderPass(cb);
}
