#include "terraingenerator.h"

#include <cmath>
#include <iostream>
#include <ostream>
#include "glm.hpp"



// Constructor
TerrainGenerator::TerrainGenerator()
{
    m_noiseMapSize = 200 ;
    m_cellSize = 25;
    translation = glm::vec3(0.0, 0.0, 0.0);
    m_xScale = 5.0;
    m_yScale = 2.0;
}

// Destructor
TerrainGenerator::~TerrainGenerator(){}

// Helper for generateTerrain()
void addPointToVector(glm::vec3 point, std::vector<float>& vector) {
    vector.push_back(point.x);
    vector.push_back(point.z);
    vector.push_back(point.y);
}


// scaling version zhou
void TerrainGenerator::generateTerrain() {
    auto perlinGen = Perlin(m_cellSize, m_noiseMapSize);
    auto noiseMap = perlinGen.formNoiseMap();

//    for (auto &v : noiseMap)
//        v = v / 3;

    // get height map
    height_data = noiseMap;

    // get xz map
    xz_data.reserve(m_xScale * m_noiseMapSize * m_yScale * m_noiseMapSize * 6);
    for(int x = 0; x < m_xScale * m_noiseMapSize; x++) {
        for(int z = 0; z < m_yScale * m_noiseMapSize; z++) {
            int x1 = x;
            int z1 = z;
            int x2 = x + 1;
            int z2 = z + 1;

            glm::vec3 p1 = getPosition(x1, z1);
            glm::vec3 p2 = getPosition(x2, z1);
            glm::vec3 p3 = getPosition(x2, z2);
            glm::vec3 p4 = getPosition(x1, z2);

            // push p3: [x2, z2]
            xz_data.push_back(p3.x);
            xz_data.push_back(p3.y);
            // push p2: [x2, z1]
            xz_data.push_back(p2.x);
            xz_data.push_back(p2.y);
            // push p1: [x1, z1]
            xz_data.push_back(p1.x);
            xz_data.push_back(p1.y);

            // push p4: [x1, z2]
            xz_data.push_back(p4.x);
            xz_data.push_back(p4.y);
            // push p3: [x2, z2]
            xz_data.push_back(p3.x);
            xz_data.push_back(p3.y);
            // push p1: [x1, z1]
            xz_data.push_back(p1.x);
            xz_data.push_back(p1.y);

            if (x<m_noiseMapSize && z<m_noiseMapSize) {
                glm::vec3 n1 = getNormal(x1, z1);
                glm::vec3 c1 = getColor(n1, p1);
                addPointToVector(n1, normal_data);
                addPointToVector(c1, color_data);
            }

        }
    }
}


glm::vec3 TerrainGenerator::getPosition(int row, int col) {
    float x = 1.0 * row / m_noiseMapSize ;
    float y = 1.0 * col / m_noiseMapSize ;
    float z = getHeight(row, col);
    return glm::vec3(x,y,z);
}


void TerrainGenerator::setMxMy(float x, float y) {
    m_xScale = x;
    m_yScale = y;

}
void TerrainGenerator::setTranslation(glm::vec3 trans) {
    translation = glm::vec3(trans[0], trans[2], trans[1]);
}


float TerrainGenerator::getHeight(int row, int col) {
    int modRow = (row+m_noiseMapSize)%m_noiseMapSize;
    int modCol = (col+m_noiseMapSize)%m_noiseMapSize;
    int index = modRow*m_noiseMapSize + modCol;
    return height_data[index];
}

// Computes the normal of a vertex by averaging neighbors
glm::vec3 TerrainGenerator::getNormal(int row, int col) {
    glm::vec3 normal = glm::vec3(0, 0, 0);
    std::vector<std::vector<int>> neighborOffsets = { // Counter-clockwise around the vertex
     {-1, -1},
     { 0, -1},
     { 1, -1},
     { 1,  0},
     { 1,  1},
     { 0,  1},
     {-1,  1},
     {-1,  0}
    };
    glm::vec3 V = getPosition(row,col);
    for (int i = 0; i < 8; ++i) {
     int n1RowOffset = neighborOffsets[i][0];
     int n1ColOffset = neighborOffsets[i][1];
     int n2RowOffset = neighborOffsets[(i + 1) % 8][0];
     int n2ColOffset = neighborOffsets[(i + 1) % 8][1];
     glm::vec3 n1 = getPosition(row + n1RowOffset, col + n1ColOffset);
     glm::vec3 n2 = getPosition(row + n2RowOffset, col + n2ColOffset);
     normal = normal + glm::cross(n1 - V, n2 - V);
    }
    return glm::normalize(normal);
}

// TODO: change to the other computing methods by using the height and normal
glm::vec3 TerrainGenerator::getColor(glm::vec3 normal, glm::vec3 position) {

    return glm::vec3(0,0,0.4);

    // Task 10: compute color as a function of the normal and position
    float y = position[2];
        glm::vec3 baseColor = glm::vec3(0.199, 0.328, 0.238); 
    glm::vec3 midColor = glm::vec3(0.473, 0.703, 0.562); // blue
    glm::vec3 topColor = glm::vec3(1.0, 1.0, 1.0);
    float thres1 = -0.12;
    float thres2 = -0.1;
    float thres3 = 0.006;
    if (y < thres1) {
        return baseColor;
    }else if (y < thres2) {
        float a = 1.0/(thres2 - thres1)*(y - thres1);
        return baseColor * (1-a) + midColor * a;
    }else if (y < thres3) {
        float a = 1.0/(thres3 - thres2)*(y - thres2);
        return midColor * (1-a) + topColor * a;

    }else {
        return glm::vec3(1,1,1);
    }

    return midColor;
}
