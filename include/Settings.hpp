#pragma once

#include <string>

enum class SurfaceType { SPHERE = 0, CUBE = 1, SURFACE_4D = 2 };
enum class DebugLevel { OFF = 0, FPS = 1, VERBOSE = 2 };

// ── Params UBO ─────────────────────────────────────────────────────────────
// Sent to shaders via a uniform buffer at (set=0, binding=3).
// Field order and types MUST match the GLSL ParamsBlock in every shader exactly.
// All fields are 4-byte scalars — std140 packs them at consecutive 4-byte offsets,
// matching a plain C++ struct with no internal padding on any standard platform.
struct ParamsUBO
{
  // Material
  float ior, tintAmount;
  int   maxDepth;
  float tintR, tintG, tintB;
  // Sun
  int   sunEnabled, sunShadows, sunCaustics;
  float sunAzimuth, sunElevation, sunIntensity, sunConeHalf;
  float sunDiskExp, sunCoronaExp;
  float sunDiskR, sunDiskG, sunDiskB;
  float sunCoronaR, sunCoronaG, sunCoronaB;
  // Point light
  int   pointEnabled, pointShadows, pointCaustics;
  float pointLightX, pointLightY, pointLightZ, pointLightRadius;
  float pointLightR, pointLightG, pointLightB, pointLightIntensity;
  // Surface
  int   surfaceType;
  float sphereRadius, sphereHeight, cubeSize;
  // Rendering
  int   nCaustics;
  float ambient, floorZ;
  // Floor appearance
  float floorScale;
  float floorLightR, floorLightG, floorLightB;
  float floorDarkR, floorDarkG, floorDarkB;
  // Sky
  float skyHorizonR, skyHorizonG, skyHorizonB;
  float skyZenithR, skyZenithG, skyZenithB;
  // 4D Gaussian Blobbies (surfaceType == 2)
  // Positions are computed from center + distance; sigmas are independent for 3D and W.
  float blob1X, blob1Y, blob1Z, blob1W;
  float blob1Mu, blob1Sigma, blob1SigmaW;
  float blob2X, blob2Y, blob2Z, blob2W;
  float blob2Mu, blob2Sigma, blob2SigmaW;
  float blobbiesThreshold;
  // Shadows
  int   shadowSamples;  // soft-shadow ray count (1 = hard shadow)
  float shadowSoftness; // disk radius multiplier on pointLightRadius
  // Caustics
  float causticDiskScale;   // sampling disk radius multiplier on objectRadius (1.0 = exact fit)
  float causticFalloff;     // Gaussian miss-falloff sharpness for point-light caustics (higher = harder edge)
  float causticBlendRadius; // world-space cell size for blended sampling (0 = per-pixel white noise)
  float causticDitherAmt;   // per-pixel noise mixed in to break up cell-boundary seams (0–1)
  // Blobby quality
  int blobMarchSteps; // ray-march step count for 4D Gaussian intersection
};

// ── Settings ───────────────────────────────────────────────────────────────
// All runtime-editable fields with sensible defaults.
struct Settings
{
  // ── Material ──────────────────────────────────────────────────────────────
  float ior        = 1.50f;
  float tintAmount = 0.25f;
  float tintR = 0.80f, tintG = 0.90f, tintB = 1.00f;

  // ── Sun ───────────────────────────────────────────────────────────────────
  bool  sunEnabled   = false;
  bool  sunShadows   = true;
  bool  sunCaustics  = true;
  float sunAzimuth   = 0.588f;
  float sunElevation = 0.770f;
  float sunIntensity = 1.000f;
  float sunConeHalf  = 0.080f;
  // Sun appearance (multiplied by sunIntensity in the shader)
  float sunDiskExp   = 256.00f;
  float sunCoronaExp = 6.00f;
  float sunDiskR = 2.0f, sunDiskG = 1.9f, sunDiskB = 1.5f;
  float sunCoronaR = 0.4f, sunCoronaG = 0.3f, sunCoronaB = 0.1f;

