#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 prd;
layout(location = 1) rayPayloadEXT   vec4 causticPrd;   // outgoing shadow / caustic rays

layout(set = 0, binding = 0) uniform accelerationStructureEXT tlas;

layout(set = 0, binding = 3) uniform ParamsBlock
{
  // Material
  float ior,          tintAmount;   int   maxDepth;
  float tintR,        tintG,        tintB;
  // Sun
  int   sunEnabled,   sunShadows,   sunCaustics;
  float sunAzimuth,   sunElevation, sunIntensity, sunConeHalf;
  float sunDiskExp,   sunCoronaExp;
  float sunDiskR,     sunDiskG,     sunDiskB;
  float sunCoronaR,   sunCoronaG,   sunCoronaB;
  // Point light
  int   pointEnabled, pointShadows, pointCaustics;
  float pointLightX,  pointLightY,  pointLightZ,  pointLightRadius;
  float pointLightR,  pointLightG,  pointLightB,  pointLightIntensity;
  // Surface
  int   surfaceType;
  float sphereRadius, sphereHeight, cubeSize;
  // Rendering
  int   nCaustics;
  float ambient,      floorZ;
  // Floor
  float floorScale;
  float floorLightR,  floorLightG,  floorLightB;
  float floorDarkR,   floorDarkG,   floorDarkB;
  // Sky
  float skyHorizonR,  skyHorizonG,  skyHorizonB;
  float skyZenithR,   skyZenithG,   skyZenithB;
  // 4D Gaussian Blobbies
  float blob1X,  blob1Y,  blob1Z,  blob1W;
  float blob1Mu, blob1Sigma,  blob1SigmaW;
  float blob2X,  blob2Y,  blob2Z,  blob2W;
  float blob2Mu, blob2Sigma,  blob2SigmaW;
  float blobbiesThreshold;
  // Shadows
  int   shadowSamples;
  float shadowSoftness;
  // Caustics
  float causticDiskScale;
  float causticFalloff;
  float causticBlendRadius;
  float causticDitherAmt;
  // Blobby quality
  int   blobMarchSteps;
} pc;

// Glass object center in world space
#define GLASS_CENTER vec3(0.0, 0.0, pc.sphereHeight)

// Effective cross-section radius for caustic disk sampling
float objectRadius ()
{
  if (pc.surfaceType == 0) { return pc.sphereRadius; }
  if (pc.surfaceType == 1) { return pc.cubeSize; }
  // Blobby: 3σ covers the Gaussian field support; add half the inter-blob separation
  // so the sampling disk encloses both blobs regardless of orientation.
  float sigma   = max(pc.blob1Sigma, pc.blob2Sigma);
  float halfSep = 0.5 * length(vec3(pc.blob2X - pc.blob1X,
                                    pc.blob2Y - pc.blob1Y,
                                    pc.blob2Z - pc.blob1Z));
  return sigma * 3.0 + halfSep;
}

// 2D PCG hash — maps a uint pair to an independent uniform uint pair.
uvec2 pcg2 (uvec2 v)
{
  v = v * 1664525u + 1013904223u;
  v.x += v.y * 1664525u;
  v.y += v.x * 1013904223u;
  v ^= v >> 16u;
  return v;
}

// Returns sample i on the unit disk for the given world-space cell.
// (ka, kb) are loop-specific constants that keep sun/shadow/point samples independent.
vec2 cellDiskSample (uvec2 cell, int i, uint ka, uint kb)
{
  uvec2 seed = uvec2(cell.x * 2891336453u ^ cell.y * 1664525u,
                     cell.x * 1013904223u ^ cell.y * 2891336453u);
  uvec2 h   = pcg2(seed ^ uvec2(uint(i) * ka, uint(i) * kb + 1u));
  float ang = float(h.x) * (6.28318 / float(0xFFFFFFFFu));
  float rad = sqrt(float(h.y) / float(0xFFFFFFFFu));
  return vec2(cos(ang), sin(ang)) * rad;
}

