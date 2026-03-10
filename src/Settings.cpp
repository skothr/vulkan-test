#include "Settings.hpp"

// ============================================================

Settings::Settings() { initRegistry(); }

// ============================================================
// Register all settings with metadata for auto ImGui rendering.
// Registration order determines rendering order within each group.
// ============================================================

void Settings::initRegistry()
{
  using W = cfg::Widget;

  // Visibility predicates
  auto isSphere = [this] { return surfaceType == SurfaceType::SPHERE; };
  auto isCube   = [this] { return surfaceType == SurfaceType::CUBE; };
  auto is4D     = [this] { return surfaceType == SurfaceType::SURFACE_4D; };
  auto sunOn    = [this] { return sunEnabled; };
  auto pointOn  = [this] { return pointEnabled; };

  // ── Object (Surface + Material) ──────────────────────────────────────────

  registry.addEnum("surfaceType", &surfaceType, SurfaceType::SPHERE)
    .widget(W::RADIO)
    .options({ "Sphere", "Cube", "4D Surface" })
    .group("Object");

  registry.add("sphereRadius", &sphereRadius, 0.70f).label("Radius").group("Object").range(0.1f, 1.5f).visibleWhen(isSphere);
  registry.add("cubeSize", &cubeSize, 0.70f).label("Size").group("Object").range(0.1f, 1.5f).visibleWhen(isCube);
  registry.add("sphereHeight", &sphereHeight, 0.00f).label("Height").group("Object").range(-0.5f, 3.0f);

  registry.add("ior", &ior, 1.50f).label("IOR").group("Object").range(1.0f, 3.0f);
  registry.add("tintAmount", &tintAmount, 0.25f).label("Tint").group("Object").range(0.0f, 1.0f);
  registry.addColor3("tintColor", &tintR, 0.80f, 0.90f, 1.00f).label("Tint Color").group("Object");

  // 4D Blobbies
  registry.add("blobbiesThreshold", &blobbiesThreshold, 0.7f)
    .label("Threshold")
    .group("Object")
    .range(0.01f, 2.0f)
    .format("%.3f")
    .visibleWhen(is4D);

  registry.addVec3("blobsCenter", &blobsCenterX, 0.0f, 0.0f, 0.0f)
    .label("Cluster Center")
    .group("Object")
    .range(-2.0f, 2.0f)
    .visibleWhen(is4D);
  registry.add("blobsDist", &blobsDist, 1.5f).label("Distance").group("Object").range(0.0f, 4.0f).visibleWhen(is4D);

  registry.addSeparator("Object", is4D);
  registry.addLabel("Blobby 1", "Object", is4D);
  registry.add("blob1Mu", &blob1Mu, 1.0f).label("Amplitude").group("Object").range(0.1f, 4.0f).visibleWhen(is4D);
  registry.add("blob1Sigma", &blob1Sigma, 0.55f).label("Sigma 3D").group("Object").range(0.05f, 2.0f).format("%.3f").visibleWhen(is4D);
  registry.add("blob1W", &blob1W, 0.0f).label("W (4D amp)").group("Object").range(-2.0f, 2.0f).visibleWhen(is4D);
  registry.add("blob1SigmaW", &blob1SigmaW, 0.5f).label("Sigma 4D").group("Object").range(0.05f, 2.0f).format("%.3f").visibleWhen(is4D);

  registry.addSeparator("Object", is4D);
  registry.addLabel("Blobby 2", "Object", is4D);
  registry.add("blob2Mu", &blob2Mu, 1.0f).label("Amplitude").group("Object").range(0.1f, 4.0f).visibleWhen(is4D);
  registry.add("blob2Sigma", &blob2Sigma, 0.55f).label("Sigma 3D").group("Object").range(0.05f, 2.0f).format("%.3f").visibleWhen(is4D);
  registry.add("blob2W", &blob2W, 0.0f).label("W (4D amp)").group("Object").range(-2.0f, 2.0f).visibleWhen(is4D);
  registry.add("blob2SigmaW", &blob2SigmaW, 0.5f).label("Sigma 4D").group("Object").range(0.05f, 2.0f).format("%.3f").visibleWhen(is4D);

  // ── Sun ──────────────────────────────────────────────────────────────────

  registry.add("sunEnabled", &sunEnabled, false).label("Enabled").group("Sun");
  registry.addAngle("sunAzimuth", &sunAzimuth, 0.588f).label("Azimuth").group("Sun").range(-3.14159f, 3.14159f).visibleWhen(sunOn);
  registry.addAngle("sunElevation", &sunElevation, 0.770f).label("Elevation").group("Sun").range(0.0f, 1.5707f).visibleWhen(sunOn);
  registry.add("sunIntensity", &sunIntensity, 1.000f).label("Intensity").group("Sun").range(0.0f, 5.0f).visibleWhen(sunOn);
  registry.add("sunShadows", &sunShadows, true).label("Shadows").group("Sun").visibleWhen(sunOn);
  registry.add("sunCaustics", &sunCaustics, true).label("Caustics").group("Sun").sameLine().visibleWhen(sunOn);
  registry.addSeparator("Sun", sunOn);
  registry.addAngle("sunConeHalf", &sunConeHalf, 0.080f).label("Disk Size").group("Sun").range(0.01f, 0.40f).visibleWhen(sunOn);
  registry.add("sunDiskExp", &sunDiskExp, 256.00f).label("Disk Exp").group("Sun").range(8.0f, 512.0f).format("%.0f").visibleWhen(sunOn);
  registry.addColor3("sunDiskColor", &sunDiskR, 2.0f, 1.9f, 1.5f).label("Disk Color").group("Sun").visibleWhen(sunOn);
  registry.add("sunCoronaExp", &sunCoronaExp, 6.00f).label("Corona Exp").group("Sun").range(1.0f, 20.0f).format("%.1f").visibleWhen(sunOn);
  registry.addColor3("sunCoronaColor", &sunCoronaR, 0.4f, 0.3f, 0.1f).label("Corona Color").group("Sun").visibleWhen(sunOn);

  // ── Point Light ──────────────────────────────────────────────────────────

  registry.add("pointEnabled", &pointEnabled, true).label("Enabled").group("Point Light");
  registry.addVec3("pointLightPos", &pointLightX, 1.5f, 0.0f, 1.5f)
    .label("Position")
    .group("Point Light")
    .rangeX(-5.0f, 5.0f)
    .rangeY(-5.0f, 5.0f)
    .rangeZ(-1.0f, 5.0f)
    .visibleWhen(pointOn);
  registry.add("pointLightRadius", &pointLightRadius, 0.3f).label("Radius").group("Point Light").range(0.0f, 2.0f).visibleWhen(pointOn);
  registry.addColor3("pointLightColor", &pointLightR, 1.0f, 0.85f, 0.6f).label("Color").group("Point Light").visibleWhen(pointOn);
  registry.add("pointLightIntensity", &pointLightIntensity, 3.0f)
    .label("Intensity")
    .group("Point Light")
    .range(0.0f, 20.0f)
    .visibleWhen(pointOn);
  registry.add("pointShadows", &pointShadows, true).label("Shadows").group("Point Light").visibleWhen(pointOn);
  registry.add("pointCaustics", &pointCaustics, true).label("Caustics").group("Point Light").sameLine().visibleWhen(pointOn);

  // ── Environment (Floor + Sky) ─────────────────────────────────────────────

  registry.add("floorScale", &floorScale, 2.0f).label("Scale").group("Environment").range(0.1f, 8.0f);
  registry.add("floorZ", &floorZ, -1.2f).label("Z").group("Environment").range(-3.0f, 0.0f);
  registry.addColor3("floorLightColor", &floorLightR, 0.85f, 0.85f, 0.85f).label("Light Color").group("Environment");
  registry.addColor3("floorDarkColor", &floorDarkR, 0.12f, 0.12f, 0.12f).label("Dark Color").group("Environment");
  registry.addColor3("skyHorizon", &skyHorizonR, 0.45f, 0.55f, 0.85f).label("Horizon").group("Environment");
  registry.addColor3("skyZenith", &skyZenithR, 0.08f, 0.15f, 0.45f).label("Zenith").group("Environment");

  // ── Quality ───────────────────────────────────────────────────────────────

  registry.add("shadowsEnabled", &shadowsEnabled, true).label("Shadows").group("Quality");
  registry.add("causticsEnabled", &causticsEnabled, true).label("Caustics").group("Quality").sameLine();
  registry.add("ambient", &ambient, 0.02f).label("Ambient").group("Quality").range(0.0f, 0.5f);
  registry.add("maxDepth", &maxDepth, 4).label("Max Depth").group("Quality").range(1, 8);

  // GPU cost estimate
  registry.addCallback("Quality", [this]() {
    bool   isBlobby  = (surfaceType == SurfaceType::SURFACE_4D);
    float  blobMult  = isBlobby ? (blobMarchSteps / 32.0f) : 1.0f;
    float  raysPerPx = (nCaustics * 2.0f + shadowSamples) * blobMult;
    ImVec4 costColor = (raysPerPx < 500.0f)  ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                     : (raysPerPx < 2000.0f) ? ImVec4(1.0f, 0.85f, 0.1f, 1.0f)
                                             : ImVec4(1.0f, 0.3f, 0.2f, 1.0f);
    ImGui::Separator();
    ImGui::TextColored(costColor, "GPU cost: ~%.0f ray-units/px", raysPerPx);
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("Rough relative cost per floor pixel.\n"
                        "Green < 500 | Yellow < 2000 | Red = TDR risk\n"
                        "= (caustics*2 + shadows) * blobMarchSteps/32\n"
                        "Cost is multiplicative -- lower one knob to compensate.");
    }
  });

  // ── Caustics ──────────────────────────────────────────────────────────────

  registry.add("nCaustics", &nCaustics, 64)
    .label("Samples")
    .group("Caustics")
    .range(1, 256)
    .tooltip("Backward caustic rays per floor pixel.\nMore = less noise, higher cost.\nCtrl+click to type a value above 256.");
  registry.add("causticDiskScale", &causticDiskScale, 1.05f)
    .label("Disk Scale")
    .group("Caustics")
    .range(0.01f, 3.0f)
    .tooltip("Caustic sampling disk radius as a\nmultiple of the glass object radius.\n1.0 = tight fit, >1 smooths edges.");
  registry.add("causticFalloff", &causticFalloff, 4.00f)
    .label("Miss Falloff")
    .group("Caustics")
    .range(0.5f, 16.0f)
    .format("%.1f")
    .tooltip("Gaussian attenuation sharpness for\nrays that narrowly miss the light.\nHigher = harder caustic edges.");
  registry.add("causticBlendRadius", &causticBlendRadius, 0.40f)
    .label("Blend Radius")
    .group("Caustics")
    .range(0.0f, 2.0f)
    .format("%.3f")
    .tooltip("World-space cell size for caustic sample blending.\nThree octaves (r, r/2, r/4) fill in detail at all scales.\n0 = per-pixel "
             "white noise (no blending).");
  registry.add("causticDitherAmt", &causticDitherAmt, 0.20f)
    .label("Dither")
    .group("Caustics")
    .range(0.0f, 1.0f)
    .tooltip(
      "Mix per-pixel noise into the blended samples to\nbreak up cell-boundary seams. 0 = pure blend,\n1 = pure per-pixel white noise.");

  // ── Soft Shadows ──────────────────────────────────────────────────────────

  registry.add("shadowSamples", &shadowSamples, 32)
    .label("Samples")
    .group("Soft Shadows")
    .range(1, 128)
    .tooltip("Shadow rays per floor pixel.\n1 = hard shadow, more = soft penumbra.\nCtrl+click to type a value above 16.");
  registry.add("shadowSoftness", &shadowSoftness, 0.50f)
    .label("Softness")
    .group("Soft Shadows")
    .range(0.0f, 4.0f)
    .tooltip("Penumbra disk radius as a multiple\nof the point light radius.");

  // ── 4D Surface ────────────────────────────────────────────────────────────

  registry.add("blobMarchSteps", &blobMarchSteps, 128)
    .label("March Steps")
    .group("4D Surface")
    .range(8, 128)
    .tooltip(
      "Ray-march steps through the 4D\nGaussian blobby AABB.\nMore = sharper surface, higher cost.\nCtrl+click to type a value above 128.");

  // ── View ─────────────────────────────────────────────────────────────────

  registry.add("fov", &fov, 45.0f).label("FOV").group("View").range(10.0f, 120.0f).format("%.1f\xc2\xb0");
  registry.add("sensitivity", &sensitivity, 0.005f).label("Sensitivity").group("View").range(0.001f, 0.02f).format("%.3f");
  registry.add("zoomSpeed", &zoomSpeed, 0.1f).label("Zoom Speed").group("View").range(0.01f, 0.5f);

  // ── Animation ────────────────────────────────────────────────────────────

  registry.add("autoRotate", &autoRotate, true).label("Auto Rotate").group("Animation");
  registry.add("rotSpeed", &rotSpeed, 1.0f).label("Rot Speed").group("Animation").range(0.0f, 5.0f);
  registry.addVec3("rotAxis", &rotAxisX, 0.5f, 1.0f, 0.0f).label("Rot Axis").group("Animation").range(-1.0f, 1.0f).horizontal();

  // ── Screenshot ───────────────────────────────────────────────────────────

  registry.addLabel("Press F12 to capture -- saved to ./images/", "Screenshot");
  registry.add("screenshotSuffix", &screenshotSuffix, std::string{})
    .label("Suffix")
    .group("Screenshot")
    .tooltip("Optional suffix: screenshot_NNN_<suffix>.png");
}

