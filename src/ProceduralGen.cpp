#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"
#include "ProceduralGen.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <numeric>

static constexpr int BVH_MAX_LEAF = 4;
static constexpr int SAH_BINS = 12;
static constexpr float BOUNDS_EPS = 1e-4f;

static float packIntAsFloat(int v) {
    float f;
    std::memcpy(&f, &v, sizeof(float));
    return f;
}

static float surfaceArea(glm::vec3 mn, glm::vec3 mx) {
    glm::vec3 d = mx - mn;
    return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
}

static uint32_t emissiveHash(int x, int z, int seed) {
    uint32_t h = uint32_t(x * 374761393 + z * 668265263 + seed * 1274126177);
    h = (h ^ (h >> 13)) * 1103515245u;
    h = h ^ (h >> 16);
    return h;
}

static uint32_t bridgeHash(int x0, int z0, int x1, int z1, int seed) {
    uint32_t h = uint32_t(x0 * 374761393u + z0 * 668265263u +
                          x1 * 1274126177u + z1 * 1103515245u +
                          seed * 2654435761u);
    h = (h ^ (h >> 13)) * 1103515245u;
    h = h ^ (h >> 16);
    return h;
}

// ---------------------------------------------------------------------------
// Generic SAH BVH builder operating on pre-computed AABB bounds
// ---------------------------------------------------------------------------

struct PrimBounds {
    glm::vec3 bmin, bmax;
};

