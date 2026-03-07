#version 460
#extension GL_EXT_ray_tracing : require

// Shadow/caustic ray payload — .rgb = accumulated glass transmittance, .a = remaining bounces
layout(location = 1) rayPayloadInEXT vec4 causticPrd;
hitAttributeEXT vec3 sphereNormal;

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
  // Occlusion-test mode (a < 0): glass blocks the point light — hard shadow.
  if (causticPrd.a < 0.0) { causticPrd.rgb = vec3(0.0); return; }

  // Separate mode from depth counter.
  // a >= 50: point-light caustic mode (depth stored as a - 50).
  // a <  50: sun caustic / shadow mode (depth stored as a).
  bool  ptCaustic = (causticPrd.a >= 50.0);
  float depth     = ptCaustic ? (causticPrd.a - 50.0) : causticPrd.a;

  // Each glass surface hit costs one bounce; run out → block all light
  if (depth <= 0.5) { causticPrd.rgb = vec3(0.0); return; }
  depth       -= 1.0;
  causticPrd.a = ptCaustic ? (depth + 50.0) : depth;

  // Transform object-space sphere normal to world space
  vec3 worldNormal = normalize(mat3(gl_ObjectToWorldEXT) * sphereNormal);

  vec3  hitPos  = gl_WorldRayOriginEXT + gl_HitTEXT * gl_WorldRayDirectionEXT;
  vec3  rayDir  = normalize(gl_WorldRayDirectionEXT);
  bool  entering = dot(rayDir, worldNormal) < 0.0;
  vec3  N        = entering ? worldNormal : -worldNormal;
  float eta      = entering ? (1.0 / pc.ior) : pc.ior;

  // Schlick Fresnel — compute transmittance (1 - reflectance)
  float cosI    = clamp(dot(-rayDir, N), 0.0, 1.0);
  float r0      = (1.0 - pc.ior) / (1.0 + pc.ior);
  r0            = r0 * r0;
  float fresnel = r0 + (1.0 - r0) * pow(1.0 - cosI, 5.0);

  vec3 refrDir = refract(rayDir, N, eta);
  bool totalIR = dot(refrDir, refrDir) < 0.001;
  if (totalIR) { causticPrd.rgb = vec3(0.0); return; }

  // Accumulate transmittance; apply tint only on entry (once per glass pass)
  vec3 tint = mix(vec3(1.0), vec3(pc.tintR, pc.tintG, pc.tintB), pc.tintAmount);
  if (entering) { causticPrd.rgb *= tint * (1.0 - fresnel); }
  else          { causticPrd.rgb *=        (1.0 - fresnel); }

  // Continue shadow ray along the refracted path through/out of the glass
  vec3 offset = N * 0.001;
  traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF,
              1, 1, 1,
              hitPos - offset, 0.001, refrDir, 10000.0,
              1);
}
