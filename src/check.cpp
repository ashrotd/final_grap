#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include "utils/shaderloader.h"
#include <array>
#include "setting.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <vector>
#include "terrain/terraingenerator.h"
#include "camera/camera.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/component_wise.hpp>
#include "noise/perlin-zhou.h"
#include "noise/worley.h"
#include "utils/debug.h"
#include <memory>
#include "glStructure/FBO.h"

GLuint m_volumeShader,  m_worleyShader, m_terrainShader, m_terrainTextureShader;
GLuint vboScreenQuad, vaoScreenQuad;
GLuint vboVolume, vaoVolume;
GLuint volumeTexHighRes, volumeTexLowRes;
GLuint ssboWorley;
GLuint sunTexture;
GLuint nightTexture;
Camera m_camera;

GLuint m_terrain_height_texture;
GLuint m_terrain_normal_texture;
GLuint m_terrain_color_texture;

// Terrain
    std::vector<float> m_terrain_data;
    GLuint m_terrain_vbo;
    GLuint m_terrain_vao;

    glm::mat4 m_proj;
    glm::mat4 m_projView;

    glm::mat4 m_terrain_camera;
    glm::mat4 m_world;
    int m_screen_width;
    int m_screen_height;

    TerrainGenerator m_terrain;

std::unique_ptr<FBO> m_FBO;
bool glInitialized = false;

constexpr std::array<GLfloat, 42> cube = {
    .5f, .5f, -.5f, -.5f, .5f, -.5f, .5f, .5f, .5f, -.5f, .5f, .5f, -.5f, -.5f, .5f, -.5f, .5f, -.5f, -.5f, -.5f, -.5f,
    .5f, .5f, -.5f, .5f, -.5f, -.5f, .5f, .5f, .5f, .5f, -.5f, .5f, -.5f, -.5f, .5f, .5f, -.5f, -.5f, -.5f, -.5f, -.5f
};
constexpr std::array<GLfloat, 30> screenQuadData {
    // POSITION          // UV
    -1.0f,  1.0f, 0.f,  0.f, 1.f,
    -1.0f, -1.0f, 0.f,  0.f, 0.f,
     1.0f, -1.0f, 0.f,  1.f, 0.f,
     1.0f,  1.0f, 0.f,  1.f, 1.f,
    -1.0f,  1.0f, 0.f,  0.f, 1.f,
     1.0f, -1.0f, 0.f,  1.f, 0.f,
};

constexpr auto szVec3() { return sizeof(GLfloat) * 3; }
constexpr auto szVec4() { return sizeof(GLfloat) * 4; }

constexpr auto WORLEY_MAX_CELLS_PER_AXIS = 32;
constexpr auto WORLEY_MAX_NUM_POINTS = WORLEY_MAX_CELLS_PER_AXIS * WORLEY_MAX_CELLS_PER_AXIS * WORLEY_MAX_CELLS_PER_AXIS;

//Update worley points
void updateWorleyPoints(const WorleyPointsParams &worleyPointsParams) {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboWorley);
    {
        auto worleyPointsFine = Worley::createWorleyPointArray3D(worleyPointsParams.cellsPerAxisFine);
        auto worleyPointsMedium = Worley::createWorleyPointArray3D(worleyPointsParams.cellsPerAxisMedium);
        auto worleyPointsCoarse = Worley::createWorleyPointArray3D(worleyPointsParams.cellsPerAxisCoarse);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, worleyPointsFine.size()*szVec4(), worleyPointsFine.data());
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, WORLEY_MAX_NUM_POINTS*szVec4(), worleyPointsMedium.size()*szVec4(), worleyPointsMedium.data());
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 2*WORLEY_MAX_NUM_POINTS*szVec4(), worleyPointsCoarse.size()*szVec4(), worleyPointsCoarse.data());
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void setUpScreenQuad(){
    glGenBuffers(1, &vboScreenQuad);
    glBindBuffer(GL_ARRAY_BUFFER, vboScreenQuad);
    glBufferData(GL_ARRAY_BUFFER, screenQuadData.size()*sizeof(GLfloat), screenQuadData.data(), GL_STATIC_DRAW);
    glGenVertexArrays(1, &vaoScreenQuad);
    glBindVertexArray(vaoScreenQuad);
    glEnableVertexAttribArray(0);  // pos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), nullptr);
    glEnableVertexAttribArray(1);  // uv
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat),
                          reinterpret_cast<void*>(3 * sizeof(GLfloat)));
}

