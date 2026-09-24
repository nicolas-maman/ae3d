#ifndef AE3D_GLAPI_H
#define AE3D_GLAPI_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned int  GLenum;
typedef unsigned char GLboolean;
typedef unsigned int  GLbitfield;
typedef signed char   GLbyte;
typedef short         GLshort;
typedef int           GLint;
typedef int           GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int  GLuint;
typedef float         GLfloat;
typedef float         GLclampf;
typedef double        GLdouble;
typedef double        GLclampd;
typedef void          GLvoid;
typedef char          GLchar;
typedef ptrdiff_t     GLintptr;
typedef ptrdiff_t     GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE  1
#define GL_NO_ERROR 0

#define GL_POINTS         0x0000
#define GL_LINES          0x0001
#define GL_TRIANGLES      0x0004

#define GL_DEPTH_BUFFER_BIT   0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT   0x00004000

#define GL_NEVER    0x0200
#define GL_LESS     0x0201
#define GL_EQUAL    0x0202
#define GL_LEQUAL   0x0203
#define GL_PACK_ALIGNMENT     0x0D05
#define GL_PIXEL_PACK_BUFFER 0x88EB
#define GL_STREAM_READ       0x88E1
#define GL_READ_ONLY         0x88B8

#define GL_SRC_ALPHA           0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_COLOR           0x0306
#define GL_ONE                 1
#define GL_ZERO                0

#define GL_FRONT           0x0404
#define GL_BACK            0x0405
#define GL_FRONT_AND_BACK  0x0408

#define GL_CW  0x0900
#define GL_CCW 0x0901

#define GL_CULL_FACE    0x0B44
#define GL_DEPTH_TEST   0x0B71
#define GL_BLEND        0x0BE2
#define GL_VIEWPORT     0x0BA2
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MULTISAMPLE  0x809D
#define GL_LINE_SMOOTH  0x0B20
#define GL_PROGRAM_POINT_SIZE 0x8642
#define GL_TEXTURE_CUBE_MAP_SEAMLESS 0x884F

#define GL_POINT 0x1B00
#define GL_LINE  0x1B01
#define GL_FILL  0x1B02

#define GL_BYTE           0x1400
#define GL_UNSIGNED_BYTE  0x1401
#define GL_SHORT          0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT            0x1404
#define GL_UNSIGNED_INT   0x1405
#define GL_FLOAT          0x1406

#define GL_VENDOR     0x1F00
#define GL_RENDERER   0x1F01
#define GL_VERSION    0x1F02

#define GL_RED  0x1903
#define GL_RGB  0x1907
#define GL_RGBA 0x1908
#define GL_RGBA16F 0x881A
#define GL_R32F    0x822E
#define GL_RGB16F  0x881B
#define GL_SRGB8_ALPHA8 0x8C43

#define GL_TEXTURE_2D        0x0DE1
#define GL_TEXTURE_3D        0x806F
#define GL_TEXTURE_WRAP_R    0x8072
#define GL_TEXTURE_CUBE_MAP  0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_TEXTURE0          0x84C0

#define GL_NEAREST 0x2600
#define GL_LINEAR  0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803
#define GL_TEXTURE_WRAP_R     0x8072
#define GL_REPEAT             0x2901
#define GL_CLAMP_TO_EDGE      0x812F
#define GL_UNPACK_ALIGNMENT   0x0CF5

#define GL_ARRAY_BUFFER         0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW          0x88E4
#define GL_DYNAMIC_DRAW         0x88E8
#define GL_STREAM_DRAW          0x88E0

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER   0x8B31
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