  // ── Point light ───────────────────────────────────────────────────────────
  bool  pointEnabled     = true;
  bool  pointShadows     = true;
  bool  pointCaustics    = true;
  float pointLightX      = 1.5f;
  float pointLightY      = 0.0f;
  float pointLightZ      = 1.5f;
  float pointLightRadius = 0.3f;
  float pointLightR = 1.0f, pointLightG = 0.85f, pointLightB = 0.6f;
  float pointLightIntensity = 3.0f;

  // ── Surface ───────────────────────────────────────────────────────────────
  SurfaceType surfaceType  = SurfaceType::SPHERE;
  float       sphereRadius = 0.70f;
  float       sphereHeight = 0.00f;
  float       cubeSize     = 0.70f;

  // ── 4D Gaussian Blobbies ──────────────────────────────────────────────────
  // The two blobby centers are placed symmetrically at ±dist/2 from (centerX,centerY,centerZ).
  // Each blobby has independent mu, sigma (3D), W (4D offset), and sigmaW (4D std dev).
  // Field equation: f_i(p) = mu_i * exp(-|p-c_i|^2/(2*sigma_i^2) - w_i^2/(2*sigmaW_i^2))
  float blobsCenterX = 0.0f, blobsCenterY = 0.0f, blobsCenterZ = 0.0f;
  float blobsDist = 1.5f; // center-to-center separation along X

  float blob1W      = 0.0f;
  float blob1Mu     = 1.0f;
  float blob1Sigma  = 0.55f; // 3D standard deviation
  float blob1SigmaW = 0.5f;  // W-dimension standard deviation

  float blob2W      = 0.0f;
  float blob2Mu     = 1.0f;
  float blob2Sigma  = 0.55f;
  float blob2SigmaW = 0.5f;

  float blobbiesThreshold = 0.7f;

  // ── Quality ───────────────────────────────────────────────────────────────
  bool  shadowsEnabled  = true;
  bool  causticsEnabled = true;
  int   maxDepth        = 4;
  int   nCaustics       = 64;
  float ambient         = 0.02f;

  // Caustics
  float causticDiskScale   = 1.05f; // 1.0 = tight fit; >1 smooths edges, <1 crops
  float causticFalloff     = 4.00f; // Gaussian sharpness for point-light miss attenuation
  float causticBlendRadius = 0.40f; // world-space cell size for blended sampling; 0 = per-pixel white noise
  float causticDitherAmt   = 0.20f; // per-pixel noise to break up cell-boundary seams (0 = none, 1 = full noise)
  // Soft shadows
  int   shadowSamples  = 32;
  float shadowSoftness = 0.50f;

  // 4D Surface
  int blobMarchSteps = 128; // ray-march steps through 4D Gaussian AABB

  // ── Floor ─────────────────────────────────────────────────────────────────
  float floorZ      = -1.2f;
  float floorScale  = 2.0f;
  float floorLightR = 0.85f, floorLightG = 0.85f, floorLightB = 0.85f;
  float floorDarkR = 0.12f, floorDarkG = 0.12f, floorDarkB = 0.12f;

  // ── Sky ───────────────────────────────────────────────────────────────────
  float skyHorizonR = 0.45f, skyHorizonG = 0.55f, skyHorizonB = 0.85f;
  float skyZenithR = 0.08f, skyZenithG = 0.15f, skyZenithB = 0.45f;

  // ── Animation ─────────────────────────────────────────────────────────────
  bool  autoRotate = true;
  float rotSpeed   = 1.0f;
  float rotAxisX = 0.5f, rotAxisY = 1.0f, rotAxisZ = 0.0f;

  // ── Camera ────────────────────────────────────────────────────────────────
  float fov         = 45.0f;
  float sensitivity = 0.005f;
  float zoomSpeed   = 0.1f;

  // ── UI ────────────────────────────────────────────────────────────────────
  bool showPanel = false;
  bool showDemo  = false;

  // ── Capture ───────────────────────────────────────────────────────────────
  std::string screenshotSuffix; // optional suffix appended to screenshot filename

  // ── Debug overlay  (0 = off, 1 = fps, 2 = verbose) ───────────────────────
  DebugLevel debugLevel = DebugLevel::FPS;

  ParamsUBO toParamsUBO() const
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
};
