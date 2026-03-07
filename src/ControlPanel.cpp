#include "ControlPanel.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cstring>
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
  ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg] = { 0.0f, 0.0f, 0.0f, 0.0f };
  ImGuiIO &io    = ImGui::GetIO();
  io.IniFilename = nullptr;  // don't persist window layout

  // Load Roboto — bundled in lib/fonts/ for portability
  const char *fontPath = "lib/fonts/Roboto-Regular.ttf";
  fontDefault = io.Fonts->AddFontFromFileTTF(fontPath, 15.0f);
  fontLarge   = io.Fonts->AddFontFromFileTTF(fontPath, 30.0f);
  if (!fontDefault || !fontLarge)
  {
    // Fallback: keep ImGui's built-in font (fontDefault/fontLarge stay nullptr)
    io.Fonts->AddFontDefault();
    fontDefault = fontLarge = nullptr;
  }

  ImGui_ImplGlfw_InitForVulkan(win, true);

  ImGui_ImplVulkan_InitInfo info {};
  info.ApiVersion     = VK_API_VERSION_1_2;
  info.Instance       = inst;
  info.PhysicalDevice = physDev;
  info.Device         = dev;
  info.QueueFamily    = queueFamily;
  info.Queue          = queue;
  info.PipelineCache  = VK_NULL_HANDLE;
  info.DescriptorPool = imguiPool;
  info.MinImageCount  = minImageCount;
  info.ImageCount     = (uint32_t)scViews.size();
  info.PipelineInfoMain.RenderPass  = imguiPass;
  info.PipelineInfoMain.Subpass     = 0;
  info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  ImGui_ImplVulkan_Init(&info);

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
  // Rebuild ImGui's internal pipeline against the new render pass
  ImGui_ImplVulkan_PipelineInfo pi {};
  pi.RenderPass  = imguiPass;
  pi.Subpass     = 0;
  pi.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  ImGui_ImplVulkan_CreateMainPipeline(&pi);
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

void ControlPanel::drawSurfaceSection ()
{
  if (!ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen)) { return; }

  int type = (int)settings.surfaceType;
  ImGui::RadioButton("Sphere",     &type, (int)SurfaceType::SPHERE);    ImGui::SameLine();
  ImGui::RadioButton("Cube",       &type, (int)SurfaceType::CUBE);      ImGui::SameLine();
  ImGui::RadioButton("4D Surface", &type, (int)SurfaceType::SURFACE_4D);
  settings.surfaceType = (SurfaceType)type;

  if (settings.surfaceType == SurfaceType::SPHERE)
  {
    ImGui::SliderFloat("Radius", &settings.sphereRadius, 0.1f, 1.5f, "%.2f");
    ImGui::SliderFloat("Z",      &settings.sphereHeight, -0.5f, 3.0f, "%.2f");
  }
  else if (settings.surfaceType == SurfaceType::CUBE)
  {
    ImGui::SliderFloat("Size",   &settings.cubeSize,     0.1f, 1.5f, "%.2f");
    ImGui::SliderFloat("Z",      &settings.sphereHeight, -0.5f, 3.0f, "%.2f");
  }
  else
  {
    ImGui::SliderFloat("Height##blobs", &settings.sphereHeight,      -0.5f,  3.0f, "%.2f");
    ImGui::SliderFloat("Threshold",     &settings.blobbiesThreshold,  0.01f,  2.0f, "%.3f");

    ImGui::Separator();
    ImGui::TextUnformatted("Cluster Center");
    ImGui::SliderFloat("X##ctr",  &settings.blobsCenterX, -2.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Y##ctr",  &settings.blobsCenterY, -2.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Z##ctr",  &settings.blobsCenterZ, -2.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Distance",&settings.blobsDist,     0.0f,  4.0f, "%.2f");

    ImGui::Separator();
    ImGui::TextUnformatted("Blobby 1");
    ImGui::SliderFloat("Amplitude##b1",   &settings.blob1Mu,     0.1f, 4.0f,  "%.2f");
    ImGui::SliderFloat("Sigma 3D##b1",    &settings.blob1Sigma,  0.05f, 2.0f, "%.3f");
    ImGui::SliderFloat("W (4D amp)##b1",  &settings.blob1W,     -2.0f, 2.0f,  "%.2f");
    ImGui::SliderFloat("Sigma 4D##b1",    &settings.blob1SigmaW, 0.05f, 2.0f, "%.3f");

    ImGui::Separator();
    ImGui::TextUnformatted("Blobby 2");
    ImGui::SliderFloat("Amplitude##b2",   &settings.blob2Mu,     0.1f, 4.0f,  "%.2f");
    ImGui::SliderFloat("Sigma 3D##b2",    &settings.blob2Sigma,  0.05f, 2.0f, "%.3f");
    ImGui::SliderFloat("W (4D amp)##b2",  &settings.blob2W,     -2.0f, 2.0f,  "%.2f");
    ImGui::SliderFloat("Sigma 4D##b2",    &settings.blob2SigmaW, 0.05f, 2.0f, "%.3f");
  }
}