#define GL_FRAMEBUFFER          0x8D40
#define GL_READ_FRAMEBUFFER     0x8CA8
#define GL_DRAW_FRAMEBUFFER     0x8CA9
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_MAX_SAMPLES          0x8D57
#define GL_RGBA8                0x8058
#define GL_RENDERBUFFER         0x8D41
#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_COLOR_ATTACHMENT1    0x8CE1
#define GL_COLOR                0x1800
#define GL_RG                   0x8227
#define GL_RG16F                0x822F
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8     0x88F0
#define GL_DEPTH_STENCIL        0x84F9
#define GL_UNSIGNED_INT_24_8    0x84FA
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH_ATTACHMENT     0x8D00
#define GL_DEPTH_COMPONENT      0x1902
#define GL_DEPTH_COMPONENT32F   0x8CAC
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_NONE                 0

#define GL_DEPTH_CLAMP 0x864F

#define AE3D_GL_FUNCS(X) \
    X(const GLubyte *, glGetString, (GLenum name)) \
    X(GLenum, glGetError, (void)) \
    X(void, glGetIntegerv, (GLenum pname, GLint *data)) \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h)) \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
    X(void, glClearDepth, (GLdouble d)) \
    X(void, glClear, (GLbitfield mask)) \
    X(void, glEnable, (GLenum cap)) \
    X(void, glDisable, (GLenum cap)) \
    X(void, glDepthFunc, (GLenum func)) \
    X(void, glDepthMask, (GLboolean flag)) \
    X(void, glCullFace, (GLenum mode)) \
    X(void, glFrontFace, (GLenum mode)) \
    X(void, glBlendFunc, (GLenum sfactor, GLenum dfactor)) \
    X(void, glPolygonMode, (GLenum face, GLenum mode)) \
    X(void, glPixelStorei, (GLenum pname, GLint param)) \
    X(void, glGenVertexArrays, (GLsizei n, GLuint *arrays)) \
    X(void, glBindVertexArray, (GLuint array)) \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint *arrays)) \
    X(void, glGenBuffers, (GLsizei n, GLuint *buffers)) \
    X(void, glBindBuffer, (GLenum target, GLuint buffer)) \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage)) \
    X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void *data)) \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint *buffers)) \
    X(void *, glMapBuffer, (GLenum target, GLenum access)) \
    X(void, glFinish, (void)) \
    X(GLboolean, glUnmapBuffer, (GLenum target)) \
    X(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void *ptr)) \
    X(void, glEnableVertexAttribArray, (GLuint index)) \
    X(void, glDisableVertexAttribArray, (GLuint index)) \
    X(void, glVertexAttribDivisor, (GLuint index, GLuint divisor)) \
    X(void, glVertexAttrib3f, (GLuint index, GLfloat x, GLfloat y, GLfloat z)) \
    X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count)) \
    X(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void *indices)) \
    X(void, glDrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei primcount)) \
    X(GLuint, glCreateShader, (GLenum type)) \
    X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)) \
    X(void, glCompileShader, (GLuint shader)) \
    X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint *params)) \
    X(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)) \
    X(void, glDeleteShader, (GLuint shader)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint program, GLuint shader)) \
    X(void, glDetachShader, (GLuint program, GLuint shader)) \
    X(void, glLinkProgram, (GLuint program)) \
    X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint *params)) \
    X(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)) \
    X(void, glUseProgram, (GLuint program)) \
    X(void, glDeleteProgram, (GLuint program)) \
    X(GLint, glGetUniformLocation, (GLuint program, const GLchar *name)) \
    X(void, glUniform1i, (GLint loc, GLint v0)) \
    X(void, glUniform1f, (GLint loc, GLfloat v0)) \
    X(void, glUniform2f, (GLint loc, GLfloat v0, GLfloat v1)) \
    X(void, glUniform3f, (GLint loc, GLfloat v0, GLfloat v1, GLfloat v2)) \
    X(void, glUniform4f, (GLint loc, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)) \
    X(void, glUniform1fv, (GLint loc, GLsizei count, const GLfloat *value)) \
    X(void, glUniform3fv, (GLint loc, GLsizei count, const GLfloat *value)) \
    X(void, glUniformMatrix4fv, (GLint loc, GLsizei count, GLboolean transpose, const GLfloat *value)) \
    X(void, glGenTextures, (GLsizei n, GLuint *textures)) \
    X(void, glBindTexture, (GLenum target, GLuint texture)) \
    X(void, glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *pixels)) \
    X(void, glTexImage3D, (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLsizei d, GLint border, GLenum format, GLenum type, const void *pixels)) \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param)) \
    X(void, glDeleteTextures, (GLsizei n, const GLuint *textures)) \
    X(void, glActiveTexture, (GLenum texture)) \
    X(void, glGenerateMipmap, (GLenum target)) \
    X(void, glGenFramebuffers, (GLsizei n, GLuint *ids)) \
    X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target)) \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint *framebuffers)) \
    X(void, glGenRenderbuffers, (GLsizei n, GLuint *renderbuffers)) \
    X(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer)) \
    X(void, glRenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei w, GLsizei h)) \
    X(void, glRenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum internalformat, GLsizei w, GLsizei h)) \
    X(void, glBlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter))     X(void, glDrawBuffers, (GLsizei n, const GLenum *bufs))     X(void, glClearBufferfv, (GLenum buffer, GLint drawbuffer, const GLfloat *value)) \
    X(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum rbtarget, GLuint renderbuffer)) \
    X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint *renderbuffers)) \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *pixels)) \
    X(void, glDrawBuffer, (GLenum buf)) \
    X(void, glReadBuffer, (GLenum src)) \
    X(void, glGenQueries, (GLsizei n, GLuint *ids)) \
    X(void, glDeleteQueries, (GLsizei n, const GLuint *ids)) \
    X(void, glBeginQuery, (GLenum target, GLuint id)) \
    X(void, glEndQuery, (GLenum target)) \
    X(void, glGetQueryObjectuiv, (GLuint id, GLenum pname, GLuint *params)) \
    X(void, glGetQueryObjectui64v, (GLuint id, GLenum pname, unsigned long long *params))

