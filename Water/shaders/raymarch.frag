#version 430 core
out vec4 FragColor;
in vec2 uv;

uniform sampler3D u_Density;
uniform vec3 u_CameraPos;
uniform mat4 u_InvView;
uniform mat4 u_InvProj;
uniform vec3 u_FluidColor;
uniform vec3 u_LightColor;
uniform vec3 u_LightDir;
uniform float u_Absorption;

vec2 intersectAABB(vec3 o, vec3 d, vec3 boxMin, vec3 boxMax) {
    vec3 invD = 1.0 / d;
    vec3 t0 = (boxMin - o) * invD;
    vec3 t1 = (boxMax - o) * invD;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float tNear = max(max(tmin.x, tmin.y), tmin.z);
    float tFar = min(min(tmax.x, tmax.y), tmax.z);
    return vec2(tNear, tFar);
}

void main() {
    vec4 clip = vec4(uv * 2.0 - 1.0, -1.0, 1.0);
    vec4 eye = u_InvProj * clip;
    vec3 rayDir = normalize(mat3(u_InvView) * eye.xyz);
    vec3 rayOrigin = u_CameraPos;
    
    // Fluid volume bounding box in world space
    vec3 boxMin = vec3(-1.0);
    vec3 boxMax = vec3(1.0);
    
    vec2 t = intersectAABB(rayOrigin, rayDir, boxMin, boxMax);
    if (t.x > t.y || t.y < 0.0) {
        FragColor = vec4(0.0);
        return; // Ray missed box completely
    }
    
    t.x = max(t.x, 0.0);
    
    int steps = 128; // Adjust for quality
    float dt = (t.y - t.x) / float(steps);
    
    float transmission = 1.0;
    vec3 color = vec3(0.0);
    
    vec3 pos = rayOrigin + rayDir * t.x;
    
    vec3 ambientColor = vec3(0.1, 0.15, 0.2);
    
    for(int i = 0; i < steps; i++) {
        // Map from [-1, 1] bounds to [0, 1] texture coordinates
        vec3 samplePos = pos * 0.5 + 0.5;
        
        float density = texture(u_Density, samplePos).r;
        
        // Add a threshold to avoid rendering noise and empty space
        if (density > 0.01) {
            // Rough gradient/normal calculation using central differences
            float eps = 1.0 / 64.0; // assuming ~64^3 res for gradient step
            float dx = texture(u_Density, samplePos + vec3(eps, 0, 0)).r - texture(u_Density, samplePos - vec3(eps, 0, 0)).r;
            float dy = texture(u_Density, samplePos + vec3(0, eps, 0)).r - texture(u_Density, samplePos - vec3(0, eps, 0)).r;
            float dz = texture(u_Density, samplePos + vec3(0, 0, eps)).r - texture(u_Density, samplePos - vec3(0, 0, eps)).r;
            
            vec3 normal = -normalize(vec3(dx, dy, dz) + 0.0001); // Negative gradient points outward
            
            // Basic Diffuse Lighting
            float diff = max(dot(normal, u_LightDir), 0.0);
            
            // Fake shadowing/scattering: sample towards light simply by comparing density
            float densityTowardsLight = texture(u_Density, samplePos + u_LightDir * 0.05).r;
            float shadow = exp(-densityTowardsLight * 5.0); // simple beer's law for shadow
            
            vec3 scatterCol = u_FluidColor * (ambientColor + u_LightColor * diff * shadow);
            
            // Tune absorption
            float stepTrans = exp(-(density * u_Absorption) * dt);
            
            color += scatterCol * density * transmission * (1.0 - stepTrans);
            
            transmission *= stepTrans;
            if (transmission < 0.01) break; // Early exit
        }
        
        pos += rayDir * dt;
    }
    
    // Add sky background instead of pure black
    vec3 skyColor = mix(vec3(0.9, 0.9, 0.95), vec3(0.5, 0.7, 0.9), rayDir.y * 0.5 + 0.5);
    color += skyColor * transmission;
    
    // Pre-multiplied alpha or regular blending (we return color, and let transmission act as remaining alpha)
    FragColor = vec4(color, 1.0);
}
