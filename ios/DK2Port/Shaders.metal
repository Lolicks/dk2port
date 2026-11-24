#include <metal_stdlib>
using namespace metal;

struct Uniforms {
    float2 center;
    float zoom;
    float yaw;
    float aspect;
    float time;
    float _pad0;
    float _pad1;
    float _pad2;
};

/* 80 bytes; matched field-for-field by DK2EffectsUniforms in Swift. */
struct EffectsUniforms {
    float4 lightDirection;  // xyz = toward light, w = intensity
    float4 lightColor;      // rgb = colour, w = ambient
    float4 fog;             // rgb = colour, w = density
    float4 shading;         // x bump, y shadow, z spec power, w spec strength
    float4 waves;           // x water speed, y lava speed, z distortion, w spare
};

struct Instance {
    float4 worldSize;   // xy = tile centre, zw = quad size
    float4 tintIndex;   // rgb = tint, a = atlas cell index
    float4 effect;      // x = effect id (0 none, 1 water, 2 lava), y = phase, z = shadow
};

struct FogInstance {
    float4 worldSize;   // xy = tile centre, zw = quad size
    float4 shade;       // rgb = overlay colour, a = opacity
};

constant float2 kQuad[6] = {
    float2(-0.5f, -0.5f), float2(0.5f, -0.5f), float2(0.5f, 0.5f),
    float2(-0.5f, -0.5f), float2(0.5f, 0.5f), float2(-0.5f, 0.5f),
};

/* Shared screen-space transform for the 2D tile and fog-overlay passes. */
static inline float2 project2D(float2 world, constant Uniforms &u) {
    float2 p = world - u.center;
    p *= u.zoom;
    float c = cos(u.yaw), s = sin(u.yaw);
    p = float2(p.x * c - p.y * s, p.x * s + p.y * c);
    return float2(p.x / (u.aspect * 13.0f), p.y / 13.0f);
}

// ---------------------------------------------------------------------------
// Instanced tiles (water / lava included)
// ---------------------------------------------------------------------------

struct VertexOut {
    float4 position  [[position]];
    float2 uv;
    float2 bumpUV;
    float2 cellOrigin;
    float3 viewDir;
    float4 tint;
    float4 effect;
    float shadow;
};

vertex VertexOut instance_vertex(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    constant Uniforms &u [[buffer(0)]],
    device const Instance *instances [[buffer(1)]],
    constant EffectsUniforms &fx [[buffer(2)]]) {
    Instance inst = instances[iid];
    float2 local = kQuad[vid];
    float2 world = inst.worldSize.xy + local * inst.worldSize.zw;

    VertexOut out;
    out.position = float4(project2D(world, u), 0.0f, 1.0f);
    uint cell = (uint)inst.tintIndex.a;
    float2 cellXY = float2(float(cell % 8u), float(cell / 8u));
    out.cellOrigin = cellXY / 8.0f;
    out.uv = out.cellOrigin + (local + 0.5f) / 8.0f;
    out.tint = float4(inst.tintIndex.rgb, 1.0f);
    out.effect = inst.effect;
    out.shadow = clamp(inst.effect.z, 0.0f, 1.0f);

    // Cell 0 of the bump atlas holds water, cell 1 holds lava.
    uint bcell = (inst.effect.x > 1.5f) ? 1u : 0u;
    float2 bXY = float2(float(bcell % 8u), float(bcell / 8u));
    out.bumpUV = (bXY + local + 0.5f) / 8.0f;
    out.viewDir = normalize(float3(u.center - world, 12.0f));
    return out;
}