// ============================================================
// Pack settings into the shader-compatible UBO layout.
// Field order MUST match the GLSL ParamsBlock exactly.
// ============================================================

ParamsUBO Settings::toParamsUBO() const
{
  // Blobby centers derived from shared center + separation along X
  float b1x = blobsCenterX - blobsDist * 0.5f;
  float b2x = blobsCenterX + blobsDist * 0.5f;

  return {
    // Material
    ior,
    tintAmount,
    maxDepth,
    tintR,
    tintG,
    tintB,
    // Sun
    sunEnabled ? 1 : 0,
    (sunShadows && shadowsEnabled) ? 1 : 0,
    (sunCaustics && causticsEnabled) ? 1 : 0,
    sunAzimuth,
    sunElevation,
    sunIntensity,
    sunConeHalf,
    sunDiskExp,
    sunCoronaExp,
    sunDiskR,
    sunDiskG,
    sunDiskB,
    sunCoronaR,
    sunCoronaG,
    sunCoronaB,
    // Point light
    pointEnabled ? 1 : 0,
    (pointShadows && shadowsEnabled) ? 1 : 0,
    (pointCaustics && causticsEnabled) ? 1 : 0,
    pointLightX,
    pointLightY,
    pointLightZ,
    pointLightRadius,
    pointLightR,
    pointLightG,
    pointLightB,
    pointLightIntensity,
    // Surface
    (int)surfaceType,
    sphereRadius,
    sphereHeight,
    cubeSize,
    // Rendering
    nCaustics,
    ambient,
    floorZ,
    // Floor
    floorScale,
    floorLightR,
    floorLightG,
    floorLightB,
    floorDarkR,
    floorDarkG,
    floorDarkB,
    // Sky
    skyHorizonR,
    skyHorizonG,
    skyHorizonB,
    skyZenithR,
    skyZenithG,
    skyZenithB,
    // Blobbies
    b1x,
    blobsCenterY,
    blobsCenterZ,
    blob1W,
    blob1Mu,
    blob1Sigma,
    blob1SigmaW,
    b2x,
    blobsCenterY,
    blobsCenterZ,
    blob2W,
    blob2Mu,
    blob2Sigma,
    blob2SigmaW,
    blobbiesThreshold,
    // Shadows
    shadowSamples,
    shadowSoftness,
    // Caustics
    causticDiskScale,
    causticFalloff,
    causticBlendRadius,
    causticDitherAmt,
    // Blobby quality
    blobMarchSteps,
  };
}