#define AE3D_GL_DECL(ret, name, args) typedef ret (*ae3d_pfn_##name) args; extern ae3d_pfn_##name ae3d_##name;
AE3D_GL_FUNCS(AE3D_GL_DECL)
#undef AE3D_GL_DECL

int ae3d_glapi_load(void);

#define glGetString                ae3d_glGetString
#define glGetError                 ae3d_glGetError
#define glGetIntegerv              ae3d_glGetIntegerv
#define glViewport                 ae3d_glViewport
#define glClearColor               ae3d_glClearColor
#define glClearDepth               ae3d_glClearDepth
#define glClear                    ae3d_glClear
#define glEnable                   ae3d_glEnable
#define glDisable                  ae3d_glDisable
#define glDepthFunc                ae3d_glDepthFunc
#define glDepthMask                ae3d_glDepthMask
#define glCullFace                 ae3d_glCullFace
#define glFrontFace                ae3d_glFrontFace
#define glBlendFunc                ae3d_glBlendFunc
#define glPolygonMode              ae3d_glPolygonMode
#define glPixelStorei              ae3d_glPixelStorei
#define glGenVertexArrays          ae3d_glGenVertexArrays
#define glBindVertexArray          ae3d_glBindVertexArray
#define glDeleteVertexArrays       ae3d_glDeleteVertexArrays
#define glGenBuffers               ae3d_glGenBuffers
#define glBindBuffer               ae3d_glBindBuffer
#define glBufferData               ae3d_glBufferData
#define glBufferSubData            ae3d_glBufferSubData
#define glDeleteBuffers            ae3d_glDeleteBuffers
#define glMapBuffer                ae3d_glMapBuffer
#define glFinish                   ae3d_glFinish
#define glUnmapBuffer              ae3d_glUnmapBuffer
#define glVertexAttribPointer      ae3d_glVertexAttribPointer
#define glEnableVertexAttribArray  ae3d_glEnableVertexAttribArray
#define glDisableVertexAttribArray ae3d_glDisableVertexAttribArray
#define glVertexAttribDivisor      ae3d_glVertexAttribDivisor
#define glVertexAttrib3f           ae3d_glVertexAttrib3f
#define glDrawArrays               ae3d_glDrawArrays
#define glDrawElements             ae3d_glDrawElements
#define glDrawElementsInstanced    ae3d_glDrawElementsInstanced
#define glCreateShader             ae3d_glCreateShader
#define glShaderSource             ae3d_glShaderSource
#define glCompileShader            ae3d_glCompileShader
#define glGetShaderiv              ae3d_glGetShaderiv
#define glGetShaderInfoLog         ae3d_glGetShaderInfoLog
#define glDeleteShader             ae3d_glDeleteShader
#define glCreateProgram            ae3d_glCreateProgram
#define glAttachShader             ae3d_glAttachShader
#define glDetachShader             ae3d_glDetachShader
#define glLinkProgram              ae3d_glLinkProgram
#define glGetProgramiv             ae3d_glGetProgramiv
#define glGetProgramInfoLog        ae3d_glGetProgramInfoLog
#define glUseProgram               ae3d_glUseProgram
#define glDeleteProgram            ae3d_glDeleteProgram
#define glGetUniformLocation       ae3d_glGetUniformLocation
#define glUniform1i                ae3d_glUniform1i
#define glUniform1f                ae3d_glUniform1f
#define glUniform2f                ae3d_glUniform2f
#define glUniform3f                ae3d_glUniform3f
#define glUniform4f                ae3d_glUniform4f
#define glUniform1fv               ae3d_glUniform1fv
#define glUniform3fv               ae3d_glUniform3fv
#define glUniformMatrix4fv         ae3d_glUniformMatrix4fv
#define glGenTextures              ae3d_glGenTextures
#define glBindTexture              ae3d_glBindTexture
#define glTexImage2D               ae3d_glTexImage2D
#define glTexImage3D               ae3d_glTexImage3D
#define glTexParameteri            ae3d_glTexParameteri
#define glDeleteTextures           ae3d_glDeleteTextures
#define glActiveTexture            ae3d_glActiveTexture
#define glGenerateMipmap           ae3d_glGenerateMipmap
#define glGenFramebuffers          ae3d_glGenFramebuffers
#define glBindFramebuffer          ae3d_glBindFramebuffer
#define glFramebufferTexture2D     ae3d_glFramebufferTexture2D
#define glCheckFramebufferStatus   ae3d_glCheckFramebufferStatus
#define glDeleteFramebuffers       ae3d_glDeleteFramebuffers
#define glGenRenderbuffers         ae3d_glGenRenderbuffers
#define glBindRenderbuffer         ae3d_glBindRenderbuffer
#define glRenderbufferStorageMultisample ae3d_glRenderbufferStorageMultisample
#define glBlitFramebuffer          ae3d_glBlitFramebuffer
#define glDrawBuffers              ae3d_glDrawBuffers
#define glClearBufferfv            ae3d_glClearBufferfv
#define glRenderbufferStorage      ae3d_glRenderbufferStorage
#define glFramebufferRenderbuffer  ae3d_glFramebufferRenderbuffer
#define glDeleteRenderbuffers      ae3d_glDeleteRenderbuffers
#define glReadPixels               ae3d_glReadPixels
#define glDrawBuffer               ae3d_glDrawBuffer
#define glReadBuffer               ae3d_glReadBuffer
#define glGenQueries               ae3d_glGenQueries
#define glDeleteQueries            ae3d_glDeleteQueries
#define glBeginQuery               ae3d_glBeginQuery
#define glEndQuery                 ae3d_glEndQuery
#define glGetQueryObjectuiv        ae3d_glGetQueryObjectuiv
#define glGetQueryObjectui64v      ae3d_glGetQueryObjectui64v

#endif
