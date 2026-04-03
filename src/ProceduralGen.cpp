#define STB_PERLIN_IMPLEMENTATION
#include "stb_perlin.h"
#include "ProceduralGen.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <numeric>

static constexpr int BVH_MAX_LEAF = 4;
static constexpr int SAH_BINS = 12;

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
// Stage 1 – sample Perlin noise into a 2D terrain grid
// ---------------------------------------------------------------------------

void ProceduralGen::sampleHeightField(int gridSize, float noiseScale,
                                       float heightScale, int seed) {
    terrain_.assign(gridSize * gridSize, {});
    float offset = float(seed) * 17.31f;

    for (int x = 0; x < gridSize; ++x) {
        for (int z = 0; z < gridSize; ++z) {
            float nx = (float(x) + offset) * noiseScale;
            float nz = (float(z) + offset) * noiseScale;

            float noise  = stb_perlin_noise3(nx, 0.0f, nz, 0, 0, 0);
            float detail = stb_perlin_noise3(nx * 2.0f, 0.5f, nz * 2.0f, 0, 0, 0) * 0.5f;
            float combined = noise + detail;

            TerrainCell& cell = terrain_[z * gridSize + x];
            if (combined < -0.1f) {
                cell.active = false;
                cell.height = 0;
            } else {
                cell.active = true;
                cell.height = std::max(1, int((combined + 0.5f) * heightScale));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Stage 2 – emit terrain column cubes from the sampled grid
// ---------------------------------------------------------------------------

void ProceduralGen::emitTerrainColumns(int gridSize, float heightScale,
                                        int seed, float emissiveDensity) {
    float halfGrid = float(gridSize) / 2.0f;
    uint32_t densityThreshold = uint32_t(emissiveDensity * 4294967295.0f);

    for (int x = 0; x < gridSize; ++x) {
        for (int z = 0; z < gridSize; ++z) {
            const TerrainCell& cell = terrain_[z * gridSize + x];
            if (!cell.active) continue;

            int height = cell.height;
            bool isEmissiveColumn = emissiveHash(x, z, seed) < densityThreshold;

            for (int y = 0; y < height; ++y) {
                CubeData cube{};
                float wx = float(x) - halfGrid;
                float wz = float(z) - halfGrid;
                float wy = float(y);

                cube.bmin = glm::vec4(wx, wy, wz, 0.0f);
                cube.bmax = glm::vec4(wx + 1.0f, wy + 1.0f, wz + 1.0f, 0.0f);

                float t = float(y) / std::max(1.0f, heightScale);
                float grassR = 0.18f + 0.12f * t;
                float grassG = 0.58f + 0.22f * t;
                float grassB = 0.12f + 0.08f * t;

                if (y == 0) {
                    cube.albedo = glm::vec4(0.50f, 0.32f, 0.18f, 1.0f);
                } else if (y == height - 1) {
                    cube.albedo = glm::vec4(grassR, grassG, grassB, 1.0f);
                } else {
                    cube.albedo = glm::vec4(0.55f, 0.38f, 0.22f, 1.0f);
                }

                cube.emission = glm::vec4(0.0f);
                if (isEmissiveColumn && y == height - 1) {
                    cube.emission = glm::vec4(1.0f, 0.7f, 0.3f, 1.0f);
                }

                cubes_.push_back(cube);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Stage 3 – analyse the terrain grid to find bridge candidates
// ---------------------------------------------------------------------------

std::vector<ProceduralGen::BridgeSpan> ProceduralGen::findBridgeSpans(
    int gridSize, int seed, const BridgeParams& params) const
{
    auto cellAt = [&](int x, int z) -> const TerrainCell& {
        return terrain_[z * gridSize + x];
    };

    std::vector<BridgeSpan> candidates;
    constexpr int dirs[][2] = {{1, 0}, {0, 1}};

    for (int x = 0; x < gridSize; ++x) {
        for (int z = 0; z < gridSize; ++z) {
            const auto& src = cellAt(x, z);
            if (!src.active || src.height < 2) continue;

            for (const auto& dir : dirs) {
                int nx = x + dir[0];
                int nz = z + dir[1];
                if (nx >= gridSize || nz >= gridSize) continue;

                const auto& neighbor = cellAt(nx, nz);
                bool isCliffEdge = !neighbor.active ||
                                   neighbor.height < src.height - 1;
                if (!isCliffEdge) continue;

                for (int step = 2; step <= params.maxSpan + 1; ++step) {
                    int px = x + dir[0] * step;
                    int pz = z + dir[1] * step;
                    if (px >= gridSize || pz >= gridSize) break;

                    const auto& probe = cellAt(px, pz);

                    if (!probe.active) continue;

                    // Active but very short relative to source: treat as gap
                    if (probe.height < src.height - 2) continue;

                    // Potential anchor or blocker – stop scanning either way
                    int gapLen = step - 1;
                    if (gapLen < params.minSpan || gapLen > params.maxSpan ||
                        std::abs(probe.height - src.height) > 2 ||
                        probe.height < 2) {
                        break;
                    }

                    int deckY = std::min(src.height, probe.height) - 1;

                    bool clearanceOk = true;
                    for (int c = 1; c < step; ++c) {
                        int cx = x + dir[0] * c;
                        int cz = z + dir[1] * c;
                        const auto& mid = cellAt(cx, cz);
                        if (mid.active && mid.height >= deckY) {
                            clearanceOk = false;
                            break;
                        }
                    }

                    if (clearanceOk) {
                        BridgeSpan span;
                        span.start     = {x, z};
                        span.end       = {px, pz};
                        span.deckY     = deckY;
                        span.gapLength = gapLen;
                        candidates.push_back(span);
                    }
                    break;
                }
            }
        }
    }

    // If few enough candidates, keep them all
    if (static_cast<int>(candidates.size()) <= params.maxBridges)
        return candidates;

    // Deterministic selection via hashing + overlap rejection
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

    return selected;
}

// ---------------------------------------------------------------------------
// Stage 4 – emit deck and support cubes for each selected bridge span
// ---------------------------------------------------------------------------

void ProceduralGen::emitBridgeCubes(int gridSize,
                                     const std::vector<BridgeSpan>& spans,
                                     const BridgeParams& params) {
    float halfGrid = float(gridSize) / 2.0f;

    int widthStart = -(params.width - 1) / 2;
    int widthEnd   =  params.width / 2;

    for (const auto& span : spans) {
        int dx = (span.end.x != span.start.x) ? 1 : 0;
        int dz = (span.end.y != span.start.y) ? 1 : 0;
        int perpDx = dz;
        int perpDz = dx;

        // Deck
        size_t deckStart = cubes_.size();
        for (int i = 1; i <= span.gapLength; ++i) {
            int baseCx = span.start.x + dx * i;
            int baseCz = span.start.y + dz * i;

            for (int w = widthStart; w <= widthEnd; ++w) {
                int cx = baseCx + perpDx * w;
                int cz = baseCz + perpDz * w;
                if (cx < 0 || cx >= gridSize || cz < 0 || cz >= gridSize)
                    continue;

                const auto& cell = terrain_[cz * gridSize + cx];
                if (cell.active && cell.height > span.deckY) continue;

                CubeData deck{};
                float wx = float(cx) - halfGrid;
                float wz = float(cz) - halfGrid;
                deck.bmin    = glm::vec4(wx, float(span.deckY), wz, 0.0f);
                deck.bmax    = glm::vec4(wx + 1.0f, float(span.deckY) + 0.4f,
                                         wz + 1.0f, 0.0f);
                deck.albedo  = glm::vec4(0.55f, 0.58f, 0.62f, 1.0f);
                deck.emission = glm::vec4(0.0f);
                cubes_.push_back(deck);
            }
        }

        size_t deckEnd = cubes_.size();
        if (deckEnd > deckStart) {
            cubes_[deckStart].emission     = glm::vec4(0.0f, 1.0f, 0.2f, 1.0f);
            cubes_[deckEnd - 1].emission   = glm::vec4(0.0f, 1.0f, 0.2f, 1.0f);
        }

        if (!params.supports) continue;

        // Vertical supports at the first and last gap positions
        int supportPositions[] = {1, span.gapLength};
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

    sampleHeightField(gridSize, noiseScale, heightScale, seed);
    emitTerrainColumns(gridSize, heightScale, seed, emissiveDensity);

    if (bridgeParams.enabled) {
        auto spans = findBridgeSpans(gridSize, seed, bridgeParams);
        emitBridgeCubes(gridSize, spans, bridgeParams);
    }

    buildBVH();
}

// ---------------------------------------------------------------------------
// BVH construction (unchanged)
// ---------------------------------------------------------------------------

int ProceduralGen::buildRecursive(int primStart, int primEnd) {
    int nodeIdx = static_cast<int>(bvhNodes_.size());
    bvhNodes_.push_back({});

    glm::vec3 bmin(1e30f), bmax(-1e30f);
    for (int i = primStart; i < primEnd; ++i) {
        int pi = primIndices_[i];
        bmin = glm::min(bmin, glm::vec3(cubes_[pi].bmin));
        bmax = glm::max(bmax, glm::vec3(cubes_[pi].bmax));
    }

    int count = primEnd - primStart;

    if (count <= BVH_MAX_LEAF) {
        bvhNodes_[nodeIdx].d0 = glm::vec4(bmin, packIntAsFloat(primStart));
        bvhNodes_[nodeIdx].d1 = glm::vec4(bmax, packIntAsFloat(count));
        return nodeIdx;
    }

    glm::vec3 centroidMin(1e30f), centroidMax(-1e30f);
    for (int i = primStart; i < primEnd; ++i) {
        int pi = primIndices_[i];
        glm::vec3 c = (glm::vec3(cubes_[pi].bmin) + glm::vec3(cubes_[pi].bmax)) * 0.5f;
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
            int pi = primIndices_[i];
            glm::vec3 c = (glm::vec3(cubes_[pi].bmin) + glm::vec3(cubes_[pi].bmax)) * 0.5f;
            int b = std::min(int((c[axis] - centroidMin[axis]) * invExtent), SAH_BINS - 1);
            bins[b].mn = glm::min(bins[b].mn, glm::vec3(cubes_[pi].bmin));
            bins[b].mx = glm::max(bins[b].mx, glm::vec3(cubes_[pi].bmax));
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
        bvhNodes_[nodeIdx].d0 = glm::vec4(bmin, packIntAsFloat(primStart));
        bvhNodes_[nodeIdx].d1 = glm::vec4(bmax, packIntAsFloat(count));
        return nodeIdx;
    }

    float extent = centroidMax[bestAxis] - centroidMin[bestAxis];
    float invExtent = float(SAH_BINS) / extent;
    auto mid = std::partition(primIndices_.begin() + primStart,
                              primIndices_.begin() + primEnd,
                              [&](int pi) {
        glm::vec3 c = (glm::vec3(cubes_[pi].bmin) + glm::vec3(cubes_[pi].bmax)) * 0.5f;
        int b = std::min(int((c[bestAxis] - centroidMin[bestAxis]) * invExtent), SAH_BINS - 1);
        return b < bestSplit;
    });
    int midIdx = static_cast<int>(mid - primIndices_.begin());

    if (midIdx == primStart || midIdx == primEnd) {
        bvhNodes_[nodeIdx].d0 = glm::vec4(bmin, packIntAsFloat(primStart));
        bvhNodes_[nodeIdx].d1 = glm::vec4(bmax, packIntAsFloat(count));
        return nodeIdx;
    }

    buildRecursive(primStart, midIdx);
    int rightChild = buildRecursive(midIdx, primEnd);

    bvhNodes_[nodeIdx].d0 = glm::vec4(bmin, packIntAsFloat(rightChild));
    bvhNodes_[nodeIdx].d1 = glm::vec4(bmax, packIntAsFloat(0));
    return nodeIdx;
}

void ProceduralGen::buildBVH() {
    int n = static_cast<int>(cubes_.size());
    primIndices_.resize(n);
    std::iota(primIndices_.begin(), primIndices_.end(), 0);
    bvhNodes_.clear();
    bvhNodes_.reserve(2 * n);

    if (n > 0) {
        buildRecursive(0, n);
    }

    std::vector<CubeData> reordered(n);
    for (int i = 0; i < n; ++i) {
        reordered[i] = cubes_[primIndices_[i]];
    }
    cubes_ = std::move(reordered);
}

// ---------------------------------------------------------------------------
// GPU upload (unchanged)
// ---------------------------------------------------------------------------

void ProceduralGen::uploadTBO() {
    if (tbo_ == 0) {
        glGenBuffers(1, &tbo_);
        glGenTextures(1, &tboTex_);
    }

    glBindBuffer(GL_TEXTURE_BUFFER, tbo_);
    glBufferData(GL_TEXTURE_BUFFER,
                 cubes_.size() * sizeof(CubeData),
                 cubes_.data(),
                 GL_STATIC_DRAW);

    glBindTexture(GL_TEXTURE_BUFFER, tboTex_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, tbo_);

    glBindBuffer(GL_TEXTURE_BUFFER, 0);

    if (bvhTbo_ == 0) {
        glGenBuffers(1, &bvhTbo_);
        glGenTextures(1, &bvhTboTex_);
    }

    glBindBuffer(GL_TEXTURE_BUFFER, bvhTbo_);
    glBufferData(GL_TEXTURE_BUFFER,
                 bvhNodes_.size() * sizeof(BVHNodeGPU),
                 bvhNodes_.data(),
                 GL_STATIC_DRAW);

    glBindTexture(GL_TEXTURE_BUFFER, bvhTboTex_);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, bvhTbo_);

    glBindBuffer(GL_TEXTURE_BUFFER, 0);
}
