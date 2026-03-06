#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 prd;

void main ()
{
  vec3 orig = gl_WorldRayOriginEXT;
  vec3 dir  = normalize(gl_WorldRayDirectionEXT);

  // Virtual checkerboard floor at y = -1.0
  if (dir.y < -0.0001)
  {
    float t = (-1.0 - orig.y) / dir.y;
    if (t > 0.001)
    {
      vec3 p     = orig + t * dir;
      int  cx    = int(floor(p.x * 2.0));
      int  cz    = int(floor(p.z * 2.0));
      bool check = ((cx + cz) & 1) == 0;
      prd.rgb = check ? vec3(0.85) : vec3(0.12);
      return;
    }
  }

  // Sky gradient
  float sky = max(0.0, dir.y);
  prd.rgb = mix(vec3(0.45, 0.55, 0.85), vec3(0.08, 0.15, 0.45), sky);

  // Bright directional sun (creates visible reflection on the glass)
  vec3 sunDir = normalize(vec3(0.7, 0.8, 0.3));
  float sun   = max(0.0, dot(dir, sunDir));
  prd.rgb += pow(sun, 256.0) * vec3(2.0, 1.9, 1.5);  // sharp highlight
  prd.rgb += pow(sun,   6.0) * vec3(0.4, 0.3, 0.1);  // glow halo
}