static int buildBVHRecursive(
    std::vector<BVHNodeGPU>& nodes,
    std::vector<int>& primIndices,
    const std::vector<PrimBounds>& bounds,
    int primStart, int primEnd)
{
    int nodeIdx = static_cast<int>(nodes.size());
    nodes.push_back({});

    glm::vec3 bmin(1e30f), bmax(-1e30f);
    for (int i = primStart; i < primEnd; ++i) {
        int pi = primIndices[i];
        bmin = glm::min(bmin, bounds[pi].bmin);
        bmax = glm::max(bmax, bounds[pi].bmax);
    }

    int count = primEnd - primStart;

    if (count <= BVH_MAX_LEAF) {
        nodes[nodeIdx].d0 = glm::vec4(bmin, packIntAsFloat(primStart));
        nodes[nodeIdx].d1 = glm::vec4(bmax, packIntAsFloat(count));
        return nodeIdx;
    }

    glm::vec3 centroidMin(1e30f), centroidMax(-1e30f);
    for (int i = primStart; i < primEnd; ++i) {
        int pi = primIndices[i];
        glm::vec3 c = (bounds[pi].bmin + bounds[pi].bmax) * 0.5f;
        centroidMin = glm::min(centroidMin, c);
        centroidMax = glm::max(centroidMax, c);
    }

    int bestAxis = -1;
    float bestCost = 1e30f;
    int bestSplit = primStart;

    for (int axis = 0; axis < 3; ++axis) {
        float extent = centroidMax[axis] - centroidMin[axis];
        if (extent < 1e-6f) continue;

        struct Bin { glm::vec3 mn{1e30f}, mx{-1e30f}; int count = 0; };
        Bin bins[SAH_BINS];

        float invExtent = float(SAH_BINS) / extent;
        for (int i = primStart; i < primEnd; ++i) {
            int pi = primIndices[i];
            glm::vec3 c = (bounds[pi].bmin + bounds[pi].bmax) * 0.5f;
            int b = std::min(int((c[axis] - centroidMin[axis]) * invExtent), SAH_BINS - 1);
            bins[b].mn = glm::min(bins[b].mn, bounds[pi].bmin);
            bins[b].mx = glm::max(bins[b].mx, bounds[pi].bmax);
            bins[b].count++;
        }

        glm::vec3 leftMn[SAH_BINS - 1], leftMx[SAH_BINS - 1];
        int leftCount[SAH_BINS - 1];
        glm::vec3 rMn(1e30f), rMx(-1e30f);
        int rCount = 0;

        glm::vec3 lMn(1e30f), lMx(-1e30f);
        int lCount = 0;
        for (int i = 0; i < SAH_BINS - 1; ++i) {
            lMn = glm::min(lMn, bins[i].mn);
            lMx = glm::max(lMx, bins[i].mx);
            lCount += bins[i].count;
            leftMn[i] = lMn; leftMx[i] = lMx; leftCount[i] = lCount;
        }

        for (int i = SAH_BINS - 1; i >= 1; --i) {
            rMn = glm::min(rMn, bins[i].mn);
            rMx = glm::max(rMx, bins[i].mx);
            rCount += bins[i].count;

            if (leftCount[i - 1] == 0 || rCount == 0) continue;
            float cost = float(leftCount[i - 1]) * surfaceArea(leftMn[i - 1], leftMx[i - 1])
                       + float(rCount) * surfaceArea(rMn, rMx);
            if (cost < bestCost) {
                bestCost = cost;
                bestAxis = axis;
                bestSplit = i;
            }
        }
    }

    if (bestAxis == -1) {
        nodes[nodeIdx].d0 = glm::vec4(bmin, packIntAsFloat(primStart));
        nodes[nodeIdx].d1 = glm::vec4(bmax, packIntAsFloat(count));
        return nodeIdx;
    }

    float extent = centroidMax[bestAxis] - centroidMin[bestAxis];
    float invExtent = float(SAH_BINS) / extent;
    auto mid = std::partition(primIndices.begin() + primStart,
                              primIndices.begin() + primEnd,
                              [&](int pi) {
        glm::vec3 c = (bounds[pi].bmin + bounds[pi].bmax) * 0.5f;
        int b = std::min(int((c[bestAxis] - centroidMin[bestAxis]) * invExtent), SAH_BINS - 1);
        return b < bestSplit;
    });
    int midIdx = static_cast<int>(mid - primIndices.begin());

    if (midIdx == primStart || midIdx == primEnd) {
        nodes[nodeIdx].d0 = glm::vec4(bmin, packIntAsFloat(primStart));
        nodes[nodeIdx].d1 = glm::vec4(bmax, packIntAsFloat(count));
        return nodeIdx;
    }

    buildBVHRecursive(nodes, primIndices, bounds, primStart, midIdx);
    int rightChild = buildBVHRecursive(nodes, primIndices, bounds, midIdx, primEnd);

    nodes[nodeIdx].d0 = glm::vec4(bmin, packIntAsFloat(rightChild));
    nodes[nodeIdx].d1 = glm::vec4(bmax, packIntAsFloat(0));
    return nodeIdx;
}

// ---------------------------------------------------------------------------
// Stage 1 – sample Perlin noise into a vertex heightfield + coarse cell grid
// ---------------------------------------------------------------------------

