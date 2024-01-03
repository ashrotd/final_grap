#pragma once
#include <GL/glew.h>
#include <iostream>
#include <string>

namespace Debug
{
    inline std::string getErrorString(GLenum error) {
        switch (error) {
            case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
            case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
            case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
            case GL_STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
            case GL_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
            case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
            case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
            default: return "Unknown Error";
        }
    }

    inline void checkOpenGLErrors(const char *fileName, int lineNum) {
        GLenum error;
        while ((error = glGetError()) != GL_NO_ERROR) {
            std::cout << "OpenGL Error in " << fileName << " at line " << lineNum << ": "
                      << getErrorString(error) << " (" << error << ")" << std::endl;
        }
    }

    #define checkOpenGLErrors() checkOpenGLErrors(__FILE__, __LINE__)
}