void setUpVolume(){
    // SSBO for Worley points of three frequencies, with enough memory prealloced
    glGenBuffers(1, &ssboWorley);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssboWorley);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssboWorley);
    glBufferData(GL_SHADER_STORAGE_BUFFER, 3*WORLEY_MAX_NUM_POINTS * szVec4(), NULL, GL_STATIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Volume textures (high and low res)
    const auto &dimHiRes = settings.hiResNoise.resolution;
    glGenTextures(1, &volumeTexHighRes);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, volumeTexHighRes);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F, dimHiRes, dimHiRes, dimHiRes, 0, GL_RGBA, GL_FLOAT, nullptr);

    const auto &dimLoRes = settings.loResNoise.resolution;
    glGenTextures(1, &volumeTexLowRes);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, volumeTexLowRes);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F, dimLoRes, dimLoRes, dimLoRes, 0, GL_RGBA, GL_FLOAT, nullptr);
}

void setUpTextures() {
    // Sun color texture
    
    int width, height, channels;
    unsigned char* data = stbi_load("../textures/sun_v1.png", &width, &height, &channels, 0);
    if (!data) {
        throw std::runtime_error("Failed to load sun texture");
    }
    
    glGenTextures(1, &sunTexture);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_1D, sunTexture);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, width, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_1D, 0);
    stbi_image_free(data);
    
    // Night sky texture
    data = stbi_load("../textures/stars2.png", &width, &height, &channels, 0);
    if (!data) {
        std::cerr << "Failed to load texture: stars2.png" << std::endl;
        throw std::runtime_error("Failed to load night sky texture");
    }
    
    // glGenTextures(1, &nightTexture);
    
    // glActiveTexture(GL_TEXTURE5);
    // glBindTexture(GL_TEXTURE_2D, nightTexture);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    // glBindTexture(GL_TEXTURE_2D, 0);
    // stbi_image_free(data);
}

void setUpTerrain() {
    // Generate and bind the VBO
    glGenBuffers(1, &m_terrain_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_terrain_vbo);

    // Put data into the VBO
    m_terrain.generateTerrain();
    glBufferData(GL_ARRAY_BUFFER,
                 m_terrain.getCoordMap().size() * sizeof(GLfloat),
                 m_terrain.getCoordMap().data(),
                 GL_STATIC_DRAW);

    // Generate and bind the VAO, with our VBO currently bound
    glGenVertexArrays(1, &m_terrain_vao);
    glBindVertexArray(m_terrain_vao);

    // Define VAO attributes
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat),
                             nullptr);

    // Unbind
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

//Draw Terrain Function
void drawTerrain() {
    glUseProgram(m_terrainShader);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_1D, sunTexture);

    glBindVertexArray(m_terrain_vao);
    int res = m_terrain.getResolution();
    glDrawArrays(GL_TRIANGLES, 0, res*res*6 * m_terrain.getScaleX() * m_terrain.getScaleY());
    glBindVertexArray(0);
    glUseProgram(0);
}

//draw Volume function
void drawVolume() {
    glDisable(GL_DEPTH_TEST);  // disable depth test for volume rendering
    glUseProgram(m_volumeShader);

    // Bind depth texture to slot #2 and color to #3
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_FBO.get()->getFboDepthTexture());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_FBO.get()->getFboColorTexture());
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_1D, sunTexture);

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, nightTexture);

    // Draw screen quad
    glBindVertexArray(vaoScreenQuad);
    glDrawArrays(GL_TRIANGLES, 0, screenQuadData.size() / 5);

    // Clear things up
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glEnable(GL_DEPTH_TEST);
}



