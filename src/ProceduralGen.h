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

class ProceduralGen {
public:
    void generate(int gridSize, float noiseScale, float heightScale, int seed,
                  float emissiveDensity = 0.02f);
    void uploadTBO();
    GLuint getTBO() const { return tbo_; }
    GLuint getTBOTex() const { return tboTex_; }
    GLuint getBVHTBOTex() const { return bvhTboTex_; }
    int getCubeCount() const { return static_cast<int>(cubes_.size()); }
    int getBVHNodeCount() const { return static_cast<int>(bvhNodes_.size()); }

private:
    void buildBVH();
    int buildRecursive(int primStart, int primEnd);

    std::vector<CubeData> cubes_;
    std::vector<int> primIndices_;
    std::vector<BVHNodeGPU> bvhNodes_;

    GLuint tbo_       = 0;
    GLuint tboTex_    = 0;
    GLuint bvhTbo_    = 0;
    GLuint bvhTboTex_ = 0;
};
