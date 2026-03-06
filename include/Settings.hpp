#pragma once

// Push-constant block — must match layout(push_constant) in the shaders byte-for-byte.
struct PushConsts
{
  float ior;
  float tintAmount;
  int   maxDepth;
  float sunAzimuth;
  float sunElevation;
  float sunIntensity;
};

struct Settings
{
  // Material
  float ior        = 1.5f;
  float tintAmount = 0.25f;
  int   maxDepth   = 4;

  // Animation
  bool  autoRotate = true;
  float rotSpeed   = 1.0f;

  // Camera
  float sensitivity = 0.005f;
  float zoomSpeed   = 0.1f;

  // Lighting  (defaults reproduce the original hardcoded sun direction)
  float sunAzimuth   = 0.588f;  // ≈ atan2(0.4, 0.6)
  float sunElevation = 0.770f;  // ≈ atan(0.7 / length(0.6, 0.4))
  float sunIntensity = 1.0f;

  // Debug overlay  (0 = off, 1 = fps, 2 = verbose)
  int debugLevel = 0;

  PushConsts toPushConsts () const
  {
    return { ior, tintAmount, maxDepth, sunAzimuth, sunElevation, sunIntensity };
  }
};