fragment float4 instance_fragment(
    VertexOut in [[stage_in]],
    constant Uniforms &u [[buffer(0)]],
    constant EffectsUniforms &fx [[buffer(2)]],
    texture2d<float> atlas [[texture(0)]],
    texture2d<float> bump [[texture(1)]],
    sampler samp [[sampler(0)]]) {
    float e = in.effect.x;
    float2 uv = in.uv;
    float3 n = float3(0.0f, 0.0f, 1.0f);
    float3 rgb = atlas.sample(samp, uv).rgb * in.tint.rgb;

    if (e > 0.5f) {
        bool isLava = e > 1.5f;
        float speed = isLava ? fx.waves.y : fx.waves.x;
        float t = u.time * speed + in.effect.y;
        float2 wobble = float2(sin(t * 1.7f + in.uv.y * 34.0f),
                               cos(t * 1.3f + in.uv.x * 27.0f));
        float amp = fx.waves.z * 0.012f;
        float2 lo = in.cellOrigin + float2(0.015625f);
        float2 hi = in.cellOrigin + float2(0.984375f);
        uv = clamp(in.uv + wobble * amp, lo, hi);
        rgb = atlas.sample(samp, uv).rgb * in.tint.rgb;

        // Height field -> normal via a finite difference inside the bump cell.
        float2 buv = clamp(in.bumpUV + wobble * amp * 2.0f,
                           in.bumpUV - float2(0.015625f),
                           in.bumpUV + float2(0.015625f));
        float texelStep = 1.0f / 512.0f;
        float h0 = bump.sample(samp, buv).r;
        float hx = bump.sample(samp, buv + float2(texelStep, 0.0f)).r;
        float hy = bump.sample(samp, buv + float2(0.0f, texelStep)).r;
        n = normalize(float3((h0 - hx) * fx.shading.x,
                             (h0 - hy) * fx.shading.x, 1.0f));

        if (isLava) {
            float glow = (sin(t * 1.9f) + 1.0f) * 0.12f;
            rgb += float3(glow, glow * 0.35f, 0.0f);
        } else {
            rgb += float3(wobble.x, wobble.y, 1.0f) * 0.03f;
        }
    }

    float3 L = normalize(fx.lightDirection.xyz);
    float diffuse = max(dot(n, L), 0.0f);
    float3 lit = rgb * (fx.lightColor.w + fx.lightDirection.w * diffuse);
    float3 H = normalize(L + in.viewDir);
    float spec = pow(max(dot(n, H), 0.0f), fx.shading.z) * fx.shading.w;
    lit += fx.lightColor.rgb * spec;
    lit *= (1.0f - fx.shading.y * in.shadow);

    float horizontal = 12.0f * length(in.viewDir.xy) / max(0.05f, in.viewDir.z);
    float fog = 1.0f - exp(-horizontal * fx.fog.w);
    lit = mix(lit, fx.fog.rgb, clamp(fog, 0.0f, 0.9f));
    return float4(lit, 1.0f);
}

// ---------------------------------------------------------------------------
// Additive particles
// ---------------------------------------------------------------------------

struct ParticleIn {
    float4 position [[attribute(0)]];   // xy world, zw velocity
    float4 color   [[attribute(1)]];    // rgb + life
};

struct ParticleOut {
    float4 position  [[position]];
    float pointSize [[point_size]];
    float4 color;
};

vertex ParticleOut particle_vertex(
    ParticleIn in [[stage_in]],
    constant Uniforms &u [[buffer(0)]],
    unsigned int iid [[instance_id]]) {
    float t = fmod(u.time + float(iid) * 0.37f, 3.0f);
    float2 p = in.position.xy + in.position.zw * t;

    ParticleOut out;
    // Slightly in front of the z=0 tile plane so the read-only depth test
    // does not discard every particle against the tiles' own depth.
    out.position = float4(project2D(p, u), -0.2f, 1.0f);
    out.pointSize = clamp(7.0f * u.zoom * (1.0f - 0.18f * t), 2.0f, 28.0f);
    out.color = float4(in.color.rgb, in.color.a * (1.0f - t / 3.0f));
    return out;
}

struct ParticleFragmentIn {
    float4 position [[position]];
    float4 color;
};

fragment float4 particle_fragment(
    ParticleFragmentIn in [[stage_in]],
    constant EffectsUniforms &fx [[buffer(0)]]) {
    return float4(in.color.rgb * fx.lightColor.rgb, in.color.a);
}

// ---------------------------------------------------------------------------
// Fog of war overlay (dark quads over unrevealed tiles)
// ---------------------------------------------------------------------------

struct FogOut {
    float4 position [[position]];
    float4 shade;
};

vertex FogOut fog_vertex(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    constant Uniforms &u [[buffer(0)]],
    device const FogInstance *instances [[buffer(1)]]) {
    FogInstance inst = instances[iid];
    float2 world = inst.worldSize.xy + kQuad[vid] * inst.worldSize.zw;
    FogOut out;
    out.position = float4(project2D(world, u), 0.999f, 1.0f);
    out.shade = inst.shade;
    return out;
}

fragment float4 fog_fragment(FogOut in [[stage_in]]) {
    return in.shade;
}

// ---------------------------------------------------------------------------
// Original KMF geometry. Both the plain and lit mesh pipelines consume the
// normal stored in the 18-byte VERT/MeshVertEx record (32-bit floats) that is
// carried through DKAF v2 / OBJ `vn` lines by DK2MeshVertex.
// ---------------------------------------------------------------------------

