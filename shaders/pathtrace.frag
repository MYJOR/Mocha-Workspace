#version 410 core

in vec2 vUV;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outDepth;

layout(std140) uniform CameraUBO {
    mat4 invViewProj;
    vec4 camPos;
    vec4 camDir;
    vec4 camUp;
    vec4 camRight;
    vec4 orthoExtents;
};

uniform samplerBuffer uCubeData;
uniform samplerBuffer uCubeBVH;
uniform int   uCubeCount;

uniform samplerBuffer uTriData;
uniform samplerBuffer uTriBVH;
uniform int   uTriCount;

uniform int   uFrameIndex;
uniform uint  uSeed;
uniform vec2  uResolution;

uniform vec3  uSunDir;
uniform vec3  uSunColor;
uniform vec3  uSkyZenith;
uniform vec3  uSkyHorizon;
uniform vec3  uAmbient;

uniform float uEmissiveIntensity;

uniform int   uAOEnabled;
uniform float uAOStrength;
uniform float uAORadius;
uniform int   uAOSamples;

vec2 octEncode(vec3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    }
    return n.xy * 0.5 + 0.5;
}

#define MAX_BOUNCES 3
#define PI 3.14159265359

uint rngState;

uint pcgHash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float randFloat() {
    rngState = pcgHash(rngState);
    return float(rngState) / 4294967295.0;
}

vec3 cosineWeightedHemisphere(vec3 normal) {
    float u1 = randFloat();
    float u2 = randFloat();
    float r  = sqrt(u1);
    float theta = 2.0 * PI * u2;

    vec3 tangent;
    if (abs(normal.y) < 0.999)
        tangent = normalize(cross(normal, vec3(0.0, 1.0, 0.0)));
    else
        tangent = normalize(cross(normal, vec3(1.0, 0.0, 0.0)));
    vec3 bitangent = cross(normal, tangent);

    return normalize(tangent * r * cos(theta) + bitangent * r * sin(theta) + normal * sqrt(1.0 - u1));
}

// ---- primitive fetch helpers ------------------------------------------------

void fetchCube(int idx, out vec3 bmin, out vec3 bmax, out vec3 albedo, out vec3 emission) {
    int base = idx * 4;
    vec4 d0 = texelFetch(uCubeData, base + 0);
    vec4 d1 = texelFetch(uCubeData, base + 1);
    vec4 d2 = texelFetch(uCubeData, base + 2);
    vec4 d3 = texelFetch(uCubeData, base + 3);
    bmin     = d0.xyz;
    bmax     = d1.xyz;
    albedo   = d2.xyz;
    emission = d3.xyz;
}

void fetchTriangle(int idx, out vec3 v0, out vec3 v1, out vec3 v2,
                   out vec3 albedo, out vec3 emission) {
    int base = idx * 5;
    vec4 d0 = texelFetch(uTriData, base + 0);
    vec4 d1 = texelFetch(uTriData, base + 1);
    vec4 d2 = texelFetch(uTriData, base + 2);
    vec4 d3 = texelFetch(uTriData, base + 3);
    vec4 d4 = texelFetch(uTriData, base + 4);
    v0       = d0.xyz;
    v1       = d1.xyz;
    v2       = d2.xyz;
    albedo   = d3.xyz;
    emission = d4.xyz;
}

// ---- intersection routines --------------------------------------------------

bool intersectAABB(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, out float tNear, out vec3 normal) {
    vec3 invRd = 1.0 / rd;
    vec3 t1 = (bmin - ro) * invRd;
    vec3 t2 = (bmax - ro) * invRd;
    vec3 tMin = min(t1, t2);
    vec3 tMax = max(t1, t2);

    tNear = max(max(tMin.x, tMin.y), tMin.z);
    float tFar = min(min(tMax.x, tMax.y), tMax.z);

    if (tNear > tFar || tFar < 0.0)
        return false;

    if (tNear < 0.0)
        tNear = tFar;

    normal = vec3(0.0);
    if (tNear == tMin.x) normal = vec3(-sign(rd.x), 0.0, 0.0);
    else if (tNear == tMin.y) normal = vec3(0.0, -sign(rd.y), 0.0);
    else normal = vec3(0.0, 0.0, -sign(rd.z));

    return true;
}

