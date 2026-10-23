// OpenGL ES 3.0 mínimo: tipos, constantes y punteros a función cargados con SDL_GL_GetProcAddress.
// El mismo código corre en Android y en Windows (contexto ES 3.0 que ofrecen los drivers de escritorio).
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat, GLclampf;
typedef char GLchar;
typedef intptr_t GLintptr;
typedef intptr_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE 0x0308
#define GL_CONSTANT_COLOR 0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR 0x8002
#define GL_FUNC_ADD 0x8006
#define GL_MIN 0x8007
#define GL_MAX 0x8008
#define GL_FUNC_SUBTRACT 0x800A
#define GL_FUNC_REVERSE_SUBTRACT 0x800B
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_DITHER 0x0BD0
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_RED 0x1903
#define GL_RGBA 0x1908
#define GL_R8 0x8229
#define GL_RGBA8 0x8058
#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_INCR 0x1E02
#define GL_DECR 0x1E03
#define GL_INVERT 0x150A
#define GL_INCR_WRAP 0x8507
#define GL_DECR_WRAP 0x8508
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_TEXTURE0 0x84C0
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88E0
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_NO_ERROR 0

#define GL_FUNCIONES(X)                                                                                   \
  X(void, glActiveTexture, (GLenum))                                                                      \
  X(void, glAttachShader, (GLuint, GLuint))                                                               \
  X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *))                                         \
  X(void, glBindBuffer, (GLenum, GLuint))                                                                 \
  X(void, glBindFramebuffer, (GLenum, GLuint))                                                            \
  X(void, glBindRenderbuffer, (GLenum, GLuint))                                                           \
  X(void, glBindTexture, (GLenum, GLuint))                                                                \
  X(void, glBindVertexArray, (GLuint))                                                                    \
  X(void, glBlendColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                             \
  X(void, glBlendEquationSeparate, (GLenum, GLenum))                                                      \
  X(void, glBlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                          \
  X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
  X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))                                       \
  X(GLenum, glCheckFramebufferStatus, (GLenum))                                                           \
  X(void, glClear, (GLbitfield))                                                                          \
  X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                             \
  X(void, glClearDepthf, (GLfloat))                                                                       \
  X(void, glClearStencil, (GLint))                                                                        \
  X(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean))                                      \
  X(void, glCompileShader, (GLuint))                                                                      \
  X(GLuint, glCreateProgram, (void))                                                                      \
  X(GLuint, glCreateShader, (GLenum))                                                                     \
  X(void, glCullFace, (GLenum))                                                                           \
  X(void, glDeleteBuffers, (GLsizei, const GLuint *))                                                     \
  X(void, glDeleteFramebuffers, (GLsizei, const GLuint *))                                                \
  X(void, glDeleteRenderbuffers, (GLsizei, const GLuint *))                                               \
  X(void, glDeleteTextures, (GLsizei, const GLuint *))                                                    \
  X(void, glDepthFunc, (GLenum))                                                                          \
  X(void, glDepthMask, (GLboolean))                                                                       \
  X(void, glDepthRangef, (GLfloat, GLfloat))                                                              \
  X(void, glDisable, (GLenum))                                                                            \
  X(void, glDisableVertexAttribArray, (GLuint))                                                           \
  X(void, glDrawArrays, (GLenum, GLint, GLsizei))                                                         \
  X(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *))                                        \
  X(void, glEnable, (GLenum))                                                                             \
  X(void, glEnableVertexAttribArray, (GLuint))                                                            \
  X(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                                    \
  X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                                \
  X(void, glFrontFace, (GLenum))                                                                          \
  X(void, glGenBuffers, (GLsizei, GLuint *))                                                              \
  X(void, glGenFramebuffers, (GLsizei, GLuint *))                                                         \
  X(void, glGenRenderbuffers, (GLsizei, GLuint *))                                                        \
  X(void, glGenTextures, (GLsizei, GLuint *))                                                             \
  X(void, glGenVertexArrays, (GLsizei, GLuint *))                                                         \
  X(GLenum, glGetError, (void))                                                                           \
  X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                                    \
  X(void, glGetProgramiv, (GLuint, GLenum, GLint *))                                                      \
  X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                                     \
  X(void, glGetShaderiv, (GLuint, GLenum, GLint *))                                                       \
  X(const GLubyte *, glGetString, (GLenum))                                                               \
  X(GLint, glGetUniformLocation, (GLuint, const GLchar *))                                                \
  X(void, glLinkProgram, (GLuint))                                                                        \
  X(void, glPixelStorei, (GLenum, GLint))                                                                 \
  X(void, glPolygonOffset, (GLfloat, GLfloat))                                                            \
  X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))                         \
  X(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))                                      \
  X(void, glScissor, (GLint, GLint, GLsizei, GLsizei))                                                    \
  X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))                        \
  X(void, glStencilFuncSeparate, (GLenum, GLenum, GLint, GLuint))                                         \
  X(void, glStencilMaskSeparate, (GLenum, GLuint))                                                        \
  X(void, glStencilOpSeparate, (GLenum, GLenum, GLenum, GLenum))                                          \
  X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))    \
  X(void, glTexParameteri, (GLenum, GLenum, GLint))                                                       \
  X(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
  X(void, glUniform1i, (GLint, GLint))                                                                    \
  X(void, glUniform1f, (GLint, GLfloat))                                                                  \
  X(void, glUniform1fv, (GLint, GLsizei, const GLfloat *))                                                \
  X(void, glUniform4fv, (GLint, GLsizei, const GLfloat *))                                                \
  X(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *))                               \
  X(void, glUseProgram, (GLuint))                                                                         \
  X(void, glVertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat))                                 \
  X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))               \
  X(void, glViewport, (GLint, GLint, GLsizei, GLsizei))

#define GL_DECLARAR(ret, nombre, args) extern ret(*nombre) args;
GL_FUNCIONES(GL_DECLARAR)

int gl_cargar(void); // 0 si falta alguna
