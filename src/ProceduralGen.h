#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>

struct CubeData {
    glm::vec4 bmin;     // xyz = min corner
    glm::vec4 bmax;     // xyz = max corner
    glm::vec4 albedo;   // rgb color
    glm::vec4 emission; // rgb base emission color, zero for non-emissive
};

struct BVHNodeGPU {
    glm::vec4 d0; // bmin.xyz, w = intBitsToFloat(rightChildIdx or primStart)
    glm::vec4 d1; // bmax.xyz, w = intBitsToFloat(primCount); 0 = internal
};

struct BridgeParams {
    bool  enabled    = false;
    int   maxBridges = 3;
    int   minSpan    = 3;
    int   maxSpan    = 8;
    int   width      = 1;
    bool  supports   = true;
};

class ProceduralGen {
public:
    void generate(int gridSize, float noiseScale, float heightScale, int seed,
                  float emissiveDensity = 0.02f,
                  const BridgeParams& bridgeParams = {});
    void uploadTBO();
    GLuint getTBO() const { return tbo_; }
    GLuint getTBOTex() const { return tboTex_; }
    GLuint getBVHTBOTex() const { return bvhTboTex_; }
    int getCubeCount() const { return static_cast<int>(cubes_.size()); }
    int getBVHNodeCount() const { return static_cast<int>(bvhNodes_.size()); }

private:
    struct TerrainCell {
        bool active = false;
        int  height = 0;
    };

    struct BridgeSpan {
        glm::ivec2 start;
        glm::ivec2 end;
        int deckY     = 0;
        int gapLength = 0;
    };

    void sampleHeightField(int gridSize, float noiseScale, float heightScale, int seed);
    void emitTerrainColumns(int gridSize, float heightScale, int seed, float emissiveDensity);
    std::vector<BridgeSpan> findBridgeSpans(int gridSize, int seed,
                                            const BridgeParams& params) const;
    void emitBridgeCubes(int gridSize, const std::vector<BridgeSpan>& spans,
                         const BridgeParams& params);
    void buildBVH();
    int buildRecursive(int primStart, int primEnd);

    std::vector<TerrainCell> terrain_;
    std::vector<CubeData> cubes_;
    std::vector<int> primIndices_;
    std::vector<BVHNodeGPU> bvhNodes_;

    GLuint tbo_       = 0;
    GLuint tboTex_    = 0;
    GLuint bvhTbo_    = 0;
    GLuint bvhTboTex_ = 0;
};