bool intersectAABBFast(vec3 ro, vec3 invRd, vec3 bmin, vec3 bmax, float tMax) {
    vec3 t1 = (bmin - ro) * invRd;
    vec3 t2 = (bmax - ro) * invRd;
    vec3 tMn = min(t1, t2);
    vec3 tMx = max(t1, t2);
    float tNear = max(max(tMn.x, tMn.y), tMn.z);
    float tFar  = min(min(tMx.x, tMx.y), tMx.z);
    return tNear <= tFar && tFar >= 0.0 && tNear < tMax;
}

bool intersectTriangle(vec3 ro, vec3 rd, vec3 v0, vec3 v1, vec3 v2,
                       out float t, out vec3 normal) {
    vec3 e1 = v1 - v0;
    vec3 e2 = v2 - v0;
    vec3 h  = cross(rd, e2);
    float a = dot(e1, h);
    if (abs(a) < 1e-7) return false;
    float f = 1.0 / a;
    vec3 s  = ro - v0;
    float u = f * dot(s, h);
    if (u < 0.0 || u > 1.0) return false;
    vec3 q  = cross(s, e1);
    float v = f * dot(rd, q);
    if (v < 0.0 || u + v > 1.0) return false;
    t = f * dot(e2, q);
    if (t < 0.001) return false;
    normal = normalize(cross(e1, e2));
    if (dot(normal, rd) > 0.0) normal = -normal;
    return true;
}

// ---- hit info struct --------------------------------------------------------

struct HitInfo {
    bool  hit;
    float t;
    vec3  normal;
    vec3  albedo;
    vec3  emission;
};

// ---- BVH traversal: cubes (bridges) ----------------------------------------

HitInfo traceCubes(vec3 ro, vec3 rd) {
    HitInfo best;
    best.hit = false;
    best.t   = 1e30;

    if (uCubeCount == 0) return best;

    vec3 invRd = 1.0 / rd;
    int stack[32];
    int stackTop = 0;
    stack[stackTop++] = 0;

    while (stackTop > 0) {
        int nodeIdx = stack[--stackTop];
        int base = nodeIdx * 2;
        vec4 d0 = texelFetch(uCubeBVH, base);
        vec4 d1 = texelFetch(uCubeBVH, base + 1);

        vec3 bmin = d0.xyz;
        vec3 bmax = d1.xyz;

        if (!intersectAABBFast(ro, invRd, bmin, bmax, best.t))
            continue;

        int primCount = floatBitsToInt(d1.w);

        if (primCount > 0) {
            int primStart = floatBitsToInt(d0.w);
            for (int i = 0; i < primCount; ++i) {
                vec3 cbmin, cbmax, alb, emi;
                fetchCube(primStart + i, cbmin, cbmax, alb, emi);
                float t;
                vec3 n;
                if (intersectAABB(ro, rd, cbmin, cbmax, t, n)) {
                    if (t > 0.001 && t < best.t) {
                        best.hit      = true;
                        best.t        = t;
                        best.normal   = n;
                        best.albedo   = alb;
                        best.emission = emi;
                    }
                }
            }
        } else {
            int rightChild = floatBitsToInt(d0.w);
            int leftChild  = nodeIdx + 1;
            stack[stackTop++] = rightChild;
            stack[stackTop++] = leftChild;
        }
    }
    return best;
}

// ---- BVH traversal: triangles (terrain) ------------------------------------