// Bilinearly blends disk sample i across the 4 surrounding world-space grid cells
// at the given spatial scale.  (Internal helper — call blendedDiskSample instead.)
vec2 bilinearCells (vec2 wpos, float scale, int i, uint ka, uint kb)
{
  vec2  uv  = wpos / scale;
  uvec2 ci  = uvec2(ivec2(floor(uv)));
  vec2  fr  = fract(uv);
  vec2  sm  = fr * fr * (3.0 - 2.0 * fr);   // smoothstep — C¹ continuity
  vec2  s00 = cellDiskSample(ci,                 i, ka, kb);
  vec2  s10 = cellDiskSample(ci + uvec2(1u, 0u), i, ka, kb);
  vec2  s01 = cellDiskSample(ci + uvec2(0u, 1u), i, ka, kb);
  vec2  s11 = cellDiskSample(ci + uvec2(1u, 1u), i, ka, kb);
  return mix(mix(s00, s10, sm.x), mix(s01, s11, sm.x), sm.y);
}

// Three-octave bilinear blend with per-pixel dither.
//
// Single-octave bilinear blending creates flat cell interiors (no variation within
// a cell) and visible seams at cell boundaries where adjacent cells' sample sets
// differ sharply.  Two fixes:
//
//  • Three octaves (scales: r, r/2, r/4, weights 4:2:1): each finer octave fills
//    in variation WITHIN the cells of the coarser octaves, eliminating flat regions
//    and breaking up the blotchy pattern at all radius values.
//
//  • Per-pixel dither (pc.causticDitherAmt): mixes in a small amount of fully
//    independent per-pixel noise.  This decorrelates immediately adjacent pixels
//    at cell boundaries, turning the sharp seam into smooth grain the eye reads
//    as noise rather than structure.
//
// blendRadius = 0: dither=1 implicitly (falls back to pure per-pixel white noise).
vec2 blendedDiskSample (vec2 wpos, float blendRadius, int i, uint ka, uint kb)
{
  // Per-pixel white-noise sample (used for fallback and dithering)
  uvec2 pseed = uvec2(gl_LaunchIDEXT.x * ka, gl_LaunchIDEXT.y * kb);
  uvec2 ph    = pcg2(pseed ^ uvec2(uint(i) * ka, uint(i) * kb + 1u));
  float pang  = float(ph.x) * (6.28318 / float(0xFFFFFFFFu));
  float prad  = sqrt(float(ph.y) / float(0xFFFFFFFFu));
  vec2  pixNoise = vec2(cos(pang), sin(pang)) * prad;

  if (blendRadius <= 0.0) { return pixNoise; }

  // Three octaves: each finer scale fills detail inside larger cells
  vec2 s1 = bilinearCells(wpos, blendRadius,        i, ka,               kb            );
  vec2 s2 = bilinearCells(wpos, blendRadius * 0.5,  i, ka ^ 0xCAFEBABEu, kb ^ 0xDEADBEEFu);
  vec2 s3 = bilinearCells(wpos, blendRadius * 0.25, i, ka ^ 0xDEADF00Du, kb ^ 0xC0FFEE00u);
  vec2 blended = (s1 * 4.0 + s2 * 2.0 + s3) * (1.0 / 7.0);

  // Scale blend strength with radius so small values stay noise-like and the
  // transition is gradual across the slider range rather than snapping instantly.
  // smoothstep ramps from 0→1 over [0, 0.5] world units — at radius=0.09 this
  // gives ~9% blend weight, not 80%, preventing the abrupt snap.
  float blendFactor = smoothstep(0.0, 0.5, blendRadius);

  // Dither mixes per-pixel noise into the blended result to break up seams.
  // Both the blend and dither scale together so dither stays proportional.
  return mix(pixNoise, blended, blendFactor * (1.0 - pc.causticDitherAmt));
}