void ProceduralGen::sampleHeightField(int gridSize, float noiseScale,
                                       float heightScale, int seed) {
    gridSize_ = gridSize;
    int vSize = gridSize + 1;
    vertexHeights_.resize(vSize * vSize);
    float offset = float(seed) * 17.31f;

    for (int vx = 0; vx < vSize; ++vx) {
        for (int vz = 0; vz < vSize; ++vz) {
            float nx = (float(vx) + offset) * noiseScale;
            float nz = (float(vz) + offset) * noiseScale;

            float noise  = stb_perlin_noise3(nx, 0.0f, nz, 0, 0, 0);
            float detail = stb_perlin_noise3(nx * 2.0f, 0.5f, nz * 2.0f, 0, 0, 0) * 0.5f;
            float combined = noise + detail;

            float h = 0.0f;
            if (combined >= -0.1f) {
                h = std::max(0.0f, (combined + 0.5f) * heightScale);
            }
            vertexHeights_[vz * vSize + vx] = h;
        }
    }

    terrain_.assign(gridSize * gridSize, {});
    for (int x = 0; x < gridSize; ++x) {
        for (int z = 0; z < gridSize; ++z) {
            float h = vertexHeights_[z * vSize + x];
            TerrainCell& cell = terrain_[z * gridSize + x];
            if (h < 0.01f) {
                cell.active = false;
                cell.height = 0;
                cell.smoothHeight = 0.0f;
            } else {
                cell.active = true;
                cell.smoothHeight = h;
                cell.height = std::max(1, static_cast<int>(h));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Stage 2 – emit terrain triangle mesh from vertex heightfield
// ---------------------------------------------------------------------------

void ProceduralGen::emitTerrainMesh(int gridSize, float heightScale, int seed,
                                     float emissiveDensity,
                                     const std::vector<ClearanceZone>& clearanceZones) {
    float halfGrid = float(gridSize) / 2.0f;
    int vSize = gridSize + 1;
    uint32_t densityThreshold = uint32_t(emissiveDensity * 4294967295.0f);

    auto vtxH = [&](int vx, int vz) -> float {
        return vertexHeights_[vz * vSize + vx];
    };

    auto worldPos = [&](int vx, int vz) -> glm::vec3 {
        return glm::vec3(float(vx) - halfGrid, vtxH(vx, vz), float(vz) - halfGrid);
    };

    std::vector<bool> cellActive(gridSize * gridSize, false);
    for (int x = 0; x < gridSize; ++x) {
        for (int z = 0; z < gridSize; ++z) {
            float h00 = vtxH(x, z);
            float h10 = vtxH(x + 1, z);
            float h01 = vtxH(x, z + 1);
            float h11 = vtxH(x + 1, z + 1);
            bool hasHeight = h00 > 0.01f || h10 > 0.01f || h01 > 0.01f || h11 > 0.01f;

            bool inClearance = false;
            for (const auto& zone : clearanceZones) {
                if (x >= zone.minX && x <= zone.maxX &&
                    z >= zone.minZ && z <= zone.maxZ) {
                    inClearance = true;
                    break;
                }
            }
            cellActive[z * gridSize + x] = hasHeight && !inClearance;
        }
    }

    auto isCellActive = [&](int cx, int cz) -> bool {
        if (cx < 0 || cx >= gridSize || cz < 0 || cz >= gridSize) return false;
        return cellActive[cz * gridSize + cx];
    };

    glm::vec4 wallColor(0.50f, 0.38f, 0.25f, 1.0f);
    glm::vec4 noEmission(0.0f);

    for (int x = 0; x < gridSize; ++x) {
        for (int z = 0; z < gridSize; ++z) {
            if (!isCellActive(x, z)) continue;

            glm::vec3 p00 = worldPos(x, z);
            glm::vec3 p10 = worldPos(x + 1, z);
            glm::vec3 p01 = worldPos(x, z + 1);
            glm::vec3 p11 = worldPos(x + 1, z + 1);

            float avgH = (p00.y + p10.y + p01.y + p11.y) * 0.25f;
            float t = avgH / std::max(1.0f, heightScale);
            float grassR = 0.18f + 0.12f * t;
            float grassG = 0.58f + 0.22f * t;
            float grassB = 0.12f + 0.08f * t;
            glm::vec4 color(grassR, grassG, grassB, 1.0f);

            if (avgH < 1.0f) {
                glm::vec3 dirt(0.50f, 0.32f, 0.18f);
                float blend = std::max(0.0f, std::min(1.0f, avgH));
                color = glm::vec4(glm::mix(dirt, glm::vec3(color), blend), 1.0f);
            }

            glm::vec4 emission(0.0f);
            if (emissiveHash(x, z, seed) < densityThreshold) {
                emission = glm::vec4(1.0f, 0.7f, 0.3f, 1.0f);
            }

            TriangleData tri1{};
            tri1.v0 = glm::vec4(p00, 0.0f);
            tri1.v1 = glm::vec4(p10, 0.0f);
            tri1.v2 = glm::vec4(p11, 0.0f);
            tri1.albedo = color;
            tri1.emission = emission;
            triangles_.push_back(tri1);

            TriangleData tri2{};
            tri2.v0 = glm::vec4(p00, 0.0f);
            tri2.v1 = glm::vec4(p11, 0.0f);
            tri2.v2 = glm::vec4(p01, 0.0f);
            tri2.albedo = color;
            tri2.emission = emission;
            triangles_.push_back(tri2);

            // Side walls where terrain meets an inactive neighbour or grid edge.
            // Each wall is a vertical quad from the surface edge down to y=0.
            struct WallEdge {
                int vx0, vz0, vx1, vz1;   // edge vertex indices
                int nx, nz;                // neighbour cell offset to check
            };
            WallEdge edges[] = {
                {x,     z,     x,     z + 1, -1,  0},  // left  (-x)
                {x + 1, z,     x + 1, z + 1,  1,  0},  // right (+x)
                {x,     z,     x + 1, z,      0, -1},  // front (-z)
                {x,     z + 1, x + 1, z + 1,  0,  1},  // back  (+z)
            };

            for (const auto& e : edges) {
                if (isCellActive(x + e.nx, z + e.nz)) continue;

                glm::vec3 top0 = worldPos(e.vx0, e.vz0);
                glm::vec3 top1 = worldPos(e.vx1, e.vz1);
                if (top0.y < 0.01f && top1.y < 0.01f) continue;

                glm::vec3 bot0(top0.x, 0.0f, top0.z);
                glm::vec3 bot1(top1.x, 0.0f, top1.z);

                TriangleData w1{};
                w1.v0 = glm::vec4(top0, 0.0f);
                w1.v1 = glm::vec4(bot0, 0.0f);
                w1.v2 = glm::vec4(bot1, 0.0f);
                w1.albedo = wallColor;
                w1.emission = noEmission;
                triangles_.push_back(w1);

                TriangleData w2{};
                w2.v0 = glm::vec4(top0, 0.0f);
                w2.v1 = glm::vec4(bot1, 0.0f);
                w2.v2 = glm::vec4(top1, 0.0f);
                w2.albedo = wallColor;
                w2.emission = noEmission;
                triangles_.push_back(w2);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Stage 3 – place fixed-size bridges and compute clearance zones (unchanged)
// ---------------------------------------------------------------------------

std::vector<ProceduralGen::BridgeSpan> ProceduralGen::placeBridgeSpans(
    int gridSize, int seed, const BridgeParams& params,
    std::vector<ClearanceZone>& zonesOut) const
{
    auto cellAt = [&](int x, int z) -> const TerrainCell& {
        return terrain_[z * gridSize + x];
    };

    std::vector<BridgeSpan> candidates;
    constexpr int dirs[][2] = {{1, 0}, {0, 1}};
    constexpr int halfW = (BRIDGE_WIDTH - 1) / 2;
    constexpr int halfCW = (BRIDGE_CLEARANCE_WID - 1) / 2;

    for (int x = 0; x < gridSize; ++x) {
        for (int z = 0; z < gridSize; ++z) {
            const auto& src = cellAt(x, z);
            if (!src.active || src.height < 2) continue;

            for (const auto& dir : dirs) {
                int dx = dir[0];
                int dz = dir[1];
                int perpDx = dz;
                int perpDz = dx;

                int endX = x + dx * (BRIDGE_LENGTH + 1);
                int endZ = z + dz * (BRIDGE_LENGTH + 1);
                if (endX < 0 || endX >= gridSize || endZ < 0 || endZ >= gridSize)
                    continue;

                const auto& dst = cellAt(endX, endZ);
                if (!dst.active || dst.height < 2) continue;
                if (std::abs(dst.height - src.height) > 2) continue;

                int clearStartX = x - dx * BRIDGE_CLEARANCE_LEN;
                int clearStartZ = z - dz * BRIDGE_CLEARANCE_LEN;
                int clearEndX   = endX + dx * BRIDGE_CLEARANCE_LEN;
                int clearEndZ   = endZ + dz * BRIDGE_CLEARANCE_LEN;

                int perpMin = -halfCW;
                int perpMax =  halfCW;

                bool fits = true;
                int checkPoints[][2] = {
                    {x + perpDx * perpMin, z + perpDz * perpMin},
                    {x + perpDx * perpMax, z + perpDz * perpMax},
                    {endX + perpDx * perpMin, endZ + perpDz * perpMin},
                    {endX + perpDx * perpMax, endZ + perpDz * perpMax},
                    {clearStartX + perpDx * perpMin, clearStartZ + perpDz * perpMin},
                    {clearStartX + perpDx * perpMax, clearStartZ + perpDz * perpMax},
                    {clearEndX + perpDx * perpMin, clearEndZ + perpDz * perpMin},
                    {clearEndX + perpDx * perpMax, clearEndZ + perpDz * perpMax},
                };
                for (const auto& pt : checkPoints) {
                    if (pt[0] < 0 || pt[0] >= gridSize || pt[1] < 0 || pt[1] >= gridSize) {
                        fits = false;
                        break;
                    }
                }
                if (!fits) continue;

                int deckY = std::min(src.height, dst.height) - 1;

                BridgeSpan span;
                span.start     = {x, z};
                span.end       = {endX, endZ};
                span.deckY     = deckY;
                span.gapLength = BRIDGE_LENGTH;
                candidates.push_back(span);
            }
        }
    }

    if (static_cast<int>(candidates.size()) <= params.maxBridges) {
        // keep all
    } else {
        std::vector<std::pair<uint32_t, int>> scored;
        scored.reserve(candidates.size());
        for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
            const auto& s = candidates[i];
            scored.push_back({bridgeHash(s.start.x, s.start.y,
                                         s.end.x, s.end.y, seed), i});
        }
        std::sort(scored.begin(), scored.end());

        std::vector<bool> occupied(gridSize * gridSize, false);
        std::vector<BridgeSpan> selected;

        for (const auto& entry : scored) {
            if (static_cast<int>(selected.size()) >= params.maxBridges) break;

            const auto& span = candidates[entry.second];
            int ddx = (span.end.x > span.start.x) ? 1 : 0;
            int ddz = (span.end.y > span.start.y) ? 1 : 0;

            bool overlaps = false;
            for (int i = 1; i <= span.gapLength; ++i) {
                int cx = span.start.x + ddx * i;
                int cz = span.start.y + ddz * i;
                if (occupied[cz * gridSize + cx]) { overlaps = true; break; }
            }
            if (overlaps) continue;

            for (int i = 1; i <= span.gapLength; ++i) {
                int cx = span.start.x + ddx * i;
                int cz = span.start.y + ddz * i;
                occupied[cz * gridSize + cx] = true;
            }
            selected.push_back(span);
        }
        candidates = std::move(selected);
    }

    zonesOut.clear();
    for (const auto& span : candidates) {
        int dx = (span.end.x != span.start.x) ? 1 : 0;
        int dz = (span.end.y != span.start.y) ? 1 : 0;
        int perpDx = dz;
        int perpDz = dx;

        auto makeZone = [&](int originX, int originZ, int dirX, int dirZ) {
            int x0 = originX;
            int z0 = originZ;
            int x1 = originX + dirX * (BRIDGE_CLEARANCE_LEN - 1);
            int z1 = originZ + dirZ * (BRIDGE_CLEARANCE_LEN - 1);

            int pw0 = -((BRIDGE_CLEARANCE_WID - 1) / 2);
            int pw1 =   (BRIDGE_CLEARANCE_WID - 1) / 2;

            int minX = std::min({x0 + perpDx * pw0, x0 + perpDx * pw1,
                                 x1 + perpDx * pw0, x1 + perpDx * pw1});
            int maxX = std::max({x0 + perpDx * pw0, x0 + perpDx * pw1,
                                 x1 + perpDx * pw0, x1 + perpDx * pw1});
            int minZ = std::min({z0 + perpDz * pw0, z0 + perpDz * pw1,
                                 z1 + perpDz * pw0, z1 + perpDz * pw1});
            int maxZ = std::max({z0 + perpDz * pw0, z0 + perpDz * pw1,
                                 z1 + perpDz * pw0, z1 + perpDz * pw1});

            zonesOut.push_back({minX, minZ, maxX, maxZ});
        };

        makeZone(span.start.x - dx, span.start.y - dz, -dx, -dz);
        makeZone(span.end.x + dx, span.end.y + dz, dx, dz);
    }

    return candidates;
}

// ---------------------------------------------------------------------------
// Stage 4 – emit deck and support cubes for each selected bridge span
// ---------------------------------------------------------------------------

void ProceduralGen::emitBridgeCubes(int gridSize,
                                     const std::vector<BridgeSpan>& spans,
                                     const BridgeParams& params) {
    float halfGrid = float(gridSize) / 2.0f;

    constexpr int widthStart = -(BRIDGE_WIDTH - 1) / 2;
    constexpr int widthEnd   =  (BRIDGE_WIDTH - 1) / 2;

    for (const auto& span : spans) {
        int dx = (span.end.x != span.start.x) ? 1 : 0;
        int dz = (span.end.y != span.start.y) ? 1 : 0;
        int perpDx = dz;
        int perpDz = dx;

        for (int i = 1; i <= BRIDGE_LENGTH; ++i) {
            int baseCx = span.start.x + dx * i;
            int baseCz = span.start.y + dz * i;

            for (int w = widthStart; w <= widthEnd; ++w) {
                int cx = baseCx + perpDx * w;
                int cz = baseCz + perpDz * w;
                if (cx < 0 || cx >= gridSize || cz < 0 || cz >= gridSize)
                    continue;

                CubeData deck{};
                float wx = float(cx) - halfGrid;
                float wz = float(cz) - halfGrid;
                deck.bmin    = glm::vec4(wx, float(span.deckY), wz, 0.0f);
                deck.bmax    = glm::vec4(wx + 1.0f, float(span.deckY) + 0.4f,
                                         wz + 1.0f, 0.0f);
                deck.albedo  = glm::vec4(0.55f, 0.58f, 0.62f, 1.0f);
                deck.emission = glm::vec4(0.0f);

                bool isEndRow = (i == 1 || i == BRIDGE_LENGTH);
                bool isEdge   = (w != 0);
                if (isEndRow && isEdge) {
                    deck.emission = glm::vec4(0.0f, 1.0f, 0.2f, 1.0f);
                }

                cubes_.push_back(deck);
            }
        }

        if (!params.supports) continue;

        int supportPositions[] = {1, BRIDGE_LENGTH};
        for (int sp : supportPositions) {
            int sx = span.start.x + dx * sp;
            int sz = span.start.y + dz * sp;
            if (sx < 0 || sx >= gridSize || sz < 0 || sz >= gridSize)
                continue;

            const auto& cell = terrain_[sz * gridSize + sx];
            int base = cell.active ? cell.height : 0;

            for (int y = base; y < span.deckY; ++y) {
                CubeData support{};
                float wx = float(sx) - halfGrid;
                float wz = float(sz) - halfGrid;
                support.bmin    = glm::vec4(wx + 0.25f, float(y), wz + 0.25f, 0.0f);
                support.bmax    = glm::vec4(wx + 0.75f, float(y + 1),
                                            wz + 0.75f, 0.0f);
                support.albedo  = glm::vec4(0.42f, 0.45f, 0.50f, 1.0f);
                support.emission = glm::vec4(0.0f);
                cubes_.push_back(support);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Top-level generate – orchestrates the pipeline stages
// ---------------------------------------------------------------------------

void ProceduralGen::generate(int gridSize, float noiseScale, float heightScale,
                             int seed, float emissiveDensity,
                             const BridgeParams& bridgeParams) {
    cubes_.clear();
    triangles_.clear();

    sampleHeightField(gridSize, noiseScale, heightScale, seed);

    std::vector<ClearanceZone> zones;
    std::vector<BridgeSpan> spans;
    if (bridgeParams.enabled) {
        spans = placeBridgeSpans(gridSize, seed, bridgeParams, zones);
    }

    emitTerrainMesh(gridSize, heightScale, seed, emissiveDensity, zones);

    if (bridgeParams.enabled) {
        emitBridgeCubes(gridSize, spans, bridgeParams);
    }

    buildTriBVH();
    buildCubeBVH();
}

// ---------------------------------------------------------------------------
// BVH construction wrappers
// ---------------------------------------------------------------------------

void ProceduralGen::buildTriBVH() {
    int n = static_cast<int>(triangles_.size());
    triBvhNodes_.clear();
    if (n == 0) return;

    std::vector<PrimBounds> bounds(n);
    for (int i = 0; i < n; ++i) {
        glm::vec3 v0(triangles_[i].v0), v1(triangles_[i].v1), v2(triangles_[i].v2);
        bounds[i].bmin = glm::min(glm::min(v0, v1), v2) - glm::vec3(BOUNDS_EPS);
        bounds[i].bmax = glm::max(glm::max(v0, v1), v2) + glm::vec3(BOUNDS_EPS);
    }

    std::vector<int> primIndices(n);
    std::iota(primIndices.begin(), primIndices.end(), 0);
    triBvhNodes_.reserve(2 * n);
    buildBVHRecursive(triBvhNodes_, primIndices, bounds, 0, n);

    std::vector<TriangleData> reordered(n);
    for (int i = 0; i < n; ++i) {
        reordered[i] = triangles_[primIndices[i]];
    }
    triangles_ = std::move(reordered);
}

void ProceduralGen::buildCubeBVH() {
    int n = static_cast<int>(cubes_.size());
    cubeBvhNodes_.clear();
    if (n == 0) return;

    std::vector<PrimBounds> bounds(n);
    for (int i = 0; i < n; ++i) {
        bounds[i].bmin = glm::vec3(cubes_[i].bmin);
        bounds[i].bmax = glm::vec3(cubes_[i].bmax);
    }

    std::vector<int> primIndices(n);
    std::iota(primIndices.begin(), primIndices.end(), 0);
    cubeBvhNodes_.reserve(2 * n);
    buildBVHRecursive(cubeBvhNodes_, primIndices, bounds, 0, n);

    std::vector<CubeData> reordered(n);
    for (int i = 0; i < n; ++i) {
        reordered[i] = cubes_[primIndices[i]];
    }
    cubes_ = std::move(reordered);
}

// ---------------------------------------------------------------------------
// GPU upload – separate TBOs for terrain triangles and bridge cubes
// ---------------------------------------------------------------------------

static void uploadBuffer(GLuint& bo, GLuint& tex, const void* data, size_t bytes) {
    if (bo == 0) {
        glGenBuffers(1, &bo);
        glGenTextures(1, &tex);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, bo);
    glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(bytes),
                 bytes > 0 ? data : nullptr, GL_STATIC_DRAW);
    glBindTexture(GL_TEXTURE_BUFFER, tex);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, bo);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void ProceduralGen::uploadTBO() {
    uploadBuffer(cubeTbo_, cubeTboTex_,
                 cubes_.data(), cubes_.size() * sizeof(CubeData));
    uploadBuffer(cubeBvhTbo_, cubeBvhTboTex_,
                 cubeBvhNodes_.data(), cubeBvhNodes_.size() * sizeof(BVHNodeGPU));
    uploadBuffer(triTbo_, triTboTex_,
                 triangles_.data(), triangles_.size() * sizeof(TriangleData));
    uploadBuffer(triBvhTbo_, triBvhTboTex_,
                 triBvhNodes_.data(), triBvhNodes_.size() * sizeof(BVHNodeGPU));
}
