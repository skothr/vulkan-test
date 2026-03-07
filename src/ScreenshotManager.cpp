#include "ScreenshotManager.hpp"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <regex>
#include <stdexcept>
#include <string>

// ============================================================
// Lifecycle
// ============================================================

void ScreenshotManager::init (const VkCtx &c, VkFormat fmt, VkExtent2D ext,
                               const std::vector<VkImage> &imgs, VkImage stImg)
{
  ctx        = c;
  scFmt      = fmt;
  scExt      = ext;
  scImgs     = imgs;
  storageImg = stImg;
  createSampler();
  createScenePreview();
  createCompositePreview();
  inited = true;
}

void ScreenshotManager::onSwapchainRecreate (VkFormat fmt, VkExtent2D ext,
                                              const std::vector<VkImage> &imgs, VkImage stImg)
{
  scFmt      = fmt;
  scExt      = ext;
  scImgs     = imgs;
  storageImg = stImg;
  createScenePreview();      // destroys + recreates size-dependent resources
  createCompositePreview();
  if (popupVisible) { renderNeeded = true; captureCompositeNextFrame = true; }
}

void ScreenshotManager::cleanup ()
{
  if (!inited) { return; }
  // compPreviewDescSet / sceneDescSet are owned by ImGui's pool — freed with controlPanel
  destroyCompositePreview();
  destroyScenePreview();
  if (sampler) { vkDestroySampler(ctx.dev, sampler, nullptr); sampler = VK_NULL_HANDLE; }
  if (compBuf) { vkDestroyBuffer(ctx.dev, compBuf, nullptr); compBuf = VK_NULL_HANDLE; }
  if (compMem) { vkFreeMemory(ctx.dev, compMem, nullptr);    compMem = VK_NULL_HANDLE; }
  inited = false;
}

// ============================================================
// Triggers
// ============================================================

void ScreenshotManager::requestCapture (const std::string &suffix)
{
  captureRequested = true;
  pendingSuffix    = suffix;
}

void ScreenshotManager::requestComposite (const std::string &suffix)
{
  compositeRequested = true;
  pendingSuffix      = suffix;
}

void ScreenshotManager::openOptions (const std::string &suffix)
{
  if (popupVisible) { requestClose = true; return; }
  captureCompositeNextFrame = true;  // capture clean composite this frame; popup opens next frame
  optionsOpen               = true;
  pendingSuffix             = suffix;
}

// ============================================================
// Per-frame flow
// ============================================================

void ScreenshotManager::update ()
{
  if (captureRequested)
  {
    captureRequested = false;
    capture(buildAutoPath(pendingSuffix));
  }
  if (compositeRequested)
  {
    compositeRequested = false;
    capturePathStr   = buildAutoPath(pendingSuffix);
    conflictIsCustom = false;
    VkDeviceSize size = (VkDeviceSize)scExt.width * scExt.height * 4;
    createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 compBuf, compMem);
    pendingComposite = true;
  }
  if (optionsCapture)
  {
    optionsCapture = false;
    if (mode == Mode::SCENE_ONLY)
    {
      capture(capturePathStr);
    }
    else  // WITH_UI: allocate staging buffer; copy commands injected in recordAfterUI
    {
      VkDeviceSize size = (VkDeviceSize)scExt.width * scExt.height * 4;
      createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   compBuf, compMem);
      pendingComposite = true;
    }
  }
}

void ScreenshotManager::drawPopups ()
{
  drawOptionsPopup();
}