void main ()
{
  vec3 orig = gl_WorldRayOriginEXT;
  vec3 dir  = normalize(gl_WorldRayDirectionEXT);

  vec3 sunDir = normalize(vec3(cos(pc.sunElevation) * cos(pc.sunAzimuth),
                               cos(pc.sunElevation) * sin(pc.sunAzimuth),
                               sin(pc.sunElevation)));

  // ── Compute intersection distances for depth ordering ───────────────────
  float tLight = -1.0;
  if (pc.pointEnabled != 0)
  {
    vec3  ptPos = vec3(pc.pointLightX, pc.pointLightY, pc.pointLightZ);
    vec3  oc    = orig - ptPos;
    float b     = dot(oc, dir);
    float c     = dot(oc, oc) - pc.pointLightRadius * pc.pointLightRadius;
    float disc  = b*b - c;
    if (disc >= 0.0)
    {
      float t = -b - sqrt(disc);
      if (t > 0.001) { tLight = t; }
    }
  }

  float tFloor = -1.0;
  if (dir.z < -0.0001)
  {
    float t = (pc.floorZ - orig.z) / dir.z;
    if (t > 0.001) { tFloor = t; }
  }

  // ── Point light sphere (emissive) — only if closer than floor ───────────
  if (tLight > 0.0 && (tFloor < 0.0 || tLight < tFloor))
  {
    prd.rgb = vec3(pc.pointLightR, pc.pointLightG, pc.pointLightB) * pc.pointLightIntensity;
    return;
  }

  // ── Ground plane (checkerboard in XY) ───────────────────────────────────
  if (tFloor > 0.0)
  {
    vec3  p        = orig + tFloor * dir;
    int   cx       = int(floor(p.x * pc.floorScale));
    int   cy       = int(floor(p.y * pc.floorScale));
    bool  check    = ((cx + cy) & 1) == 0;
    vec3  floorCol = check ? vec3(pc.floorLightR, pc.floorLightG, pc.floorLightB)
                           : vec3(pc.floorDarkR,  pc.floorDarkG,  pc.floorDarkB);
    vec3  lighting = vec3(pc.ambient);

    // Glass disk geometry — shared by sun and point-light caustics
    vec3  toGlass     = GLASS_CENTER - p;
    float distToGlass = length(toGlass);
    vec3  dg          = normalize(toGlass);
    vec3  b1 = (abs(dg.x) < 0.9) ? normalize(cross(dg, vec3(1.0, 0.0, 0.0)))
                                  : normalize(cross(dg, vec3(0.0, 1.0, 0.0)));
    vec3  b2 = cross(dg, b1);

    float objR       = objectRadius();
    float omegaGlass = 3.14159 * objR * objR / (distToGlass * distToGlass);

    // Each caustic/shadow loop uses blendedDiskSample with distinct (ka,kb) constants
    // to keep their sample sequences independent.

    // ── Directional sun ──────────────────────────────────────────────────
    if (pc.sunEnabled != 0)
    {
      float NdotL = max(0.0, sunDir.z);   // floor normal = +Z

      // Direct shadow
      vec3 directTransmit = vec3(1.0);
      if (pc.sunShadows != 0)
      {
        causticPrd = vec4(1.0, 1.0, 1.0, 2.0);
        traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF,
                    1, 1, 1,
                    p + vec3(0.0, 0.0, 0.002), 0.001, sunDir, 10000.0,
                    1);
        directTransmit = causticPrd.rgb;
      }

      // Caustics: backward rays toward glass disk, check exit toward sun.
      // Samples are drawn from a disk 1.5× the glass radius; samples outside the
      // glass naturally return 0, creating a smooth spatial falloff at caustic edges.
      // Concentration is scaled by 1.5² to maintain correct energy.
      vec3 causticLight = vec3(0.0);
      if (pc.sunCaustics != 0)
      {
        float SAMP = pc.causticDiskScale;
        vec3 causticAccum = vec3(0.0);
        for (int i = 0; i < pc.nCaustics; i++)
        {
          vec2  s      = blendedDiskSample(p.xy, pc.causticBlendRadius, i, 2891336453u, 747796405u) * objR * SAMP;
          vec3  diskPt = GLASS_CENTER + b1 * s.x + b2 * s.y;
          vec3  sdir   = normalize(diskPt - p);
          float sdist  = distToGlass + objR * SAMP + 1.0;

          causticPrd = vec4(1.0, 1.0, 1.0, 2.0);
          traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF,
                      1, 1, 1,
                      p + vec3(0.0, 0.0, 0.002), 0.001, sdir, sdist,
                      1);
          causticAccum += causticPrd.rgb;
        }
        causticAccum /= float(pc.nCaustics);

        float concentration = omegaGlass * SAMP * SAMP / (3.14159 * pc.sunConeHalf * pc.sunConeHalf);
        causticLight = causticAccum * concentration;
      }

      lighting += NdotL * (directTransmit + causticLight) * pc.sunIntensity;
    }

    // ── Point light ──────────────────────────────────────────────────────
    if (pc.pointEnabled != 0)
    {
      vec3  ptPos    = vec3(pc.pointLightX, pc.pointLightY, pc.pointLightZ);
      vec3  toLight  = ptPos - p;
      float dist     = length(toLight);
      vec3  lightDir = toLight / dist;
      float NdotL_pt = max(0.0, lightDir.z);

      float r2      = pc.pointLightRadius * pc.pointLightRadius;
      float falloff = 1.0 / max(dist * dist, r2);
      vec3  ptColor = vec3(pc.pointLightR, pc.pointLightG, pc.pointLightB);

      // Soft shadow: Vogel-disk samples over the light sphere give a smooth
      // penumbra and hide the blobby surface's ragged intersection boundary.
      float shadow = 1.0;
      if (pc.pointShadows != 0)
      {
        int   sN    = max(pc.shadowSamples, 1);
        float sRmax = pc.pointLightRadius * pc.shadowSoftness;
        vec3  sb1   = (abs(lightDir.x) < 0.9) ? normalize(cross(lightDir, vec3(1,0,0)))
                                               : normalize(cross(lightDir, vec3(0,1,0)));
        vec3  sb2   = cross(lightDir, sb1);
        float shadowAccum = 0.0;
        for (int i = 0; i < sN; i++)
        {
          vec2  ss      = blendedDiskSample(p.xy, pc.causticBlendRadius, i, 1664525u, 1013904223u) * sRmax;
          vec3  lSample = ptPos + sb1 * ss.x + sb2 * ss.y;
          vec3  lDir    = normalize(lSample - p);
          float lDist   = length(lSample - p);
          causticPrd = vec4(1.0, 1.0, 1.0, -1.0);
          traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF,
                      1, 1, 1,
                      p + vec3(0.0, 0.0, 0.002), 0.001, lDir, lDist - 0.01,
                      1);
          shadowAccum += causticPrd.r;
        }
        shadow = shadowAccum / float(sN);
      }

      lighting += ptColor * NdotL_pt * falloff * pc.pointLightIntensity * shadow;

      // Caustics: backward rays toward glass disk, check exit toward point light sphere.
      // Mode a = 52.0 → point-light caustic (a >= 50) with depth 2.
      if (pc.pointCaustics != 0)
      {
        float SAMP = pc.causticDiskScale;
        vec3 causticAccumPt = vec3(0.0);
        for (int i = 0; i < pc.nCaustics; i++)
        {
          vec2  s      = blendedDiskSample(p.xy, pc.causticBlendRadius, i, 1013904223u, 2891336453u) * objR * SAMP;
          vec3  diskPt = GLASS_CENTER + b1 * s.x + b2 * s.y;
          vec3  sdir   = normalize(diskPt - p);
          float sdist  = distToGlass + objR * SAMP + 1.0;

          causticPrd = vec4(1.0, 1.0, 1.0, 52.0);
          traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF,
                      1, 1, 1,
                      p + vec3(0.0, 0.0, 0.002), 0.001, sdir, sdist,
                      1);
          causticAccumPt += causticPrd.rgb;
        }
        causticAccumPt /= float(pc.nCaustics);

        float omegaPtLight = 3.14159 * r2 / max(dist * dist, 1e-6);
        float concentPt    = omegaGlass * SAMP * SAMP / max(omegaPtLight, 1e-6);
        lighting += ptColor * NdotL_pt * causticAccumPt * concentPt
                  * pc.pointLightIntensity * falloff;
      }
    }

    prd.rgb = floorCol * lighting;
    return;
  }

  // ── Sky gradient (Z is up) ───────────────────────────────────────────────
  float sky = max(0.0, dir.z);
  prd.rgb = mix(vec3(pc.skyHorizonR, pc.skyHorizonG, pc.skyHorizonB),
                vec3(pc.skyZenithR,  pc.skyZenithG,  pc.skyZenithB),
                sky);

  // ── Sun disk (only when enabled) ─────────────────────────────────────────
  if (pc.sunEnabled != 0)
  {
    float sun = max(0.0, dot(dir, sunDir));
    prd.rgb += pow(sun, pc.sunDiskExp)   * vec3(pc.sunDiskR,   pc.sunDiskG,   pc.sunDiskB)   * pc.sunIntensity;
    prd.rgb += pow(sun, pc.sunCoronaExp) * vec3(pc.sunCoronaR, pc.sunCoronaG, pc.sunCoronaB) * pc.sunIntensity;
  }
}