void paintGL() {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, m_terrain_normal_texture);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, m_terrain_height_texture);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_terrain_color_texture);

        drawTerrain();
        drawVolume();

        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);

        Debug::checkOpenGLErrors();
    }


// Changed setting 
void settingsChanged() {
    if (!glInitialized) return;  // avoid gl calls before initialization finishes

    
    glUseProgram(m_terrainShader);
    // Light
    glUniform1f(glGetUniformLocation(m_terrainShader , "testLight.longitude"), settings.lightData.longitude);
    glUniform1f(glGetUniformLocation(m_terrainShader , "testLight.latitude"), settings.lightData.latitude);
    glUniform1i(glGetUniformLocation(m_terrainShader , "testLight.type"), settings.lightData.type);
    glUniform3fv(glGetUniformLocation(m_terrainShader , "testLight.dir"), 1, glm::value_ptr(settings.lightData.dir));
    glUniform3fv(glGetUniformLocation(m_terrainShader , "testLight.color"), 1, glm::value_ptr(settings.lightData.color));
    glUniform4fv(glGetUniformLocation(m_terrainShader , "testLight.pos"), 1, glm::value_ptr(settings.lightData.pos));


    glUseProgram(m_volumeShader);

    // Volume
    glUniform3fv(glGetUniformLocation(m_volumeShader, "volumeScaling"), 1, glm::value_ptr(settings.volumeScaling));
    glUniform3fv(glGetUniformLocation(m_volumeShader, "volumeTranslate"), 1, glm::value_ptr(settings.volumeTranslate));
    glUniform1i(glGetUniformLocation(m_volumeShader, "numSteps"), settings.numSteps);
//        glUniform1f(glGetUniformLocation(m_volumeShader, "stepSize"), settings.stepSize);

    // Render Params
    glUniform1f(glGetUniformLocation(m_volumeShader, "densityMult"), settings.densityMult);
    glUniform1i(glGetUniformLocation(m_volumeShader, "invertDensity"), settings.invertDensity);
    glUniform1i(glGetUniformLocation(m_volumeShader, "gammaCorrect"), settings.gammaCorrect);
    glUniform1f(glGetUniformLocation(m_volumeShader, "cloudLightAbsorptionMult"), settings.cloudLightAbsorptionMult);
    glUniform1f(glGetUniformLocation(m_volumeShader, "minLightTransmittance"), settings.minLightTransmittance);

    // Shape texture: hi-res
    glUniform4fv(glGetUniformLocation(m_volumeShader , "hiResNoiseScaling"), 1, glm::value_ptr(settings.hiResNoise.scaling));
    glUniform3fv(glGetUniformLocation(m_volumeShader, "hiResNoiseTranslate"), 1, glm::value_ptr(settings.hiResNoise.translate));
    glUniform4fv(glGetUniformLocation(m_volumeShader, "hiResChannelWeights"), 1, glm::value_ptr(settings.hiResNoise.channelWeights));
    glUniform1f(glGetUniformLocation(m_volumeShader , "hiResDensityOffset"), settings.hiResNoise.densityOffset);

    // Detailed texture: low-res
    glUniform1f(glGetUniformLocation(m_volumeShader , "loResNoiseScaling"), settings.loResNoise.scaling[0]);
    glUniform3fv(glGetUniformLocation(m_volumeShader, "loResNoiseTranslate"), 1, glm::value_ptr(settings.loResNoise.translate));
    glUniform4fv(glGetUniformLocation(m_volumeShader, "loResChannelWeights"), 1, glm::value_ptr(settings.loResNoise.channelWeights));
    glUniform1f(glGetUniformLocation(m_volumeShader , "loResDensityWeight"), settings.loResNoise.densityWeight);

    // Light
    glUniform1f(glGetUniformLocation(m_volumeShader , "testLight.longitude"), settings.lightData.longitude);
    glUniform1f(glGetUniformLocation(m_volumeShader , "testLight.latitude"), settings.lightData.latitude);
    glUniform1i(glGetUniformLocation(m_volumeShader , "testLight.type"), settings.lightData.type);
    glUniform3fv(glGetUniformLocation(m_volumeShader , "testLight.dir"), 1, glm::value_ptr(settings.lightData.dir));
    glUniform3fv(glGetUniformLocation(m_volumeShader , "testLight.color"), 1, glm::value_ptr(settings.lightData.color));
    glUniform4fv(glGetUniformLocation(m_volumeShader , "testLight.pos"), 1, glm::value_ptr(settings.lightData.pos));
    std::cout << glm::to_string(settings.lightData.dir) << glm::to_string(settings.lightData.color) << '\n';

    glUseProgram(m_worleyShader);
    auto newArray = settings.newFineArray || settings.newMediumArray || settings.newCoarseArray;
    if (newArray) {
        int texSlot = settings.curSlot;
        int channelIdx = settings.curChannel;
        std::cout << "check" << texSlot << " " << channelIdx << '\n';

        // pass uniforms
        const auto &noiseParams = texSlot == 0 ? settings.hiResNoise : settings.loResNoise;
        glUniform1f(glGetUniformLocation(m_worleyShader, "persistence"), noiseParams.persistence);
        glUniform1i(glGetUniformLocation(m_worleyShader, "volumeResolution"), noiseParams.resolution);

        const auto &volumeTex = texSlot == 0 ? volumeTexHighRes : volumeTexLowRes;
        glBindImageTexture(0, volumeTex, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA32F);


        glm::vec4 channelMask(0.f);
        channelMask[channelIdx] = 1.f;
        glUniform4fv(glGetUniformLocation(m_worleyShader, "channelMask"), 1, glm::value_ptr(channelMask));

        const auto &worleyPointsParams = noiseParams.worleyPointsParams[channelIdx];
        std::cout << "ss" << worleyPointsParams.cellsPerAxisFine << '\n';
        updateWorleyPoints(worleyPointsParams);  // generate new worley points into SSBO
        glUniform1i(glGetUniformLocation(m_worleyShader, "cellsPerAxisFine"), worleyPointsParams.cellsPerAxisFine);
        glUniform1i(glGetUniformLocation(m_worleyShader, "cellsPerAxisMedium"), worleyPointsParams.cellsPerAxisMedium);
        glUniform1i(glGetUniformLocation(m_worleyShader, "cellsPerAxisCoarse"), worleyPointsParams.cellsPerAxisCoarse);
        glDispatchCompute(noiseParams.resolution, noiseParams.resolution, noiseParams.resolution);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
    }

    glUseProgram(0);

    
}

