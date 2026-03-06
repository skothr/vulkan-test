#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 prd;

layout(push_constant) uniform PC
{
  float ior;
  float tintAmount;
  int   maxDepth;
  float sunAzimuth;
  float sunElevation;
  float sunIntensity;
} pc;

void main ()
{
  vec3 orig = gl_WorldRayOriginEXT;
  vec3 dir  = normalize(gl_WorldRayDirectionEXT);

  // Ground plane at z = -0.6 (Z is world-up; checkerboard in XY)
  if (dir.z < -0.0001)
  {
    float t = (-0.6 - orig.z) / dir.z;
    if (t > 0.001)
    {
      vec3 p     = orig + t * dir;
      int  cx    = int(floor(p.x * 2.0));
      int  cy    = int(floor(p.y * 2.0));
      bool check = ((cx + cy) & 1) == 0;
      prd.rgb = check ? vec3(0.85) : vec3(0.12);
      return;
    }
  }

  // Sky gradient (Z is up)
  float sky = max(0.0, dir.z);
  prd.rgb = mix(vec3(0.45, 0.55, 0.85), vec3(0.08, 0.15, 0.45), sky);

  // Directional sun — direction derived from azimuth + elevation push constants
  vec3  sunDir = normalize(vec3(cos(pc.sunElevation) * cos(pc.sunAzimuth),
                                cos(pc.sunElevation) * sin(pc.sunAzimuth),
                                sin(pc.sunElevation)));
  float sun    = max(0.0, dot(dir, sunDir));
  prd.rgb += pow(sun, 256.0) * vec3(2.0, 1.9, 1.5) * pc.sunIntensity;
  prd.rgb += pow(sun,   6.0) * vec3(0.4, 0.3, 0.1) * pc.sunIntensity;
}