HitInfo traceTriangles(vec3 ro, vec3 rd) {
    HitInfo best;
    best.hit = false;
    best.t   = 1e30;

    if (uTriCount == 0) return best;

    vec3 invRd = 1.0 / rd;
    int stack[32];
    int stackTop = 0;
    stack[stackTop++] = 0;

    while (stackTop > 0) {
        int nodeIdx = stack[--stackTop];
        int base = nodeIdx * 2;
        vec4 d0 = texelFetch(uTriBVH, base);
        vec4 d1 = texelFetch(uTriBVH, base + 1);

        vec3 bmin = d0.xyz;
        vec3 bmax = d1.xyz;

        if (!intersectAABBFast(ro, invRd, bmin, bmax, best.t))
            continue;

        int primCount = floatBitsToInt(d1.w);

        if (primCount > 0) {
            int primStart = floatBitsToInt(d0.w);
            for (int i = 0; i < primCount; ++i) {
                vec3 tv0, tv1, tv2, alb, emi;
                fetchTriangle(primStart + i, tv0, tv1, tv2, alb, emi);
                float t;
                vec3 n;
                if (intersectTriangle(ro, rd, tv0, tv1, tv2, t, n) && t < best.t) {
                    best.hit      = true;
                    best.t        = t;
                    best.normal   = n;
                    best.albedo   = alb;
                    best.emission = emi;
                }
            }
        } else {
            int rightChild = floatBitsToInt(d0.w);
            int leftChild  = nodeIdx + 1;
            stack[stackTop++] = rightChild;
            stack[stackTop++] = leftChild;
        }
    }
    return best;
}

// ---- combined scene trace ---------------------------------------------------

HitInfo traceScene(vec3 ro, vec3 rd) {
    HitInfo cubeHit = traceCubes(ro, rd);
    HitInfo triHit  = traceTriangles(ro, rd);

    if (cubeHit.hit && (!triHit.hit || cubeHit.t < triHit.t)) return cubeHit;
    if (triHit.hit) return triHit;

    HitInfo miss;
    miss.hit = false;
    miss.t   = 1e30;
    return miss;
}

// ---- occlusion helpers ------------------------------------------------------

bool occludedCubes(vec3 ro, vec3 rd, float maxDist) {
    if (uCubeCount == 0) return false;

    vec3 invRd = 1.0 / rd;
    int stack[32];
    int stackTop = 0;
    stack[stackTop++] = 0;

    while (stackTop > 0) {
        int nodeIdx = stack[--stackTop];
        int base = nodeIdx * 2;
        vec4 d0 = texelFetch(uCubeBVH, base);
        vec4 d1 = texelFetch(uCubeBVH, base + 1);

        vec3 bmin = d0.xyz;
        vec3 bmax = d1.xyz;

        if (!intersectAABBFast(ro, invRd, bmin, bmax, maxDist))
            continue;

        int primCount = floatBitsToInt(d1.w);

        if (primCount > 0) {
            int primStart = floatBitsToInt(d0.w);
            for (int i = 0; i < primCount; ++i) {
                vec3 cbmin, cbmax, alb, emi;
                fetchCube(primStart + i, cbmin, cbmax, alb, emi);
                float t;
                vec3 n;
                if (intersectAABB(ro, rd, cbmin, cbmax, t, n) && t > 0.001 && t < maxDist)
                    return true;
            }
        } else {
            int rightChild = floatBitsToInt(d0.w);
            int leftChild  = nodeIdx + 1;
            stack[stackTop++] = rightChild;
            stack[stackTop++] = leftChild;
        }
    }
    return false;
}

bool occludedTriangles(vec3 ro, vec3 rd, float maxDist) {
    if (uTriCount == 0) return false;

    vec3 invRd = 1.0 / rd;
    int stack[32];
    int stackTop = 0;
    stack[stackTop++] = 0;

    while (stackTop > 0) {
        int nodeIdx = stack[--stackTop];
        int base = nodeIdx * 2;
        vec4 d0 = texelFetch(uTriBVH, base);
        vec4 d1 = texelFetch(uTriBVH, base + 1);

        vec3 bmin = d0.xyz;
        vec3 bmax = d1.xyz;

        if (!intersectAABBFast(ro, invRd, bmin, bmax, maxDist))
            continue;

        int primCount = floatBitsToInt(d1.w);

        if (primCount > 0) {
            int primStart = floatBitsToInt(d0.w);
            for (int i = 0; i < primCount; ++i) {
                vec3 tv0, tv1, tv2, alb, emi;
                fetchTriangle(primStart + i, tv0, tv1, tv2, alb, emi);
                float t;
                vec3 n;
                if (intersectTriangle(ro, rd, tv0, tv1, tv2, t, n) && t < maxDist)
                    return true;
            }
        } else {
            int rightChild = floatBitsToInt(d0.w);
            int leftChild  = nodeIdx + 1;
            stack[stackTop++] = rightChild;
            stack[stackTop++] = leftChild;
        }
    }
    return false;
}