//Finish function
void finish() {
    

    glDeleteBuffers(1, &vboVolume);
    glDeleteBuffers(1, &vboScreenQuad);
    glDeleteBuffers(1, &ssboWorley);
    glDeleteVertexArrays(1, &vaoVolume);
    glDeleteVertexArrays(1, &vaoScreenQuad);
    glDeleteProgram(m_volumeShader);
    glDeleteProgram(m_worleyShader);
    glDeleteTextures(1, &volumeTexHighRes);
    glDeleteTextures(1, &volumeTexLowRes);
}

// Initialize OpenGL function
void initializeGL(GLFWwindow* window){
    // Initialize GLEW
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::cerr << "Error initializing GLEW: " << glewGetErrorString(err) << std::endl;
        glfwTerminate();
        return ;
    }
    std::cout << "Initialized GLEW: Version " << glewGetString(GLEW_VERSION) << std::endl;

    // OpenGL Initialization (from Realtime::initializeGL)
    glClearColor(.5f, .5f, .5f, 1.f);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glEnable(GL_DEPTH_TEST);

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    glViewport(0, 0, width, height);

    // ... Rest of your OpenGL initialization code ...
    m_volumeShader = ShaderLoader::createShaderProgram("../Shaders/default.vert", "../Shaders/default.frag");
    m_worleyShader = ShaderLoader::createComputeShaderProgram("../Shaders/worley.comb");
    m_terrainShader = ShaderLoader::createShaderProgram("../Shaders/terrainGen.vert", "../Shaders/terrainGen.frag");
    m_terrainTextureShader = ShaderLoader::createShaderProgram("../Shaders/terrain.vert", "../Shaders/terrain.frag");

    setUpScreenQuad();
    setUpVolume();
    setUpTextures();

    /* Set up default camera */
    m_camera = Camera(SceneCameraData(), width, height, settings.nearPlane, settings.farPlane);


    /* Set up terrain shader and terrain data */
    glUseProgram(m_terrainShader);
    {
        // VAO, VBO
        setUpTerrain();

        m_world = glm::mat4(1.f);
        m_world = glm::translate(m_world, glm::vec3(-0.5, -0.5, 0));
        //m_world = glm::scale(m_world, glm::vec3(2, 2, 2));

        glm::mat4 projView = m_camera.getProjMatrix() * m_camera.getViewMatrix() * m_world;
        glUniformMatrix4fv(glGetUniformLocation(m_terrainShader, "projViewMatrix"), 1, GL_FALSE, glm::value_ptr(projView));

        glm::mat4 transInv = glm::transpose(glm::inverse(m_camera.getViewMatrix() * m_world));
        glUniformMatrix4fv(glGetUniformLocation(m_terrainShader, "transInvViewMatrix"), 1, GL_FALSE, glm::value_ptr(transInv));

        glUniform1f(glGetUniformLocation(m_terrainShader , "testLight.longitude"), settings.lightData.longitude);
        glUniform1f(glGetUniformLocation(m_terrainShader , "testLight.latitude"), settings.lightData.latitude);
        glUniform1i(glGetUniformLocation(m_terrainShader , "testLight.type"), settings.lightData.type);
        glUniform3fv(glGetUniformLocation(m_terrainShader , "testLight.dir"), 1, glm::value_ptr(settings.lightData.dir));
        glUniform3fv(glGetUniformLocation(m_terrainShader , "testLight.color"), 1, glm::value_ptr(settings.lightData.color));
        glUniform4fv(glGetUniformLocation(m_terrainShader , "testLight.pos"), 1, glm::value_ptr(settings.lightData.pos));

        GLint color_texture_loc = glGetUniformLocation(m_terrainShader, "color_sampler");
        glUniform1i(color_texture_loc, 3);
        GLint height_texture_loc = glGetUniformLocation(m_terrainShader, "height_sampler");
        glUniform1i(height_texture_loc, 6);
        GLint normal_texture_loc = glGetUniformLocation(m_terrainShader, "normal_sampler");
        glUniform1i(normal_texture_loc, 7);

        // start height map modification
        glGenTextures(1, &m_terrain_height_texture);
        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, m_terrain_height_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, m_terrain.getResolution(), m_terrain.getResolution(), 0, GL_RED, GL_FLOAT, m_terrain.getHeightMap().data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        // start normal map modification
        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, m_terrain_normal_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, m_terrain.getResolution(), m_terrain.getResolution(), 0, GL_RGB, GL_FLOAT, m_terrain.getNormalMap().data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        // start color map modification
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_terrain_color_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_terrain.getResolution(), m_terrain.getResolution(), 0, GL_RGB, GL_FLOAT, m_terrain.getColorMap().data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        // end height map modification
    }
    // Runs above this fine, 
    glUseProgram(0);


    /* Compute worley noise 3D textures */
    glUseProgram(m_worleyShader);
    for (GLuint texSlot : {0, 1}) {  // high and low res volumes
        // pass uniforms
        const auto &noiseParams = texSlot == 0 ? settings.hiResNoise : settings.loResNoise;
        glUniform1f(glGetUniformLocation(m_worleyShader, "persistence"), noiseParams.persistence);
        glUniform1i(glGetUniformLocation(m_worleyShader, "volumeResolution"), noiseParams.resolution);

        const auto &volumeTex = texSlot == 0 ? volumeTexHighRes : volumeTexLowRes;
        glBindImageTexture(0, volumeTex, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA32F);

        for (int channelIdx = 0; channelIdx < 4; channelIdx++) {
            glm::vec4 channelMask(0.f);
            channelMask[channelIdx] = 1.f;
            glUniform4fv(glGetUniformLocation(m_worleyShader, "channelMask"), 1, glm::value_ptr(channelMask));

            const auto &worleyPointsParams = noiseParams.worleyPointsParams[channelIdx];
            updateWorleyPoints(worleyPointsParams);  // generate new worley points into SSBO
            glUniform1i(glGetUniformLocation(m_worleyShader, "cellsPerAxisFine"), worleyPointsParams.cellsPerAxisFine);
            glUniform1i(glGetUniformLocation(m_worleyShader, "cellsPerAxisMedium"), worleyPointsParams.cellsPerAxisMedium);
            glUniform1i(glGetUniformLocation(m_worleyShader, "cellsPerAxisCoarse"), worleyPointsParams.cellsPerAxisCoarse);
            glDispatchCompute(noiseParams.resolution, noiseParams.resolution, noiseParams.resolution);
            glMemoryBarrier(GL_ALL_BARRIER_BITS);
        }
    }
    std::cout << "Hami yaha chau\n";
    /* Pass uniforms to volume shader */
    glUseProgram(m_volumeShader);
    {
        // Volume
        glUniform3fv(glGetUniformLocation(m_volumeShader, "volumeScaling"), 1, glm::value_ptr(settings.volumeScaling));
        glUniform3fv(glGetUniformLocation(m_volumeShader, "volumeTranslate"), 1, glm::value_ptr(settings.volumeTranslate));
        glUniform1i(glGetUniformLocation(m_volumeShader, "numSteps"), settings.numSteps);
//        glUniform1f(glGetUniformLocation(m_volumeShader, "stepSize"), settings.stepSize);
        
        // Noise
        glUniform1f(glGetUniformLocation(m_volumeShader, "densityMult"), settings.densityMult);
        glUniform1i(glGetUniformLocation(m_volumeShader, "invertDensity"), settings.invertDensity);
        glUniform1i(glGetUniformLocation(m_volumeShader, "gammaCorrect"), settings.gammaCorrect);
        // hi-res
        glUniform4fv(glGetUniformLocation(m_volumeShader , "hiResNoiseScaling"), 1, glm::value_ptr(settings.hiResNoise.scaling));
        glUniform3fv(glGetUniformLocation(m_volumeShader, "hiResNoiseTranslate"), 1, glm::value_ptr(settings.hiResNoise.translate));
        glUniform4fv(glGetUniformLocation(m_volumeShader, "hiResChannelWeights"), 1, glm::value_ptr(settings.hiResNoise.channelWeights));
        glUniform1f(glGetUniformLocation(m_volumeShader , "hiResDensityOffset"), settings.hiResNoise.densityOffset);
        // lo-res
        glUniform1f(glGetUniformLocation(m_volumeShader , "loResNoiseScaling"), settings.loResNoise.scaling[0]);
        glUniform3fv(glGetUniformLocation(m_volumeShader, "loResNoiseTranslate"), 1, glm::value_ptr(settings.loResNoise.translate));
        glUniform4fv(glGetUniformLocation(m_volumeShader, "loResChannelWeights"), 1, glm::value_ptr(settings.loResNoise.channelWeights));
        glUniform1f(glGetUniformLocation(m_volumeShader , "loResDensityWeight"), settings.loResNoise.densityWeight);

        // Camera
        glUniform1f(glGetUniformLocation(m_volumeShader , "xMax"), m_camera.xMax());
        glUniform1f(glGetUniformLocation(m_volumeShader , "yMax"), m_camera.yMax());
        glUniform3fv(glGetUniformLocation(m_volumeShader, "rayOrigWorld"), 1, glm::value_ptr(m_camera.getPos()));
        glUniformMatrix4fv(glGetUniformLocation(m_volumeShader, "viewInverse"), 1, GL_FALSE, glm::value_ptr(m_camera.getViewMatrixInverse()));

        // Lighting
//        glUniform1i(glGetUniformLocation(m_volumeShader, "numLights"), 0);
        glUniform4fv(glGetUniformLocation(m_volumeShader, "phaseParams"), 1, glm::value_ptr(glm::vec4(0.83f, 0.3f, 0.8f, 0.15f))); // TODO: make it adjustable hyperparameters
        glUniform1f(glGetUniformLocation(m_volumeShader , "testLight.longitude"), settings.lightData.longitude);
        glUniform1f(glGetUniformLocation(m_volumeShader , "testLight.latitude"), settings.lightData.latitude);
        glUniform1i(glGetUniformLocation(m_volumeShader , "testLight.type"), settings.lightData.type);
        glUniform3fv(glGetUniformLocation(m_volumeShader , "testLight.dir"), 1, glm::value_ptr(settings.lightData.dir));
        glUniform3fv(glGetUniformLocation(m_volumeShader , "testLight.color"), 1, glm::value_ptr(settings.lightData.color));
        glUniform4fv(glGetUniformLocation(m_volumeShader , "testLight.pos"), 1, glm::value_ptr(settings.lightData.pos));
        glUniform1i(glGetUniformLocation(m_volumeShader, "nightColor"), 5);
        glUniform1i(glGetUniformLocation(m_volumeShader, "sunGradient"), 4);
        glUniform1i(glGetUniformLocation(m_volumeShader, "solidDepth"), 2);
        glUniform1i(glGetUniformLocation(m_volumeShader, "solidColor"), 3);
        glUniform1f(glGetUniformLocation(m_volumeShader, "near"), settings.nearPlane);
        glUniform1f(glGetUniformLocation(m_volumeShader, "far"), settings.farPlane);
        std::cout<<settings.farPlane<<std::endl;
    }
    glUseProgram(0);

    // init FBO
    m_FBO = std::make_unique<FBO>(2, width, height);
    m_FBO.get()->makeFBO();

    std::cout << "checking errors in initializeGL...\n";
    Debug::checkOpenGLErrors();
    std::cout << "checking done\n";

    glInitialized = true;
}
//Resize GL code
void resizeGL(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);

    m_camera.setWidthHeight(width, height);

    if (m_camera.projChanged()) {
        m_camera.updateProjMatrix();  // only recompute proj matrices if clip planes updated
        m_camera.updateProjView();

        glUseProgram(m_terrainShader);
        glm::mat4 projView = m_camera.getProjMatrix() * m_camera.getViewMatrix() * m_world;
        glUniformMatrix4fv(glGetUniformLocation(m_terrainShader, "projViewMatrix"), 1, GL_FALSE, glm::value_ptr(projView));
        glUseProgram(0);
    }

    // Update FBO with the new size
    // Assuming you have a corresponding setup for FBO in GLFW
   
    m_FBO.get()->deleteRenderBuffer();
    m_FBO.get()->deleteDepthTexture();
    m_FBO.get()->deleteColorTexture();
    m_FBO.get()->deleteFrameBuffer();
    m_screen_width = width;
    m_screen_height = height;
    m_FBO.get()->setFboWidth(m_screen_width);
    m_FBO.get()->setFboHeight(m_screen_height);
    m_FBO.get()->makeFBO();

    glUseProgram(m_volumeShader);  // Pass camera mat (proj * view)
    glUniform1f(glGetUniformLocation(m_volumeShader , "xMax"), m_camera.xMax());
    glUseProgram(0);
}


int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    GLFWwindow* window = glfwCreateWindow(800, 600, "GLFW Application", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);

    initializeGL(window);


    while (!glfwWindowShouldClose(window)) {
        // Rendering commands go here
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        glfwSetFramebufferSizeCallback(window, resizeGL);


        paintGL();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    finish();
    // Clean up
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