void ControlPanel::drawMaterialSection ()
{
  if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGui::SliderFloat("IOR",       &settings.ior,        1.0f, 3.0f);
  ImGui::SliderFloat("Tint",      &settings.tintAmount, 0.0f, 1.0f);
  ImGui::ColorEdit3 ("Tint Color",&settings.tintR);
}

void ControlPanel::drawAnimationSection ()
{
  if (!ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGui::Checkbox   ("Auto Rotate", &settings.autoRotate);
  ImGui::SliderFloat("Rot Speed",   &settings.rotSpeed,  0.0f,  5.0f);
  ImGui::SliderFloat("Axis X",      &settings.rotAxisX, -1.0f,  1.0f, "%.2f");
  ImGui::SliderFloat("Axis Y",      &settings.rotAxisY, -1.0f,  1.0f, "%.2f");
  ImGui::SliderFloat("Axis Z",      &settings.rotAxisZ, -1.0f,  1.0f, "%.2f");
}

void ControlPanel::drawCameraSection ()
{
  if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGui::SliderFloat("FOV",         &settings.fov,         10.0f, 120.0f, "%.1f°");
  ImGui::SliderFloat("Sensitivity", &settings.sensitivity,  0.001f, 0.02f, "%.3f");
  ImGui::SliderFloat("Zoom Speed",  &settings.zoomSpeed,    0.01f,  0.5f,  "%.2f");
}

void ControlPanel::drawSunSection ()
{
  if (!ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGui::Checkbox("Enabled##sun", &settings.sunEnabled);
  if (settings.sunEnabled)
  {
    ImGui::SliderFloat("Azimuth##sun",   &settings.sunAzimuth,   -3.14159f, 3.14159f, "%.2f rad");
    ImGui::SliderFloat("Elevation##sun", &settings.sunElevation,  0.0f,     1.5707f,  "%.2f rad");
    ImGui::SliderFloat("Intensity##sun", &settings.sunIntensity,  0.0f,     5.0f);
    ImGui::Checkbox("Shadows##sun",  &settings.sunShadows);  ImGui::SameLine();
    ImGui::Checkbox("Caustics##sun", &settings.sunCaustics);
    ImGui::Separator();
    ImGui::SliderFloat("Disk Size",    &settings.sunConeHalf,  0.01f,   0.40f, "%.3f rad");
    ImGui::SliderFloat("Disk Exp",     &settings.sunDiskExp,   8.0f,  512.0f,  "%.0f");
    ImGui::ColorEdit3 ("Disk Color",   &settings.sunDiskR);
    ImGui::SliderFloat("Corona Exp",   &settings.sunCoronaExp, 1.0f,   20.0f,  "%.1f");
    ImGui::ColorEdit3 ("Corona Color", &settings.sunCoronaR);
  }
}

void ControlPanel::drawPointLightSection ()
{
  if (!ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGui::Checkbox("Enabled##pt", &settings.pointEnabled);
  if (settings.pointEnabled)
  {
    ImGui::SliderFloat("X##pt",         &settings.pointLightX,         -5.0f,  5.0f, "%.2f");
    ImGui::SliderFloat("Y##pt",         &settings.pointLightY,         -5.0f,  5.0f, "%.2f");
    ImGui::SliderFloat("Z##pt",         &settings.pointLightZ,         -1.0f,  5.0f, "%.2f");
    ImGui::SliderFloat("Radius##pt",    &settings.pointLightRadius,     0.0f,  2.0f, "%.2f");
    ImGui::ColorEdit3 ("Color##pt",     &settings.pointLightR);
    ImGui::SliderFloat("Intensity##pt", &settings.pointLightIntensity,  0.0f, 20.0f);
    ImGui::Checkbox("Shadows##pt",  &settings.pointShadows);  ImGui::SameLine();
    ImGui::Checkbox("Caustics##pt", &settings.pointCaustics);
  }
}

void ControlPanel::drawFloorSection ()
{
  if (!ImGui::CollapsingHeader("Floor", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGui::SliderFloat("Scale",       &settings.floorScale,  0.1f,  8.0f, "%.2f");
  ImGui::SliderFloat("Z##floor",    &settings.floorZ,     -3.0f,  0.0f, "%.2f");
  ImGui::ColorEdit3 ("Light Color", &settings.floorLightR);
  ImGui::ColorEdit3 ("Dark Color",  &settings.floorDarkR);
}

void ControlPanel::drawSkySection ()
{
  if (!ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGui::ColorEdit3("Horizon", &settings.skyHorizonR);
  ImGui::ColorEdit3("Zenith",  &settings.skyZenithR);
}


void ControlPanel::drawRenderingSection ()
{
  if (!ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) { return; }

  ImGui::Checkbox("Shadows",  &settings.shadowsEnabled);   ImGui::SameLine();
  ImGui::Checkbox("Caustics", &settings.causticsEnabled);
  ImGui::Separator();
  ImGui::SliderFloat("Ambient",   &settings.ambient,   0.0f, 0.5f,  "%.2f");
  ImGui::SliderInt  ("Max Depth", &settings.maxDepth,  1,    8);

  // ── GPU cost estimate ─────────────────────────────────────────────────────
  // Rays per floor pixel = caustic rays + shadow rays; blobby multiplies each
  // by march steps (each ray traverses the AABB with N field evaluations).
  bool   isBlobby = (settings.surfaceType == SurfaceType::SURFACE_4D);
  float  blobMult = isBlobby ? (settings.blobMarchSteps / 32.0f) : 1.0f;
  float  raysPerPx = (settings.nCaustics * 2.0f + settings.shadowSamples) * blobMult;
  // Rough threshold: >500 = caution, >2000 = danger (GPU TDR risk)
  ImVec4 costColor = (raysPerPx < 500.0f)  ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                   : (raysPerPx < 2000.0f) ? ImVec4(1.0f, 0.85f, 0.1f, 1.0f)
                                            : ImVec4(1.0f, 0.3f,  0.2f, 1.0f);
  ImGui::Separator();
  ImGui::TextColored(costColor, "GPU cost: ~%.0f ray-units/px", raysPerPx);
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("Rough relative cost per floor pixel.\n"
                      "Green < 500 | Yellow < 2000 | Red = TDR risk\n"
                      "= (caustics*2 + shadows) * blobMarchSteps/32\n"
                      "Cost is multiplicative — lower one knob to compensate.");
  }

  // ── Caustics ─────────────────────────────────────────────────────────────
  ImGui::Separator();
  ImGui::TextDisabled("Caustics");
  ImGui::SliderInt  ("Samples##caustic",  &settings.nCaustics,          1,    256);
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Backward caustic rays per floor pixel.\nMore = less noise, higher cost.\nCtrl+click to type a value above 256."); }
  ImGui::SliderFloat("Disk Scale",        &settings.causticDiskScale,  0.01f,  3.0f, "%.2f");
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Caustic sampling disk radius as a\nmultiple of the glass object radius.\n1.0 = tight fit, >1 smooths edges."); }
  ImGui::SliderFloat("Miss Falloff",      &settings.causticFalloff,    0.5f,  16.0f, "%.1f");
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Gaussian attenuation sharpness for\nrays that narrowly miss the light.\nHigher = harder caustic edges."); }
  ImGui::SliderFloat("Blend Radius",      &settings.causticBlendRadius, 0.0f,  2.0f, "%.3f");
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("World-space cell size for caustic sample blending.\nThree octaves (r, r/2, r/4) fill in detail at all scales.\n0 = per-pixel white noise (no blending)."); }
  ImGui::SliderFloat("Dither",            &settings.causticDitherAmt,   0.0f,  1.0f, "%.2f");
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Mix per-pixel noise into the blended samples to\nbreak up cell-boundary seams. 0 = pure blend,\n1 = pure per-pixel white noise."); }
  // ── Shadows ───────────────────────────────────────────────────────────────
  ImGui::Separator();
  ImGui::TextDisabled("Soft Shadows");
  ImGui::SliderInt  ("Samples##shadow",   &settings.shadowSamples,    1,   128);
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Shadow rays per floor pixel.\n1 = hard shadow, more = soft penumbra.\nCtrl+click to type a value above 16."); }
  ImGui::SliderFloat("Softness",          &settings.shadowSoftness,   0.0f,  4.0f, "%.2f");
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Penumbra disk radius as a multiple\nof the point light radius."); }

  // ── Blobby ────────────────────────────────────────────────────────────────
  ImGui::Separator();
  ImGui::TextDisabled("4D Surface");
  ImGui::SliderInt  ("March Steps",       &settings.blobMarchSteps,   8,   128);
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Ray-march steps through the 4D\nGaussian blobby AABB.\nMore = sharper surface, higher cost.\nCtrl+click to type a value above 128."); }
}

// ============================================================
// ImGui configuration UI
// ============================================================

void ControlPanel::drawImGuiStyleSection ()
{
  if (!ImGui::CollapsingHeader("Style", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGuiStyle &s = ImGui::GetStyle();

  // Theme selector
  static int theme = 0;  // 0=Dark, 1=Light, 2=Classic
  if (ImGui::Combo("Theme", &theme, "Dark\0Light\0Classic\0"))
  {
    switch (theme)
    {
      case 0: ImGui::StyleColorsDark();    break;
      case 1: ImGui::StyleColorsLight();   break;
      case 2: ImGui::StyleColorsClassic(); break;
    }
  }
  ImGui::Separator();

  // Font
  ImGui::SliderFloat("Font Scale",      &s.FontScaleMain, 0.5f, 3.0f, "%.2f");

  // Global
  ImGui::SliderFloat("Alpha",           &s.Alpha,         0.1f, 1.0f, "%.2f");
  ImGui::SliderFloat("Disabled Alpha",  &s.DisabledAlpha, 0.0f, 1.0f, "%.2f");

  // Windows
  ImGui::Separator();
  ImGui::TextDisabled("Windows");
  ImGui::SliderFloat2("Window Padding",  (float *)&s.WindowPadding, 0.0f, 20.0f, "%.0f");
  ImGui::SliderFloat ("Window Rounding", &s.WindowRounding,         0.0f, 14.0f, "%.0f");
  ImGui::SliderFloat ("Window Border",   &s.WindowBorderSize,       0.0f,  2.0f, "%.0f");
  ImGui::SliderFloat2("Window Min Size", (float *)&s.WindowMinSize, 0.0f, 200.0f, "%.0f");
  ImGui::SliderFloat2("Window Title Align", (float *)&s.WindowTitleAlign, 0.0f, 1.0f, "%.2f");

  // Child / Popup
  ImGui::SliderFloat("Child Rounding",   &s.ChildRounding,  0.0f, 14.0f, "%.0f");
  ImGui::SliderFloat("Child Border",     &s.ChildBorderSize, 0.0f, 2.0f, "%.0f");
  ImGui::SliderFloat("Popup Rounding",   &s.PopupRounding,  0.0f, 14.0f, "%.0f");
  ImGui::SliderFloat("Popup Border",     &s.PopupBorderSize, 0.0f, 2.0f, "%.0f");

  // Frame
  ImGui::Separator();
  ImGui::TextDisabled("Frames");
  ImGui::SliderFloat2("Frame Padding",   (float *)&s.FramePadding, 0.0f, 20.0f, "%.0f");
  ImGui::SliderFloat ("Frame Rounding",  &s.FrameRounding,        0.0f, 14.0f,  "%.0f");
  ImGui::SliderFloat ("Frame Border",    &s.FrameBorderSize,       0.0f,  2.0f,  "%.0f");

  // Items
  ImGui::Separator();
  ImGui::TextDisabled("Items / Spacing");
  ImGui::SliderFloat2("Item Spacing",       (float *)&s.ItemSpacing,      0.0f, 20.0f, "%.0f");
  ImGui::SliderFloat2("Item Inner Spacing", (float *)&s.ItemInnerSpacing, 0.0f, 20.0f, "%.0f");
  ImGui::SliderFloat2("Cell Padding",       (float *)&s.CellPadding,     0.0f, 20.0f, "%.0f");
  ImGui::SliderFloat2("Touch Extra Padding",(float *)&s.TouchExtraPadding,0.0f, 20.0f, "%.0f");
  ImGui::SliderFloat ("Indent Spacing",     &s.IndentSpacing,            0.0f, 40.0f,  "%.0f");
  ImGui::SliderFloat ("Columns Min Spacing",&s.ColumnsMinSpacing,        0.0f, 20.0f,  "%.0f");

  // Scrollbar
  ImGui::Separator();
  ImGui::TextDisabled("Scrollbar");
  ImGui::SliderFloat("Scrollbar Size",     &s.ScrollbarSize,     1.0f, 30.0f, "%.0f");
  ImGui::SliderFloat("Scrollbar Rounding", &s.ScrollbarRounding, 0.0f, 14.0f, "%.0f");
  ImGui::SliderFloat("Scrollbar Padding",  &s.ScrollbarPadding,  0.0f, 10.0f, "%.0f");

  // Grab
  ImGui::Separator();
  ImGui::TextDisabled("Grab");
  ImGui::SliderFloat("Grab Min Size", &s.GrabMinSize, 1.0f, 30.0f, "%.0f");
  ImGui::SliderFloat("Grab Rounding", &s.GrabRounding, 0.0f, 14.0f, "%.0f");

  // Tabs
  ImGui::Separator();
  ImGui::TextDisabled("Tabs");
  ImGui::SliderFloat("Tab Rounding",       &s.TabRounding,       0.0f, 14.0f, "%.0f");
  ImGui::SliderFloat("Tab Border",         &s.TabBorderSize,     0.0f,  2.0f, "%.0f");
  ImGui::SliderFloat("Tab Bar Border",     &s.TabBarBorderSize,  0.0f,  2.0f, "%.1f");
  ImGui::SliderFloat("Tab Bar Overline",   &s.TabBarOverlineSize, 0.0f, 4.0f, "%.1f");
  ImGui::SliderFloat("Tab Min Width",      &s.TabMinWidthBase,   0.0f, 200.0f, "%.0f");
  ImGui::SliderFloat("Tab Min Shrink",     &s.TabMinWidthShrink, 0.0f, 100.0f, "%.0f");

  // Misc visuals
  ImGui::Separator();
  ImGui::TextDisabled("Misc");
  ImGui::SliderFloat ("Log Slider Deadzone",   &s.LogSliderDeadzone,      0.0f, 20.0f,  "%.0f");
  ImGui::SliderFloat ("Image Rounding",        &s.ImageRounding,          0.0f, 14.0f,  "%.0f");
  ImGui::SliderFloat ("Image Border",          &s.ImageBorderSize,        0.0f,  2.0f,  "%.0f");
  ImGui::SliderFloat ("Separator Text Border", &s.SeparatorTextBorderSize, 0.0f, 4.0f,  "%.0f");
  ImGui::SliderFloat2("Button Text Align",     (float *)&s.ButtonTextAlign, 0.0f, 1.0f, "%.2f");
  ImGui::SliderFloat2("Selectable Text Align", (float *)&s.SelectableTextAlign, 0.0f, 1.0f, "%.2f");

  // Anti-aliasing / Tessellation
  ImGui::Separator();
  ImGui::TextDisabled("Rendering");
  ImGui::Checkbox("Anti-Aliased Lines",       &s.AntiAliasedLines);
  ImGui::Checkbox("Anti-Aliased Lines (Tex)", &s.AntiAliasedLinesUseTex);
  ImGui::Checkbox("Anti-Aliased Fill",        &s.AntiAliasedFill);
  ImGui::SliderFloat("Curve Tess Tol",      &s.CurveTessellationTol,    0.10f, 10.0f, "%.2f");
  ImGui::SliderFloat("Circle Tess Max Err", &s.CircleTessellationMaxError, 0.10f, 5.0f, "%.2f");

  // Hover delays
  ImGui::Separator();
  ImGui::TextDisabled("Hover");
  ImGui::SliderFloat("Hover Stationary Delay", &s.HoverStationaryDelay, 0.0f, 2.0f, "%.2f");
  ImGui::SliderFloat("Hover Delay Short",      &s.HoverDelayShort,     0.0f, 2.0f, "%.2f");
  ImGui::SliderFloat("Hover Delay Normal",     &s.HoverDelayNormal,    0.0f, 2.0f, "%.2f");
}

void ControlPanel::drawImGuiColorsSection ()
{
  if (!ImGui::CollapsingHeader("Colors")) { return; }
  ImGuiStyle &s = ImGui::GetStyle();

  // Filter
  static ImGuiTextFilter filter;
  filter.Draw("Filter##colors", -1.0f);
  ImGui::Spacing();

  for (int i = 0; i < ImGuiCol_COUNT; i++)
  {
    const char *name = ImGui::GetStyleColorName(i);
    if (!filter.PassFilter(name)) { continue; }
    ImGui::PushID(i);
    ImGui::ColorEdit4("##color", (float *)&s.Colors[i],
                      ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::TextUnformatted(name);
    ImGui::PopID();
  }
}

void ControlPanel::drawImGuiInputSection ()
{
  if (!ImGui::CollapsingHeader("Input", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGuiIO &io = ImGui::GetIO();

  // Mouse
  ImGui::TextDisabled("Mouse");
  ImGui::SliderFloat("Double-Click Time", &io.MouseDoubleClickTime,   0.05f, 1.0f,  "%.2f s");
  ImGui::SliderFloat("Double-Click Dist", &io.MouseDoubleClickMaxDist, 1.0f, 20.0f, "%.0f px");
  ImGui::SliderFloat("Drag Threshold",    &io.MouseDragThreshold,      1.0f, 20.0f, "%.0f px");
  ImGui::Checkbox   ("Software Cursor",   &io.MouseDrawCursor);
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Render a mouse cursor via ImGui\ninstead of the OS cursor."); }

  // Keyboard
  ImGui::Separator();
  ImGui::TextDisabled("Keyboard");
  ImGui::SliderFloat("Key Repeat Delay", &io.KeyRepeatDelay, 0.05f, 1.0f,  "%.3f s");
  ImGui::SliderFloat("Key Repeat Rate",  &io.KeyRepeatRate,  0.01f, 0.5f,  "%.3f s");
  ImGui::Checkbox("Cursor Blink",            &io.ConfigInputTextCursorBlink);
  ImGui::Checkbox("Enter Keeps Active",      &io.ConfigInputTextEnterKeepActive);
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Pressing Enter keeps the input\ntext widget active and selected."); }
  ImGui::Checkbox("Drag Click to Input",     &io.ConfigDragClickToInputText);
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Click-release on a drag widget\nenters text input mode."); }
  ImGui::Checkbox("Trickle Event Queue",     &io.ConfigInputTrickleEventQueue);
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Spread simultaneous input events\nacross multiple frames."); }
}

void ControlPanel::drawImGuiBehaviorSection ()
{
  if (!ImGui::CollapsingHeader("Behavior", ImGuiTreeNodeFlags_DefaultOpen)) { return; }
  ImGuiIO &io = ImGui::GetIO();
  ImGuiStyle &s = ImGui::GetStyle();

  // Window behavior
  ImGui::TextDisabled("Windows");
  ImGui::Checkbox("Resize From Edges",       &io.ConfigWindowsResizeFromEdges);
  ImGui::Checkbox("Move From Title Bar Only",&io.ConfigWindowsMoveFromTitleBarOnly);
  ImGui::Checkbox("Copy Contents (Ctrl+C)",  &io.ConfigWindowsCopyContentsWithCtrlC);
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Ctrl+C copies the contents of\nthe focused window to clipboard."); }
  ImGui::Checkbox("Scrollbar Scroll By Page",&io.ConfigScrollbarScrollByPage);
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Click outside scrollbar grab to\nscroll by page instead of jumping."); }
  ImGui::Checkbox("macOS Behaviors",         &io.ConfigMacOSXBehaviors);

  // Navigation
  ImGui::Separator();
  ImGui::TextDisabled("Navigation");
  ImGui::Checkbox("Nav Swap Gamepad Buttons",   &io.ConfigNavSwapGamepadButtons);
  ImGui::Checkbox("Nav Move Sets Mouse Pos",    &io.ConfigNavMoveSetMousePos);
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Keyboard/gamepad navigation\nteleports the mouse cursor."); }
  ImGui::Checkbox("Nav Capture Keyboard",       &io.ConfigNavCaptureKeyboard);
  ImGui::Checkbox("Nav Esc Clears Focus Item",  &io.ConfigNavEscapeClearFocusItem);
  ImGui::Checkbox("Nav Esc Clears Focus Window",&io.ConfigNavEscapeClearFocusWindow);
  ImGui::Checkbox("Nav Cursor Auto Visible",    &io.ConfigNavCursorVisibleAuto);
  ImGui::Checkbox("Nav Cursor Always Visible",  &io.ConfigNavCursorVisibleAlways);

  // Memory
  ImGui::Separator();
  ImGui::TextDisabled("Memory / Performance");
  ImGui::SliderFloat("Compact Timer", &io.ConfigMemoryCompactTimer, -1.0f, 120.0f, "%.0f s");
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Time before freeing unused transient\nbuffers. -1 = never free."); }
  ImGui::Checkbox("Font Allow User Scaling", &io.FontAllowUserScaling);
  if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Allow Ctrl+Wheel to scale\nindividual window text."); }

  // Docking (surface if compiled in)
  ImGui::Separator();
  ImGui::TextDisabled("Docking");
  ImGui::Checkbox("No Split",              &io.ConfigDockingNoSplit);
  ImGui::Checkbox("No Dock Over",          &io.ConfigDockingNoDockingOver);
  ImGui::Checkbox("With Shift",            &io.ConfigDockingWithShift);
  ImGui::Checkbox("Always Tab Bar",        &io.ConfigDockingAlwaysTabBar);
  ImGui::Checkbox("Transparent Payload",   &io.ConfigDockingTransparentPayload);
  ImGui::Checkbox("Node Close Button",     &s.DockingNodeHasCloseButton);
  ImGui::SliderFloat("Separator Size",     &s.DockingSeparatorSize, 1.0f, 10.0f, "%.0f");

  // Debug
  ImGui::Separator();
  ImGui::TextDisabled("Debug");
  ImGui::Checkbox("Error Recovery",              &io.ConfigErrorRecovery);
  ImGui::Checkbox("Highlight ID Conflicts",      &io.ConfigDebugHighlightIdConflicts);
  ImGui::Checkbox("Debug Begin Return Once",     &io.ConfigDebugBeginReturnValueOnce);
  ImGui::Checkbox("Debug Begin Return Loop",     &io.ConfigDebugBeginReturnValueLoop);
  ImGui::Checkbox("Debug Ignore Focus Loss",     &io.ConfigDebugIgnoreFocusLoss);
}

void ControlPanel::drawCaptureSection ()
{
  if (!ImGui::CollapsingHeader("Screenshot", ImGuiTreeNodeFlags_DefaultOpen)) { return; }

  ImGui::TextDisabled("Press F12 to capture — saved to ./images/");
  ImGui::Spacing();

  // Suffix input
  char buf[256];
  std::strncpy(buf, settings.screenshotSuffix.c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::InputText("##suffix", buf, sizeof(buf)))
    { settings.screenshotSuffix = buf; }
  ImGui::TextDisabled("Optional suffix: screenshot_NNN_<suffix>.png");
}

void ControlPanel::beginFrame ()
{
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ControlPanel::drawDebugOverlay (const DebugInfo &dbg)
{
  if (settings.debugLevel == DebugLevel::OFF) { return; }

  const float    PAD = 10.0f;
  ImGuiWindowFlags flags =
    ImGuiWindowFlags_NoDecoration   | ImGuiWindowFlags_AlwaysAutoResize |
    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
    ImGuiWindowFlags_NoNav           | ImGuiWindowFlags_NoMove;

  ImGui::SetNextWindowPos(ImVec2(PAD, PAD), ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin("##debug", nullptr, flags);

  if (fontLarge) { ImGui::PushFont(fontLarge); }
  ImGui::Text("FPS  %.1f   (%.2f ms)", dbg.fps, 1000.0f / dbg.fps);
  if (fontLarge) { ImGui::PopFont(); }

  if (settings.debugLevel == DebugLevel::VERBOSE)
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
  if (!settings.showPanel) { return; }

  // Settings panel — anchored to the upper-right on first appearance
  ImGuiIO &io = ImGui::GetIO();
  const float PAD_PANEL = 10.0f;
  const float WIN_W     = 370.0f;
  ImGui::SetNextWindowPos (ImVec2(io.DisplaySize.x - WIN_W - PAD_PANEL, PAD_PANEL), ImGuiCond_Appearing);
  ImGui::SetNextWindowSize(ImVec2(WIN_W, io.DisplaySize.y - PAD_PANEL * 2.0f), ImGuiCond_Appearing);
  ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f, 100.0f),
                                      ImVec2(FLT_MAX, io.DisplaySize.y - PAD_PANEL * 2.0f));
  ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoScrollbar     |
                                    ImGuiWindowFlags_NoScrollWithMouse);

  // Tab bar — rendered before child so it stays fixed at the top
  static int activeTab = 0;
  if (ImGui::BeginTabBar("##tabs"))
  {
    if (ImGui::BeginTabItem("Scene"))     { activeTab = 0; ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Rendering")) { activeTab = 1; ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("Camera"))    { activeTab = 2; ImGui::EndTabItem(); }
    if (ImGui::BeginTabItem("UI"))        { activeTab = 3; ImGui::EndTabItem(); }
    ImGui::EndTabBar();
  }

  // Scrollable content — fills remaining window height
  ImGui::BeginChild("##tabContent", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
  switch (activeTab)
  {
    case 0:
      drawSurfaceSection();
      drawMaterialSection();
      drawSunSection();
      drawPointLightSection();
      drawFloorSection();
      drawSkySection();
      break;
    case 1: drawRenderingSection();                      break;
    case 2: drawCameraSection(); drawAnimationSection(); break;
    case 3:
      drawImGuiStyleSection();
      drawImGuiColorsSection();
      drawImGuiInputSection();
      drawImGuiBehaviorSection();
      break;
  }
  ImGui::EndChild();

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
