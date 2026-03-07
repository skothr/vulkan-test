#version 460
#extension GL_EXT_ray_tracing : require

// Shadow/caustic ray payload — .rgb = accumulated glass transmittance, .a = remaining bounces
layout(location = 1) rayPayloadInEXT vec4 causticPrd;

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
  int   shadowSamples;
  float shadowSoftness;
  float causticDiskScale;
  float causticFalloff;
  float causticBlendRadius;
  float causticDitherAmt;
  int   blobMarchSteps;
} pc;

void main ()
{
  // Occlusion-test mode (a < 0): ray missed all geometry → point is lit.  Keep rgb=1.
  if (causticPrd.a < 0.0) { return; }

  if (causticPrd.a >= 50.0)
  {
    // Point-light caustic mode: smooth Gaussian falloff based on how close the exit
    // ray passes to the light center (disc = r² - d_closest²).
    // Matches the smoothstep approach used for sun caustics to eliminate speckle.
    vec3  dir   = normalize(gl_WorldRayDirectionEXT);
    vec3  orig  = gl_WorldRayOriginEXT;
    vec3  ptPos = vec3(pc.pointLightX, pc.pointLightY, pc.pointLightZ);
    vec3  oc    = orig - ptPos;
    float b     = dot(oc, dir);
    float r2    = pc.pointLightRadius * pc.pointLightRadius;
    float disc  = b*b - (dot(oc, oc) - r2);

    if (-b <= 0.001)
    {
      // Light is behind the exit ray.
      causticPrd.rgb = vec3(0.0);
    }
    else if (disc < 0.0)
    {
      // Miss: Gaussian falloff — disc = r² - d_closest², so -disc/r² is the
      // normalised squared excess distance beyond the sphere edge.
      causticPrd.rgb *= exp(disc / r2 * pc.causticFalloff);
    }
    // else: direct hit — keep full transmittance.
  }
  else
  {
    // Sun caustic/shadow mode: weight transmittance by how well the exit direction
    // points at the sun.  Hard cutoff at 2× the cone half-angle; smoothstep between
    // [2×, 1×] so caustic edges fade smoothly rather than cutting off sharply.
    vec3 sunDir = normalize(vec3(cos(pc.sunElevation) * cos(pc.sunAzimuth),
                                 cos(pc.sunElevation) * sin(pc.sunAzimuth),
                                 sin(pc.sunElevation)));
    float cosDir  = dot(normalize(gl_WorldRayDirectionEXT), sunDir);
    float cosHalf = cos(pc.sunConeHalf);
    float cosWide = 2.0 * cosHalf * cosHalf - 1.0;  // cos(2 * sunConeHalf)
    if (cosDir < cosWide)
    {
      causticPrd.rgb = vec3(0.0);
    }
    else if (cosDir < cosHalf)
    {
      float t = (cosDir - cosWide) / max(1e-6, cosHalf - cosWide);
      causticPrd.rgb *= t * t * (3.0 - 2.0 * t);  // smoothstep
    }
  }
}
