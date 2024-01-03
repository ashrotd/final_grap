#pragma once

#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif
#include <GL/glew.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

class ShaderLoader {
public:
    static GLuint createShaderProgram(const char *vertex_file_path, const char *fragment_file_path) {
        // Create and compile the shaders.
        GLuint vertexShaderID = createShader(GL_VERTEX_SHADER, vertex_file_path);
        GLuint fragmentShaderID = createShader(GL_FRAGMENT_SHADER, fragment_file_path);

        // Link the shader program.
        GLuint programID = glCreateProgram();
        glAttachShader(programID, vertexShaderID);
        glAttachShader(programID, fragmentShaderID);
        glLinkProgram(programID);

        // Check for linking errors
        checkLinkStatus(programID);

        // Shaders no longer necessary, stored in program
        glDeleteShader(vertexShaderID);
        glDeleteShader(fragmentShaderID);

        return programID;
    }

    static GLuint createComputeShaderProgram(const char *compute_file_path) {
        // Create and compile the shader.
        GLuint computeShaderID = createShader(GL_COMPUTE_SHADER, compute_file_path);

        // Link the shader program.
        GLuint programID = glCreateProgram();
        glAttachShader(programID, computeShaderID);
        glLinkProgram(programID);

        // Check for linking errors
        checkLinkStatus(programID);

        // Shader no longer necessary, stored in program
        glDeleteShader(computeShaderID);

        return programID;
    }

private:
    static GLuint createShader(GLenum shaderType, const char *filepath) {
        GLuint shaderID = glCreateShader(shaderType);

        // Read shader file.
        std::string code = readFile(filepath);

        // Compile shader code.
        const char *codePtr = code.c_str();
        glShaderSource(shaderID, 1, &codePtr, nullptr); // Assumes code is null terminated
        glCompileShader(shaderID);

        // Check for compilation errors
        checkCompileStatus(shaderID);

        return shaderID;
    }

    static std::string readFile(const char *filepath) {
        std::ifstream file(filepath, std::ios::in);
        if (!file.is_open()) {
            throw std::runtime_error(std::string("Failed to open shader file: ") + filepath);
        }

        std::stringstream sstr;
        sstr << file.rdbuf();
        return sstr.str();
    }

    static void checkCompileStatus(GLuint shaderID) {
        GLint status;
        glGetShaderiv(shaderID, GL_COMPILE_STATUS, &status);
        if (status == GL_FALSE) {
            GLint length;
            glGetShaderiv(shaderID, GL_INFO_LOG_LENGTH, &length);
            std::string log(length, '\0');
            glGetShaderInfoLog(shaderID, length, nullptr, &log[0]);
            glDeleteShader(shaderID);
            throw std::runtime_error(log);
        }
    }

    static void checkLinkStatus(GLuint programID) {
        GLint status;
        glGetProgramiv(programID, GL_LINK_STATUS, &status);
        if (status == GL_FALSE) {
            GLint length;
            glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &length);
            std::string log(length, '\0');
            glGetProgramInfoLog(programID, length, nullptr, &log[0]);
            glDeleteProgram(programID);
            throw std::runtime_error(log);
        }
    }
};