bool occluded(vec3 ro, vec3 rd) {
    return occludedCubes(ro, rd, 1e30) || occludedTriangles(ro, rd, 1e30);
}

bool occludedWithinRadius(vec3 ro, vec3 rd, float maxDist) {
    return occludedCubes(ro, rd, maxDist) || occludedTriangles(ro, rd, maxDist);
}

// ---- AO & sky ---------------------------------------------------------------

float computeAO(vec3 hitPos, vec3 normal) {
    if (uAOSamples <= 0) return 1.0;
    vec3 origin = hitPos + normal * 0.002;
    int blocked = 0;
    for (int i = 0; i < uAOSamples; ++i) {
        vec3 dir = cosineWeightedHemisphere(normal);
        if (occludedWithinRadius(origin, dir, uAORadius))
            blocked++;
    }
    return 1.0 - float(blocked) / float(uAOSamples);
}

vec3 skyGradient(vec3 rd) {
    float t = 0.5 * (rd.y + 1.0);
    return mix(uSkyHorizon, uSkyZenith, t);
}

// ---- main -------------------------------------------------------------------

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);

    rngState = pcgHash(uint(pixel.x) + uint(pixel.y) * 4096u
                     + uint(uFrameIndex) * 16777259u + uSeed * 3141592u);

    vec2 jitter = vec2(randFloat(), randFloat());
    vec2 uv = (vec2(pixel) + jitter) / uResolution;
    vec2 ndc = uv * 2.0 - 1.0;

    vec4 nearPt = invViewProj * vec4(ndc, -1.0, 1.0);
    vec4 farPt  = invViewProj * vec4(ndc,  1.0, 1.0);
    nearPt /= nearPt.w;
    farPt  /= farPt.w;

    vec3 ro = nearPt.xyz;
    vec3 rd = normalize(farPt.xyz - nearPt.xyz);

    vec3 firstNormal = vec3(0.0);
    float firstDepth = 1e30;
    vec3 color = vec3(0.0);
    vec3 throughput = vec3(1.0);

    for (int bounce = 0; bounce < MAX_BOUNCES; ++bounce) {
        HitInfo hit = traceScene(ro, rd);

        if (!hit.hit) {
            color += throughput * skyGradient(rd);
            break;
        }

        vec3 hitPos = ro + rd * hit.t;

        if (bounce == 0) {
            firstNormal = hit.normal;
            firstDepth  = hit.t;
        }

        if (uEmissiveIntensity > 0.0 && dot(hit.emission, hit.emission) > 0.0) {
            color += throughput * min(hit.emission * uEmissiveIntensity, vec3(8.0));
        }

        vec3 shadowOrigin = hitPos + hit.normal * 0.002;
        float nDotL = max(dot(hit.normal, uSunDir), 0.0);
        if (nDotL > 0.0 && !occluded(shadowOrigin, uSunDir)) {
            color += throughput * hit.albedo * uSunColor * nDotL;
        }

        float aoFactor = 1.0;
        if (bounce == 0 && uAOEnabled != 0) {
            float ao = computeAO(hitPos, hit.normal);
            aoFactor = mix(1.0, ao, uAOStrength);
        }

        color += throughput * hit.albedo * uAmbient * aoFactor;

        throughput *= hit.albedo;

        {
            float p = max(0.1, max(throughput.x, max(throughput.y, throughput.z)));
            if (randFloat() > p) break;
            throughput /= p;
        }

        ro = shadowOrigin;
        rd = cosineWeightedHemisphere(hit.normal);
    }

    outColor  = vec4(color, 1.0);
    outNormal = vec4(octEncode(firstNormal), 0.0, 1.0);
    outDepth  = vec4(firstDepth, 0.0, 0.0, 1.0);
}
