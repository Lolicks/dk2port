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

struct Instance {
    float4 worldSize;   // xy = tile centre, zw = quad size
    float4 tintIndex;   // rgb = tint, a = atlas cell index
    float4 effect;      // x = effect id (0 none, 1 water, 2 lava), y = phase
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
    float4 tint;
    float4 effect;
};

constant float2 kQuad[6] = {
    float2(-0.5f, -0.5f), float2(0.5f, -0.5f), float2(0.5f, 0.5f),
    float2(-0.5f, -0.5f), float2(0.5f, 0.5f), float2(-0.5f, 0.5f),
};

vertex VertexOut instance_vertex(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    constant Uniforms &u [[buffer(0)]],
    device const Instance *instances [[buffer(1)]]) {
    Instance inst = instances[iid];
    float2 local = kQuad[vid];
    float2 p = inst.worldSize.xy + local * inst.worldSize.zw - u.center;
    p *= u.zoom;
    float c = cos(u.yaw), s = sin(u.yaw);
    p = float2(p.x * c - p.y * s, p.x * s + p.y * c);
    float2 ndc = float2(p.x / (u.aspect * 13.0f), p.y / 13.0f);

    VertexOut out;
    out.effect = inst.effect;
    out.position = float4(ndc, 0.0f, 1.0f);
    uint cell = (uint)inst.tintIndex.a;
    float2 cellXY = float2(float(cell % 8u), float(cell / 8u));
    out.uv = (cellXY + (local + 0.5f)) / 8.0f;
    out.tint = float4(inst.tintIndex.rgb, 1.0f);
    return out;
}

fragment float4 instance_fragment(
    VertexOut in [[stage_in]],
    constant Uniforms &u [[buffer(0)]],
    texture2d<float> atlas [[texture(0)]],
    sampler samp [[sampler(0)]]) {
    float4 texel = atlas.sample(samp, in.uv);
    float3 rgb = texel.rgb * in.tint.rgb;
    float e = in.effect.x;
    if (e > 0.5f) {
        float wave = sin(u.time * 2.2f + in.effect.y) * 0.10f +
                     sin(u.time * 1.3f + in.effect.y * 1.7f) * 0.05f;
        if (e < 1.5f) {
            rgb += float3(wave * 0.5f, wave * 0.8f, wave);
        } else {
            float glow = (sin(u.time * 3.0f + in.effect.y) + 1.0f) * 0.12f;
            rgb += float3(glow, glow * 0.35f, 0.0f);
        }
    }
    return float4(rgb, 1.0f);
}

struct ParticleIn {
    float4 position [[attribute(0)]];   // xy world, zw velocity
    float4 color   [[attribute(1)]];    // rgb + life
};

struct ParticleOut {
    float4 position [[position]];
    float4 color;
};

vertex ParticleOut particle_vertex(
    ParticleIn in [[stage_in]],
    constant Uniforms &u [[buffer(0)]],
    unsigned int iid [[instance_id]]) {
    ParticleOut out;
    float t = fmod(u.time + float(iid) * 0.37f, 3.0f);
    float2 p = in.position.xy + in.position.zw * t - u.center;
    p *= u.zoom;
    float c = cos(u.yaw), s = sin(u.yaw);
    p = float2(p.x * c - p.y * s, p.x * s + p.y * c);
    out.position = float4(p.x / (u.aspect * 13.0f), p.y / 13.0f, 0.0f, 1.0f);
    out.color = float4(in.color.rgb, in.color.a * (1.0f - t / 3.0f));
    return out;
}

fragment float4 particle_fragment(ParticleOut in [[stage_in]]) {
    return in.color;
}

/* Original KMF geometry (static MESH or animated ANIM frames).
   Layout is identical to DK2MeshUniforms on the Swift side (80 bytes). */
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
};

struct MeshOut {
    float4 position [[position]];
    float2 uv;
};

/* The exported KMF vertices use XY as the ground plane and Z as height
   (negative in the source files, hence the negation). placement holds
   xy = tile centre, z = uniform scale, w = atlas cell index. */
vertex MeshOut mesh_vertex(
    MeshVertexIn in [[stage_in]],
    constant MeshUniforms &mu [[buffer(2)]],
    constant float4 &placement [[buffer(1)]]) {
    float3 local = in.position.xyz * placement.z;
    float3 world = float3(placement.x + local.x,
                          placement.y + local.y,
                          -local.z);
    MeshOut out;
    out.position = mu.viewProjection * float4(world, 1.0f);

    // Same 8x8 atlas cell convention as instance_vertex; fract() keeps UVs
    // from the original OBJ inside the cell.
    uint cell = (uint)placement.w;
    float2 cellXY = float2(float(cell % 8u), float(cell / 8u));
    out.uv = (cellXY + fract(in.uv)) / 8.0f;
    return out;
}

fragment float4 mesh_fragment(
    MeshOut in [[stage_in]],
    texture2d<float> atlas [[texture(0)]],
    sampler samp [[sampler(0)]]) {
    return atlas.sample(samp, in.uv);
}