void ScreenshotManager::recordBeforeUI (VkCommandBuffer cb)
{
  // Scene preview: copy storageImg → scenePreviewImg each frame the popup is visible.
  if (!popupVisible || !scenePreviewImg) { return; }

  transitionImage(cb, scenePreviewImg,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_ACCESS_SHADER_READ_BIT,  VK_ACCESS_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkImageCopy region {};
  region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
  region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
  region.extent         = { scExt.width, scExt.height, 1 };
  vkCmdCopyImage(cb,
    storageImg,      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    scenePreviewImg, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    1, &region);

  transitionImage(cb, scenePreviewImg,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void ScreenshotManager::recordAfterUI (VkCommandBuffer cb, uint32_t imgIdx)
{
  VkImage scImage = scImgs[imgIdx];

  // Composite preview: blit swapchain → compPreviewImg only on designated capture frames
  // (popup is skipped that frame, so the composite contains scene + UI but no popup).
  bool captureComposite = captureCompositeNextFrame && compPreviewImg;
  if (captureComposite) { captureCompositeNextFrame = false; }
  renderNeeded = false;
  if (captureComposite)
  {
    VkImageMemoryBarrier bars[2] = {};
    // swapchain: PRESENT_SRC_KHR → TRANSFER_SRC_OPTIMAL
    bars[0].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bars[0].srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bars[0].dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT;
    bars[0].oldLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    bars[0].newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bars[0].image            = scImage;
    bars[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    // compPreviewImg: SHADER_READ_ONLY_OPTIMAL → TRANSFER_DST_OPTIMAL
    bars[1].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    bars[1].srcAccessMask    = VK_ACCESS_SHADER_READ_BIT;
    bars[1].dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
    bars[1].oldLayout        = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bars[1].newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bars[1].image            = compPreviewImg;
    bars[1].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cb,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 0, nullptr, 2, bars);

    // Blit handles BGRA_SRGB → R8G8B8A8_UNORM channel/format conversion
    VkImageBlit blit {};
    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.srcOffsets[1]  = { (int32_t)scExt.width, (int32_t)scExt.height, 1 };
    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.dstOffsets[1]  = { (int32_t)scExt.width, (int32_t)scExt.height, 1 };
    vkCmdBlitImage(cb,
      scImage,       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      compPreviewImg,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1, &blit, VK_FILTER_NEAREST);

    // Restore layouts
    bars[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    bars[0].dstAccessMask = 0;
    bars[0].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bars[0].newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    bars[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bars[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bars[1].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bars[1].newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(cb,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      0, 0, nullptr, 0, nullptr, 2, bars);
  }

  // Composite capture: copy swapchain → staging buffer for deferred readback
  if (pendingComposite && compBuf)
  {
    VkImageMemoryBarrier toSrc { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toSrc.srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toSrc.dstAccessMask    = VK_ACCESS_TRANSFER_READ_BIT;
    toSrc.oldLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toSrc.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.image            = scImage;
    toSrc.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cb,
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      0, 0, nullptr, 0, nullptr, 1, &toSrc);

    VkBufferImageCopy copyRegion {};
    copyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copyRegion.imageExtent      = { scExt.width, scExt.height, 1 };
    vkCmdCopyImageToBuffer(cb, scImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           compBuf, 1, &copyRegion);

    VkImageMemoryBarrier toPresent { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toPresent.srcAccessMask    = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask    = 0;
    toPresent.oldLayout        = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.image            = scImage;
    toPresent.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdPipelineBarrier(cb,
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
      0, 0, nullptr, 0, nullptr, 1, &toPresent);
  }

  popupOpen = false;   // reset each frame; drawOptionsPopup sets it when modal is active
}

void ScreenshotManager::processAfterPresent ()
{
  if (!pendingComposite) { return; }
  pendingComposite = false;
  vkDeviceWaitIdle(ctx.dev);

  VkDeviceSize size = (VkDeviceSize)scExt.width * scExt.height * 4;
  void *data;
  vkMapMemory(ctx.dev, compMem, 0, size, 0, &data);
  pixels.assign((uint8_t *)data, (uint8_t *)data + size);
  pixW = scExt.width; pixH = scExt.height;
  vkUnmapMemory(ctx.dev, compMem);
  vkDestroyBuffer(ctx.dev, compBuf, nullptr); compBuf = VK_NULL_HANDLE;
  vkFreeMemory(ctx.dev, compMem, nullptr);    compMem = VK_NULL_HANDLE;

  // Swapchain is often BGRA — swap channels so stbi gets RGBA
  if (scFmt == VK_FORMAT_B8G8R8A8_SRGB || scFmt == VK_FORMAT_B8G8R8A8_UNORM)
  {
    for (size_t i = 0; i < pixels.size(); i += 4)
      { std::swap(pixels[i], pixels[i + 2]); }
  }

  save(capturePathStr);
}

// ============================================================
// Screenshot logic
// ============================================================

int ScreenshotManager::nextIndex ()
{
  std::filesystem::create_directories("./images");
  std::regex pat { R"(screenshot_(\d+).*\.png)" };
  int        maxIdx = 0;
  for (auto &entry : std::filesystem::directory_iterator("./images"))
  {
    std::smatch m;
    std::string name = entry.path().filename().string();
    if (std::regex_match(name, m, pat)) { maxIdx = std::max(maxIdx, std::stoi(m[1].str())); }
  }
  return maxIdx + 1;
}

std::string ScreenshotManager::buildAutoPath (const std::string &suffix)
{
  int idx = nextIndex();
  return "./images/screenshot_" + std::to_string(idx)
       + (suffix.empty() ? "" : "_" + suffix) + ".png";
}

void ScreenshotManager::capture (const std::string &path)
{
  vkDeviceWaitIdle(ctx.dev);

  uint32_t     w = scExt.width, h = scExt.height;
  VkDeviceSize size = (VkDeviceSize)w * h * 4;

  VkBuffer stageBuf; VkDeviceMemory stageMem;
  createBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stageBuf, stageMem);

  VkCommandBuffer cb = beginOneTimeCmd();

  transitionImage(cb, storageImg,
                  VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                  VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkBufferImageCopy region {};
  region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
  region.imageExtent      = { w, h, 1 };
  vkCmdCopyImageToBuffer(cb, storageImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         stageBuf, 1, &region);

  transitionImage(cb, storageImg,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR);

  endOneTimeCmd(cb);

  void *data;
  vkMapMemory(ctx.dev, stageMem, 0, size, 0, &data);
  pixels.assign((uint8_t *)data, (uint8_t *)data + size);
  pixW = w; pixH = h;
  vkUnmapMemory(ctx.dev, stageMem);
  vkDestroyBuffer(ctx.dev, stageBuf, nullptr);
  vkFreeMemory(ctx.dev, stageMem, nullptr);

  save(path);
}

void ScreenshotManager::save (const std::string &path)
{
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  if (stbi_write_png(path.c_str(), (int)pixW, (int)pixH, 4, pixels.data(), (int)(pixW * 4)))
    { std::printf("Screenshot saved: %s\n", path.c_str()); }
  else
    { std::fprintf(stderr, "Screenshot failed: %s\n", path.c_str()); }
  pixels.clear();
}


// ============================================================
// ImGui popups
// ============================================================

void ScreenshotManager::drawOptionsPopup ()
{
  if (optionsOpen)
  {
    // Pre-fill suffix from default; pre-fill dir if empty
    strncpy(optionsSuffix, pendingSuffix.c_str(), sizeof(optionsSuffix) - 1);
    optionsSuffix[sizeof(optionsSuffix) - 1] = '\0';
    if (optionsDir[0] == '\0')
    {
      strncpy(optionsDir, "./images/", sizeof(optionsDir) - 1);
      optionsDir[sizeof(optionsDir) - 1] = '\0';
    }
    ImGui::OpenPopup("Screenshot Options");
    optionsOpen = false;
  }

  // Track popup visibility (safe here — inside an active ImGui frame)
  popupVisible = ImGui::IsPopupOpen("Screenshot Options");

  // Skip rendering popup this frame — composite is being captured without popup in it
  if (captureCompositeNextFrame) { return; }

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                          ImGuiCond_Appearing, { 0.5f, 0.5f });
  ImGui::SetNextWindowSizeConstraints({ 480.0f, 0.0f }, { 480.0f, FLT_MAX });
  if (!ImGui::BeginPopupModal("Screenshot Options", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize))
    { awaitingOverwriteConfirm = false; return; }
  popupOpen = true;   // freeze scene this frame
  // Full-screen dim drawn inside the modal's draw list (modal layer → above all windows)
  {
    ImGuiIO    &io    = ImGui::GetIO();
    ImGuiStyle &style = ImGui::GetStyle();
    ImDrawList *dl    = ImGui::GetWindowDrawList();
    ImVec2      p0    = { 0.0f, 0.0f }, p1 = io.DisplaySize;
    ImVec2      wpos  = ImGui::GetWindowPos();
    ImVec2      wsz   = ImGui::GetWindowSize();
    dl->PushClipRectFullScreen();
    dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 180));
    // Redraw popup window bg + border over the dim overlay so content is readable
    dl->AddRectFilled(wpos, { wpos.x + wsz.x, wpos.y + wsz.y },
                      ImGui::GetColorU32(ImGuiCol_PopupBg), style.WindowRounding);
    if (style.WindowBorderSize > 0.0f)
      dl->AddRect(wpos, { wpos.x + wsz.x, wpos.y + wsz.y },
                  ImGui::GetColorU32(ImGuiCol_Border), style.WindowRounding,
                  0, style.WindowBorderSize);
    dl->PopClipRect();
  }

  // ── Content ──────────────────────────────────────────────────
  ImGui::TextDisabled("Content"); ImGui::Separator();

  bool sceneOnly = (mode == Mode::SCENE_ONLY);
  if (ImGui::RadioButton("Scene only", sceneOnly))  { mode = Mode::SCENE_ONLY; }
  if (ImGui::IsItemHovered())
    { ImGui::SetTooltip("Raw ray-traced output — no debug overlay or UI panels."); }
  ImGui::SameLine();
  if (ImGui::RadioButton("With UI", !sceneOnly))    { mode = Mode::WITH_UI; }
  if (ImGui::IsItemHovered())
    { ImGui::SetTooltip("Full composite: scene + debug overlay + any open UI panels.\n"
                        "Captured after ImGui renders, so shows exactly what you see."); }

  // ── Naming ───────────────────────────────────────────────────
  ImGui::Spacing();
  ImGui::TextDisabled("Naming"); ImGui::Separator();

  bool isAuto = (naming == Naming::AUTO);
  if (ImGui::RadioButton("Auto", isAuto))    { naming = Naming::AUTO; }
  if (ImGui::IsItemHovered())
    { ImGui::SetTooltip("Auto-increment index with an optional suffix:\n"
                        "  ./images/screenshot_NNN[_suffix].png"); }
  ImGui::SameLine();
  if (ImGui::RadioButton("Custom", !isAuto)) { naming = Naming::CUSTOM; }
  if (ImGui::IsItemHovered())
    { ImGui::SetTooltip("Specify directory and filename manually.\n"
                        "'.png' is appended automatically if omitted."); }

  ImGui::Spacing();
  std::string prevPath;
  if (isAuto)
  {
    ImGui::Text("Suffix  "); ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##optSuffix", optionsSuffix, sizeof(optionsSuffix));
    prevPath = buildAutoPath(optionsSuffix);
  }
  else  // CUSTOM
  {
    ImGui::Text("Directory"); ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##optDir", optionsDir, sizeof(optionsDir));

    ImGui::Text("Filename "); ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##optFile", optionsFilename, sizeof(optionsFilename));
    if (ImGui::IsItemHovered())
      { ImGui::SetTooltip("Filename without extension — .png is appended automatically."); }

    std::string dir  = optionsDir[0]      ? optionsDir      : "./images/";
    std::string name = optionsFilename[0] ? optionsFilename : "screenshot";
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') { dir += '/'; }
    if (name.size() > 4 && name.substr(name.size() - 4) == ".png")
      { name = name.substr(0, name.size() - 4); }
    prevPath = dir + name + ".png";
  }

  // Live path preview
  bool prevExists = !prevPath.empty() && std::filesystem::exists(prevPath);
  ImGui::Spacing();
  ImGui::Text("Path     "); ImGui::SameLine();
  ImGui::TextColored(prevExists ? ImVec4 { 1.0f, 0.4f, 0.3f, 1.0f }
                                : ImVec4 { 0.75f, 0.75f, 0.75f, 1.0f },
                     "%s", prevPath.c_str());
  if (prevExists)
    { ImGui::SameLine(); ImGui::TextColored({ 1.0f, 0.4f, 0.3f, 1.0f }, "(exists)"); }

  // ── Preview ──────────────────────────────────────────────────
  ImGui::Spacing();
  ImGui::TextDisabled("Preview"); ImGui::Separator();
  {
    VkDescriptorSet texId = (mode == Mode::WITH_UI) ? compPreviewDescSet : sceneDescSet;
    if (texId)
    {
      float  avail  = ImGui::GetContentRegionAvail().x;
      float  aspect = (float)scExt.height / (float)scExt.width;
      ImVec2 sz     { avail, avail * aspect };
      ImGui::Image((ImTextureID)texId, sz);
    }
    else { ImGui::TextDisabled("(preview unavailable)"); }
  }

  // ── Buttons ──────────────────────────────────────────────────
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (!awaitingOverwriteConfirm)
  {
    if (ImGui::Button("Capture", { 100.0f, 0.0f }))
    {
      if (prevExists)
      {
        // Inline overwrite confirmation — stay in popup
        conflictPath = prevPath;
        conflictIsCustom = !isAuto;
        auto stem = std::filesystem::path(prevPath).stem().string();
        strncpy(popupSuffix, stem.c_str(), sizeof(popupSuffix) - 1);
        popupSuffix[sizeof(popupSuffix) - 1] = '\0';
        awaitingOverwriteConfirm = true;
      }
      else
      {
        capturePathStr   = prevPath;
        conflictIsCustom = !isAuto;
        optionsCapture   = true;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", { 100.0f, 0.0f }) || requestClose)
      { requestClose = false; ImGui::CloseCurrentPopup(); }
  }
  else
  {
    // ── Overwrite confirmation ────────────────────────────────
    ImGui::TextColored({ 1.0f, 0.4f, 0.3f, 1.0f }, "Already exists:");
    ImGui::SameLine();
    ImGui::TextColored({ 1.0f, 0.75f, 0.2f, 1.0f }, "%s", conflictPath.c_str());

    if (!isAuto)
    {
      ImGui::Spacing();
    }
    else
    {
      // Alternate suffix input for "Save as New"
      ImGui::Spacing();
      ImGui::Text("New suffix "); ImGui::SameLine();
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputText("##conflictSuffix", popupSuffix, sizeof(popupSuffix));
      std::string altPath   = buildAutoPath(popupSuffix);
      bool        altExists = std::filesystem::exists(altPath);
      ImGui::Text("Save as   "); ImGui::SameLine();
      ImGui::TextColored(altExists ? ImVec4 { 1.0f, 0.4f, 0.3f, 1.0f }
                                   : ImVec4 { 0.75f, 0.75f, 0.75f, 1.0f },
                         "%s", altPath.c_str());
      ImGui::Spacing();
      ImGui::BeginDisabled(altExists);
      if (ImGui::Button("Save as New", { 110.0f, 0.0f }))
      {
        capturePathStr   = altPath;
        conflictIsCustom = false;
        optionsCapture   = true;
        awaitingOverwriteConfirm = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
    }

    if (ImGui::Button("Overwrite", { 100.0f, 0.0f }))
    {
      capturePathStr   = conflictPath;
      optionsCapture   = true;
      awaitingOverwriteConfirm = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back", { 80.0f, 0.0f }))
      { awaitingOverwriteConfirm = false; }
    if (requestClose)
      { requestClose = false; awaitingOverwriteConfirm = false; ImGui::CloseCurrentPopup(); }
  }

  ImGui::EndPopup();
}

// ============================================================
// Preview resource management
// ============================================================

void ScreenshotManager::createSampler ()
{
  VkSamplerCreateInfo sci { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
  sci.magFilter    = VK_FILTER_LINEAR;
  sci.minFilter    = VK_FILTER_LINEAR;
  sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sci.maxLod       = VK_LOD_CLAMP_NONE;
  vkCreateSampler(ctx.dev, &sci, nullptr, &sampler);
}

void ScreenshotManager::createScenePreview ()
{
  destroyScenePreview();
  createImage(scExt.width, scExt.height, VK_FORMAT_R8G8B8A8_UNORM,
              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              scenePreviewImg, scenePreviewMem);
  scenePreviewView = createImageView(scenePreviewImg, VK_FORMAT_R8G8B8A8_UNORM);

  VkCommandBuffer cb = beginOneTimeCmd();
  transitionImage(cb, scenePreviewImg,
                  VK_IMAGE_LAYOUT_UNDEFINED,            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  0,                                    VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  endOneTimeCmd(cb);

  updateSceneDescriptor();
}

void ScreenshotManager::destroyScenePreview ()
{
  if (scenePreviewView) { vkDestroyImageView(ctx.dev, scenePreviewView, nullptr); scenePreviewView = VK_NULL_HANDLE; }
  if (scenePreviewImg)  { vkDestroyImage(ctx.dev, scenePreviewImg, nullptr);      scenePreviewImg  = VK_NULL_HANDLE; }
  if (scenePreviewMem)  { vkFreeMemory(ctx.dev, scenePreviewMem, nullptr);        scenePreviewMem  = VK_NULL_HANDLE; }
}

void ScreenshotManager::updateSceneDescriptor ()
{
  if (!sampler || !scenePreviewView) { return; }
  if (!sceneDescSet)
  {
    sceneDescSet = ImGui_ImplVulkan_AddTexture(sampler, scenePreviewView,
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return;
  }
  VkDescriptorImageInfo ii { sampler, scenePreviewView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
  VkWriteDescriptorSet  wr { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
  wr.dstSet = sceneDescSet; wr.dstBinding = 0; wr.descriptorCount = 1;
  wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr.pImageInfo = &ii;
  vkUpdateDescriptorSets(ctx.dev, 1, &wr, 0, nullptr);
}

void ScreenshotManager::createCompositePreview ()
{
  destroyCompositePreview();
  createImage(scExt.width, scExt.height, VK_FORMAT_R8G8B8A8_UNORM,
              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              compPreviewImg, compPreviewMem);
  compPreviewView = createImageView(compPreviewImg, VK_FORMAT_R8G8B8A8_UNORM);

  VkCommandBuffer cb = beginOneTimeCmd();
  transitionImage(cb, compPreviewImg,
                  VK_IMAGE_LAYOUT_UNDEFINED,            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  0,                                    VK_ACCESS_SHADER_READ_BIT,
                  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  endOneTimeCmd(cb);

  updateCompositeDescriptor();
}

void ScreenshotManager::destroyCompositePreview ()
{
  if (compPreviewView) { vkDestroyImageView(ctx.dev, compPreviewView, nullptr); compPreviewView = VK_NULL_HANDLE; }
  if (compPreviewImg)  { vkDestroyImage(ctx.dev, compPreviewImg, nullptr);      compPreviewImg  = VK_NULL_HANDLE; }
  if (compPreviewMem)  { vkFreeMemory(ctx.dev, compPreviewMem, nullptr);        compPreviewMem  = VK_NULL_HANDLE; }
}

void ScreenshotManager::updateCompositeDescriptor ()
{
  if (!sampler || !compPreviewView) { return; }
  if (!compPreviewDescSet)
  {
    compPreviewDescSet = ImGui_ImplVulkan_AddTexture(sampler, compPreviewView,
                                                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return;
  }
  VkDescriptorImageInfo ii { sampler, compPreviewView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
  VkWriteDescriptorSet  wr { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
  wr.dstSet = compPreviewDescSet; wr.dstBinding = 0; wr.descriptorCount = 1;
  wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr.pImageInfo = &ii;
  vkUpdateDescriptorSets(ctx.dev, 1, &wr, 0, nullptr);
}

// ============================================================
// Low-level Vulkan helpers
// ============================================================

uint32_t ScreenshotManager::findMemoryType (uint32_t filter, VkMemoryPropertyFlags flags)
{
  VkPhysicalDeviceMemoryProperties props;
  vkGetPhysicalDeviceMemoryProperties(ctx.physDev, &props);
  for (uint32_t i = 0; i < props.memoryTypeCount; i++)
    { if ((filter & (1 << i)) && (props.memoryTypes[i].propertyFlags & flags) == flags) { return i; } }
  throw std::runtime_error("ScreenshotManager: no suitable memory type");
}

void ScreenshotManager::createBuffer (VkDeviceSize size, VkBufferUsageFlags usage,
                                       VkMemoryPropertyFlags props,
                                       VkBuffer &buf, VkDeviceMemory &mem)
{
  VkBufferCreateInfo ci { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
  ci.size = size; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(ctx.dev, &ci, nullptr, &buf) != VK_SUCCESS)
    { throw std::runtime_error("ScreenshotManager: vkCreateBuffer failed"); }
  VkMemoryRequirements req; vkGetBufferMemoryRequirements(ctx.dev, buf, &req);
  VkMemoryAllocateInfo ai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
  ai.allocationSize  = req.size;
  ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, props);
  if (vkAllocateMemory(ctx.dev, &ai, nullptr, &mem) != VK_SUCCESS)
    { throw std::runtime_error("ScreenshotManager: vkAllocateMemory failed"); }
  vkBindBufferMemory(ctx.dev, buf, mem, 0);
}

void ScreenshotManager::createImage (uint32_t w, uint32_t h, VkFormat fmt,
                                      VkImageUsageFlags usage,
                                      VkImage &img, VkDeviceMemory &mem)
{
  VkImageCreateInfo ci { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
  ci.imageType     = VK_IMAGE_TYPE_2D;
  ci.extent        = { w, h, 1 };
  ci.mipLevels     = 1; ci.arrayLayers = 1;
  ci.format        = fmt;
  ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
  ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  ci.usage         = usage;
  ci.samples       = VK_SAMPLE_COUNT_1_BIT;
  ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateImage(ctx.dev, &ci, nullptr, &img) != VK_SUCCESS)
    { throw std::runtime_error("ScreenshotManager: vkCreateImage failed"); }
  VkMemoryRequirements req; vkGetImageMemoryRequirements(ctx.dev, img, &req);
  VkMemoryAllocateInfo ai { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
  ai.allocationSize  = req.size;
  ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (vkAllocateMemory(ctx.dev, &ai, nullptr, &mem) != VK_SUCCESS)
    { throw std::runtime_error("ScreenshotManager: vkAllocateMemory failed"); }
  vkBindImageMemory(ctx.dev, img, mem, 0);
}

VkImageView ScreenshotManager::createImageView (VkImage img, VkFormat fmt)
{
  VkImageViewCreateInfo ci { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
  ci.image            = img;
  ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
  ci.format           = fmt;
  ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  VkImageView view;
  if (vkCreateImageView(ctx.dev, &ci, nullptr, &view) != VK_SUCCESS)
    { throw std::runtime_error("ScreenshotManager: vkCreateImageView failed"); }
  return view;
}

VkCommandBuffer ScreenshotManager::beginOneTimeCmd ()
{
  VkCommandBufferAllocateInfo ai { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
  ai.commandPool = ctx.cmdPool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
  VkCommandBuffer cb; vkAllocateCommandBuffers(ctx.dev, &ai, &cb);
  VkCommandBufferBeginInfo bi { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cb, &bi);
  return cb;
}

void ScreenshotManager::endOneTimeCmd (VkCommandBuffer cb)
{
  vkEndCommandBuffer(cb);
  VkSubmitInfo si { VK_STRUCTURE_TYPE_SUBMIT_INFO };
  si.commandBufferCount = 1; si.pCommandBuffers = &cb;
  vkQueueSubmit(ctx.queue, 1, &si, VK_NULL_HANDLE);
  vkQueueWaitIdle(ctx.queue);
  vkFreeCommandBuffers(ctx.dev, ctx.cmdPool, 1, &cb);
}

void ScreenshotManager::transitionImage (VkCommandBuffer cb, VkImage img,
                                          VkImageLayout oldLayout,  VkImageLayout newLayout,
                                          VkAccessFlags srcAccess,  VkAccessFlags dstAccess,
                                          VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
  VkImageMemoryBarrier b { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
  b.srcAccessMask    = srcAccess;
  b.dstAccessMask    = dstAccess;
  b.oldLayout        = oldLayout;
  b.newLayout        = newLayout;
  b.image            = img;
  b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
  vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}