/* Layout is identical to DK2MeshUniforms on the Swift side (80 bytes). */
struct MeshUniforms {
    float4x4 viewProjection;
    float time;
    float pad0;
    float pad1;
    float pad2;
};

struct MeshVertexIn {
    float4 position [[attribute(0)]];
    float2 uv       [[attribute(1)]];
    float3 normal   [[attribute(2)]];
};

struct MeshOut {
    float4 position [[position]];
    float2 uv;
    float3 normal;
};

/* The exported KMF vertices use XY as the ground plane and Z as height
   (negative in the source files, hence the negation). placement holds
   xy = tile centre, z = uniform scale, w = atlas cell index. */
vertex MeshOut mesh_vertex(
    MeshVertexIn in [[stage_in]],
    constant MeshUniforms &mu [[buffer(2)]],
    constant float4 &placement [[buffer(1)]],
    constant float4 &origin [[buffer(4)]]) {
    float3 local = in.position.xyz * placement.z;
    float3 world = float3(placement.x + local.x,
                          placement.y + local.y,
                          origin.x - local.z);
    MeshOut out;
    out.position = mu.viewProjection * float4(world, 1.0f);
    uint cell = (uint)placement.w;
    float2 cellXY = float2(float(cell % 8u), float(cell / 8u));
    out.uv = (cellXY + fract(in.uv)) / 8.0f;
    out.normal = in.normal;
    return out;
}

fragment float4 mesh_fragment(
    MeshOut in [[stage_in]],
    texture2d<float> atlas [[texture(0)]],
    sampler samp [[sampler(0)]]) {
    return atlas.sample(samp, in.uv);
}

struct LitMeshOut {
    float4 position [[position]];
    float2 uv;
    float3 normal;
    float3 worldPos;
    float3 viewDir;
    float fog;
    float shadow;
};

vertex LitMeshOut lit_mesh_vertex(
    MeshVertexIn in [[stage_in]],
    constant Uniforms &u [[buffer(0)]],
    constant float4 &placement [[buffer(1)]],
    constant MeshUniforms &mu [[buffer(2)]],
    constant EffectsUniforms &fx [[buffer(3)]],
    constant float4 &origin [[buffer(4)]]) {
    float3 local = in.position.xyz * placement.z;
    float3 world = float3(placement.x + local.x,
                          placement.y + local.y,
                          origin.x - local.z);

    LitMeshOut out;
    out.position = mu.viewProjection * float4(world, 1.0f);
    uint cell = (uint)placement.w;
    float2 cellXY = float2(float(cell % 8u), float(cell / 8u));
    out.uv = (cellXY + fract(in.uv)) / 8.0f;
    // world = (local.x, local.y, -local.z); the inverse-transpose reflection
    // negates the normal's Z component.
    out.normal = float3(in.normal.x, in.normal.y, -in.normal.z);
    out.worldPos = world;
    out.viewDir = normalize(float3(u.center - world.xy, 12.0f - world.z));
    float horizontal = length(world.xy - u.center);
    out.fog = 1.0f - exp(-horizontal * fx.fog.w);
    out.shadow = clamp(1.0f - world.z, 0.0f, 1.0f);
    return out;
}

fragment float4 lit_mesh_fragment(
    LitMeshOut in [[stage_in]],
    constant EffectsUniforms &fx [[buffer(3)]],
    texture2d<float> atlas [[texture(0)]],
    sampler samp [[sampler(0)]]) {
    float4 texel = atlas.sample(samp, in.uv);
    // Use the original VERT normal; fall back to a derivative only for
    // meshes exported without normals (legacy DKAF v1 buffers).
    float3 n = in.normal;
    if (dot(n, n) < 0.25f) {
        n = cross(dfdy(in.worldPos), dfdx(in.worldPos));
        if (n.z < 0.0f) n = -n;
    }
    n = normalize(n);

    float3 L = normalize(fx.lightDirection.xyz);
    float diffuse = max(dot(n, L), 0.0f);
    float3 lit = texel.rgb * (fx.lightColor.w + fx.lightDirection.w * diffuse);
    float3 H = normalize(L + in.viewDir);
    float spec = pow(max(dot(n, H), 0.0f), fx.shading.z) * fx.shading.w * 0.5f;
    lit += fx.lightColor.rgb * spec;
    lit *= (1.0f - fx.shading.y * in.shadow);
    lit = mix(lit, fx.fog.rgb, clamp(in.fog, 0.0f, 0.9f));
    return float4(lit, texel.a);
}
