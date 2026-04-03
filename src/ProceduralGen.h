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

struct TriangleData {
    glm::vec4 v0;       // xyz = vertex 0
    glm::vec4 v1;       // xyz = vertex 1
    glm::vec4 v2;       // xyz = vertex 2
    glm::vec4 albedo;   // rgb color
    glm::vec4 emission; // rgb emission
};

struct BVHNodeGPU {
    glm::vec4 d0; // bmin.xyz, w = intBitsToFloat(rightChildIdx or primStart)
    glm::vec4 d1; // bmax.xyz, w = intBitsToFloat(primCount); 0 = internal
};

static constexpr int BRIDGE_LENGTH        = 12;
static constexpr int BRIDGE_WIDTH         = 3;
static constexpr int BRIDGE_CLEARANCE_LEN = 8;
static constexpr int BRIDGE_CLEARANCE_WID = 4;

struct BridgeParams {
    bool enabled     = false;
    int  maxBridges  = 3;
    bool supports    = true;
};

class ProceduralGen {
public:
    void generate(int gridSize, float noiseScale, float heightScale, int seed,
                  float emissiveDensity = 0.02f,
                  const BridgeParams& bridgeParams = {});
    void uploadTBO();

    GLuint getCubeTBOTex()    const { return cubeTboTex_; }
    GLuint getCubeBVHTBOTex() const { return cubeBvhTboTex_; }
    int    getCubeCount()     const { return static_cast<int>(cubes_.size()); }

    GLuint getTriTBOTex()     const { return triTboTex_; }
    GLuint getTriBVHTBOTex()  const { return triBvhTboTex_; }
    int    getTriCount()      const { return static_cast<int>(triangles_.size()); }

private:
    struct TerrainCell {
        bool  active       = false;
        int   height       = 0;
        float smoothHeight = 0.0f;
    };

    struct BridgeSpan {
        glm::ivec2 start;
        glm::ivec2 end;
        int deckY     = 0;
        int gapLength = 0;
    };

    struct ClearanceZone {
        int minX, minZ, maxX, maxZ;
    };

    void sampleHeightField(int gridSize, float noiseScale, float heightScale, int seed);
    void emitTerrainMesh(int gridSize, float heightScale, int seed,
                         float emissiveDensity,
                         const std::vector<ClearanceZone>& clearanceZones);
    std::vector<BridgeSpan> placeBridgeSpans(int gridSize, int seed,
                                             const BridgeParams& params,
                                             std::vector<ClearanceZone>& zonesOut) const;
    void emitBridgeCubes(int gridSize, const std::vector<BridgeSpan>& spans,
                         const BridgeParams& params);

    void buildCubeBVH();
    void buildTriBVH();

    std::vector<TerrainCell> terrain_;
    std::vector<float> vertexHeights_;
    int gridSize_ = 0;

    std::vector<TriangleData> triangles_;
    std::vector<BVHNodeGPU> triBvhNodes_;

    std::vector<CubeData> cubes_;
    std::vector<BVHNodeGPU> cubeBvhNodes_;

    GLuint cubeTbo_       = 0;
    GLuint cubeTboTex_    = 0;
    GLuint cubeBvhTbo_    = 0;
    GLuint cubeBvhTboTex_ = 0;

    GLuint triTbo_       = 0;
    GLuint triTboTex_    = 0;
    GLuint triBvhTbo_    = 0;
    GLuint triBvhTboTex_ = 0;
};
