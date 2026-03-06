#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 prd;
hitAttributeEXT vec2 baryCoords;

layout(set = 0, binding = 0) uniform accelerationStructureEXT tlas;
layout(set = 0, binding = 3) readonly buffer VertexBuf { float data[]; } verts;
layout(set = 0, binding = 4) readonly buffer IndexBuf  { uint  data[]; } inds;

const int   MAX_DEPTH = 4;
const float IOR       = 1.5;  // glass index of refraction

uint getIdx (uint i)
{
  uint word = inds.data[i >> 1u];
  return (i & 1u) == 0u ? (word & 0xFFFFu) : (word >> 16u);
}

void main ()
{
  // Bail out at recursion limit
  if (int(prd.w) >= MAX_DEPTH) { prd.rgb = vec3(0.0); return; }

  uint i0 = getIdx(uint(gl_PrimitiveID) * 3u + 0u);
  uint i1 = getIdx(uint(gl_PrimitiveID) * 3u + 1u);
  uint i2 = getIdx(uint(gl_PrimitiveID) * 3u + 2u);

  // Object-space vertex positions → face normal
  vec3 p0 = vec3(verts.data[i0*6u+0u], verts.data[i0*6u+1u], verts.data[i0*6u+2u]);
  vec3 p1 = vec3(verts.data[i1*6u+0u], verts.data[i1*6u+1u], verts.data[i1*6u+2u]);
  vec3 p2 = vec3(verts.data[i2*6u+0u], verts.data[i2*6u+1u], verts.data[i2*6u+2u]);
  vec3 objNormal   = normalize(cross(p1 - p0, p2 - p0));
  // Transform normal to world space (pure rotation, so transpose(inverse) == model rotation)
  vec3 worldNormal = normalize(mat3(gl_ObjectToWorldEXT) * objNormal);

  // Barycentric vertex colors → subtle tint for refracted light
  vec3 bary = vec3(1.0 - baryCoords.x - baryCoords.y, baryCoords.x, baryCoords.y);
  vec3 c0   = vec3(verts.data[i0*6u+3u], verts.data[i0*6u+4u], verts.data[i0*6u+5u]);
  vec3 c1   = vec3(verts.data[i1*6u+3u], verts.data[i1*6u+4u], verts.data[i1*6u+5u]);
  vec3 c2   = vec3(verts.data[i2*6u+3u], verts.data[i2*6u+4u], verts.data[i2*6u+5u]);
  vec3 tint = mix(vec3(1.0), bary.x*c0 + bary.y*c1 + bary.z*c2, 0.25);

  vec3  hitPos = gl_WorldRayOriginEXT + gl_HitTEXT * gl_WorldRayDirectionEXT;
  vec3  rayDir = normalize(gl_WorldRayDirectionEXT);

  // N always faces toward the incoming ray; eta = IOR ratio at the interface
  bool  entering = dot(rayDir, worldNormal) < 0.0;
  vec3  N        = entering ? worldNormal : -worldNormal;
  float eta      = entering ? (1.0 / IOR) : IOR;

  // Schlick Fresnel approximation
  float cosI    = clamp(dot(-rayDir, N), 0.0, 1.0);
  float r0      = (1.0 - IOR) / (1.0 + IOR);
  r0            = r0 * r0;
  float fresnel = r0 + (1.0 - r0) * pow(1.0 - cosI, 5.0);

  vec3 reflDir = reflect(rayDir, N);
  vec3 refrDir = refract(rayDir, N, eta);
  bool totalIR = dot(refrDir, refrDir) < 0.001;
  if (totalIR) { fresnel = 1.0; }

  float savedDepth = prd.w;
  // Small offset to avoid re-hitting the same surface
  vec3 offset = N * 0.001;

  // --- Reflected ray ---
  prd.w = savedDepth + 1.0;
  traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF,
              0, 0, 0,
              hitPos + offset, 0.001, reflDir, 1000.0, 0);
  vec3 reflColor = prd.rgb;

  // --- Refracted ray ---
  vec3 refrColor = vec3(0.0);
  if (!totalIR)
  {
    prd.w = savedDepth + 1.0;
    traceRayEXT(tlas, gl_RayFlagsOpaqueEXT, 0xFF,
                0, 0, 0,
                hitPos - offset, 0.001, refrDir, 1000.0, 0);
    refrColor = prd.rgb * tint;  // tint refracted light with face color
  }

  prd.rgb = mix(refrColor, reflColor, fresnel);
  prd.w   = savedDepth;  // restore depth for the caller
}
