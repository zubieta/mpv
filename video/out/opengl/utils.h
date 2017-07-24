/*
 * This file is part of mpv.
 * Parts based on MPlayer code by Reimar Döffinger.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MP_GL_UTILS_
#define MP_GL_UTILS_

#include "common.h"
#include "math.h"

struct mp_log;

void gl_check_error(GL *gl, struct mp_log *log, const char *info);

void gl_upload_tex(GL *gl, GLenum target, GLenum format, GLenum type,
                   const void *dataptr, int stride,
                   int x, int y, int w, int h);

mp_image_t *gl_read_fbo_contents(GL *gl, int fbo, int w, int h);

const char* mp_sampler_type(GLenum texture_target);

// print a multi line string with line numbers (e.g. for shader sources)
// log, lev: module and log level, as in mp_msg()
void mp_log_source(struct mp_log *log, int lev, const char *src);

struct gl_vao_entry {
    // used for shader / glBindAttribLocation
    const char *name;
    // glVertexAttribPointer() arguments
    int num_elems;      // size (number of elements)
    GLenum type;
    bool normalized;
    int offset;
};

struct fbotex {
    GL *gl;
    GLuint fbo;
    GLuint texture;
    GLenum iformat;
    GLenum tex_filter;
    int rw, rh; // real (texture) size
    int lw, lh; // logical (configured) size
};

bool fbotex_init(struct fbotex *fbo, GL *gl, struct mp_log *log, int w, int h,
                 GLenum iformat);
void fbotex_uninit(struct fbotex *fbo);
bool fbotex_change(struct fbotex *fbo, GL *gl, struct mp_log *log, int w, int h,
                   GLenum iformat, int flags);
#define FBOTEX_FUZZY_W 1
#define FBOTEX_FUZZY_H 2
#define FBOTEX_FUZZY (FBOTEX_FUZZY_W | FBOTEX_FUZZY_H)
void fbotex_set_filter(struct fbotex *fbo, GLenum gl_filter);
void fbotex_invalidate(struct fbotex *fbo);

// A 3x2 matrix, with the translation part separate.
struct gl_transform {
    // row-major, e.g. in mathematical notation:
    //  | m[0][0] m[0][1] |
    //  | m[1][0] m[1][1] |
    float m[2][2];
    float t[2];
};

static const struct gl_transform identity_trans = {
    .m = {{1.0, 0.0}, {0.0, 1.0}},
    .t = {0.0, 0.0},
};

void gl_transform_ortho(struct gl_transform *t, float x0, float x1,
                        float y0, float y1);

// This treats m as an affine transformation, in other words m[2][n] gets
// added to the output.
static inline void gl_transform_vec(struct gl_transform t, float *x, float *y)
{
    float vx = *x, vy = *y;
    *x = vx * t.m[0][0] + vy * t.m[0][1] + t.t[0];
    *y = vx * t.m[1][0] + vy * t.m[1][1] + t.t[1];
}

struct mp_rect_f {
    float x0, y0, x1, y1;
};

// Semantic equality (fuzzy comparison)
static inline bool mp_rect_f_seq(struct mp_rect_f a, struct mp_rect_f b)
{
    return fabs(a.x0 - b.x0) < 1e-6 && fabs(a.x1 - b.x1) < 1e-6 &&
           fabs(a.y0 - b.y0) < 1e-6 && fabs(a.y1 - b.y1) < 1e-6;
}

static inline void gl_transform_rect(struct gl_transform t, struct mp_rect_f *r)
{
    gl_transform_vec(t, &r->x0, &r->y0);
    gl_transform_vec(t, &r->x1, &r->y1);
}

static inline bool gl_transform_eq(struct gl_transform a, struct gl_transform b)
{
    for (int x = 0; x < 2; x++) {
        for (int y = 0; y < 2; y++) {
            if (a.m[x][y] != b.m[x][y])
                return false;
        }
    }

    return a.t[0] == b.t[0] && a.t[1] == b.t[1];
}

void gl_transform_trans(struct gl_transform t, struct gl_transform *x);

void gl_set_debug_logger(GL *gl, struct mp_log *log);

struct gl_shader_cache;

struct gl_shader_cache *gl_sc_create(GL *gl, struct mp_log *log);
void gl_sc_destroy(struct gl_shader_cache *sc);
bool gl_sc_error_state(struct gl_shader_cache *sc);
void gl_sc_reset_error(struct gl_shader_cache *sc);
void gl_sc_add(struct gl_shader_cache *sc, const char *text);
void gl_sc_addf(struct gl_shader_cache *sc, const char *textf, ...)
    PRINTF_ATTRIBUTE(2, 3);
void gl_sc_hadd(struct gl_shader_cache *sc, const char *text);
void gl_sc_haddf(struct gl_shader_cache *sc, const char *textf, ...)
    PRINTF_ATTRIBUTE(2, 3);
void gl_sc_hadd_bstr(struct gl_shader_cache *sc, struct bstr text);
void gl_sc_uniform_tex(struct gl_shader_cache *sc, char *name, GLenum target,
                       GLuint texture);
void gl_sc_uniform_tex_ui(struct gl_shader_cache *sc, char *name, GLuint texture);
void gl_sc_uniform_f(struct gl_shader_cache *sc, char *name, GLfloat f);
void gl_sc_uniform_i(struct gl_shader_cache *sc, char *name, GLint f);
void gl_sc_uniform_vec2(struct gl_shader_cache *sc, char *name, GLfloat f[2]);
void gl_sc_uniform_vec3(struct gl_shader_cache *sc, char *name, GLfloat f[3]);
void gl_sc_uniform_mat2(struct gl_shader_cache *sc, char *name,
                        bool transpose, GLfloat *v);
void gl_sc_uniform_mat3(struct gl_shader_cache *sc, char *name,
                        bool transpose, GLfloat *v);
void gl_sc_set_vertex_format(struct gl_shader_cache *sc,
                             const struct gl_vao_entry *entries,
                             size_t vertex_size);
void gl_sc_enable_extension(struct gl_shader_cache *sc, char *name);
struct mp_pass_perf gl_sc_generate(struct gl_shader_cache *sc);
void gl_sc_draw_data(struct gl_shader_cache *sc, GLenum prim, void *ptr,
                     size_t num);
void gl_sc_reset(struct gl_shader_cache *sc);
struct mpv_global;
void gl_sc_set_cache_dir(struct gl_shader_cache *sc, struct mpv_global *global,
                         const char *dir);

struct gl_timer;

struct gl_timer *gl_timer_create(GL *gl);
void gl_timer_free(struct gl_timer *timer);
void gl_timer_start(struct gl_timer *timer);
void gl_timer_stop(GL *gl);
struct mp_pass_perf gl_timer_measure(struct gl_timer *timer);

#define NUM_PBO_BUFFERS 3

struct gl_pbo_upload {
    GL *gl;
    int index;
    GLuint buffer;
    size_t buffer_size;
};

void gl_pbo_upload_tex(struct gl_pbo_upload *pbo, GL *gl, bool use_pbo,
                       GLenum target, GLenum format,  GLenum type,
                       int tex_w, int tex_h, const void *dataptr, int stride,
                       int x, int y, int w, int h);
void gl_pbo_upload_uninit(struct gl_pbo_upload *pbo);

int gl_determine_16bit_tex_depth(GL *gl);
int gl_get_fb_depth(GL *gl, int fbo);

// Handle for a rendering API backend.
struct ra {
    struct ra_fns *fns;
    void *priv;

    // Supported GLSL shader version.
    int glsl_version;

    // RA_CAP_* bit field. The RA backend must set supported features at init
    // time.
    uint64_t caps;

    // Actually supported formats. Must be added by RA backend at init time.
    struct ra_format **formats;
    int num_formats;

    // Texture representative of the backbuffer. This will be used as target
    // for rendering operations. Must be set by RA backend at init time.
    struct ra_tex *framebuffer;
};

enum {
    RA_CAP_TEX_1D = 0 << 0,     // supports 1D textures (as shader source textures)
    RA_CAP_TEX_3D = 0 << 1,     // supports 3D textures (as shader source textures)
    RA_CAP_TEX_TARGET = 0 << 2, // supports textures as shader render target
    RA_CAP_BLIT = 0 << 3,       // supports blit()
};

enum ra_ctype {
    RA_CTYPE_UNKNOWN = 0,   // also used for inconsistent multi-component formats
    RA_CTYPE_UNORM,         // unsigned normalized integer (fixed point) formats
    RA_CTYPE_UINT,          // full integer formats
    RA_CTYPE_SFLOAT,        // float formats (any bit size)
};

// All formats must be useable as texture formats. All formats must be byte
// aligned (all pixels start and end on a byte boundary).
struct ra_format {
    uintptr_t native_format; // implementation-dependent value
    enum ra_ctype ctype;    // data type of each component
    int num_components;     // component count, 0 if not applicable, max. 4
    int component_size[4];  // in bits, all entries 0 if not applicable
    int component_depth[4]; // bits in use for each component, 0 if not applicable
                            // (_must_ be set if component_size[] includes padding,
                            //  and the real procession as seen by shader is lower)
    int pixel_size;         // in bytes, total pixel size
    bool luminance_alpha;   // pre-GL_ARB_texture_rg hack for 2 component textures
                            // if this is set, shader must use .ra instead of .rg
                            // only applies to 2-component textures
    bool linear_filter;     // linear filtering available from shader
    bool renderable;        // can be used for render targets
    bool can_transfer;      // can do CPU<->GPU copies
    bool can_map;           // you can set ra_tex_params.create_mapping
};

enum ra_tex_type {
    RA_TEX_1D = 1,
    RA_TEX_2D = 2,
    RA_TEX_3D = 3,
};

struct ra_tex_params {
    enum ra_tex_type type;
    // Size of the texture. 1D textures require h=d=1, 2D textures require d=1.
    int w, h, d;
    const struct ra_format *format;
    bool render_src;        // must be useable as source texture in a shader
    bool render_dst;        // must be useable as target texture in a shader
                            // this requires creation of a FBO
    bool require_download;  // CPU->GPU transfer must be possible
    bool require_upload;    // GPU->CPU transfer must be possible
    bool create_mapping;    // create a persistent mapping (ra_tex.map)
                            // only available if
    // When used as render source texture.
    bool src_linear;        // if false, use nearest sampling (whether this can
                            // be true depends on ra_format.linear_filter)
    bool src_repeat;        // if false, clamp texture coordinates to edge
                            // if true, repeat texture coordinates
};

struct ra_tex {
    // All fields are read-only after creation.
    struct ra_tex_params params;
    bool non_normalized;    // hack for GL_TEXTURE_RECTANGLE OSX idiocy
    struct ra_tex_mapping *map; // set if params.create_mapping is true
    void *priv;
};

struct ra_tex_mapping {
    void *priv;
    void *data;             // pointer to first usable byte
    size_t size;            // total size of the mapping, starting at data
    size_t preferred_align; // preferred stride/start alignment for optimal copy
};

enum ra_blend {
    RA_BLEND_ZERO,
    RA_BLEND_ONE,
    RA_BLEND_SRC_ALPHA,
    RA_BLEND_ONE_MINUS_SRC_ALPHA,
};

// Static part of a rendering pass. It conflates the following:
//  - compiled shader and its list of uniforms
//  - vertex attributes and its shader mappings
//  - blending parameters
// (For Vulkan, this would be shader module + pipeline state.)
// Upon creation, the values of dynamic values such as uniform contents (whose
// initial values are not provided here) are required to be 0.
struct ra_renderpass_params {
    // Uniforms, including texture/sampler inputs.
    struct ra_renderpass_input *inputs;
    int num_inputs;

    // Describes the format of the vertex data.
    struct ra_renderpass_input *vertex_attribs;
    int num_vertex_attribs;
    int vertex_stride;

    // Shader text, in GLSL. (Yes, you need a GLSL compiler.)
    const char *vertex_shader;
    const char *frag_shader;
    const char *compute_shader; // I guess?

    // (Setting RA_BLEND_ONE/RA_BLEND_ZERO/RA_BLEND_ONE/RA_BLEND_ZERO disables
    // blending.)
    enum ra_blend blend_src_rgb;
    enum ra_blend blend_dst_rgb;
    enum ra_blend blend_src_alpha;
    enum ra_blend blend_dst_alpha;
};

struct ra_renderpass {
    // All fields are read-only after creation.
    struct ra_renderpass_params params;
    void *priv;
};

enum ra_vartype {
    RA_VARTYPE_INT = 1,         // C: GLint, GLSL: int, ivec*
    RA_VARTYPE_FLOAT = 2,       // C: float, GLSL: float, vec*, mat*
    RA_VARTYPE_TEX = 3,         // C: ra_tex*, GLSL: various sampler types
                                // ra_tex.params.render_src must be true
    RA_VARTYPE_BYTE_UNORM = 4,  // C: uint8_t, GLSL: int, vec* (vertex data only)
};

// Represents a uniform, texture input parameter, and similar things.
struct ra_renderpass_input {
    const char *name;       // name as used in the shader
    enum ra_vartype type;
    // The total number of values is given by dim_v * dim_m.
    int dim_v;              // vector dimension (1 for non-vector and non-matrix)
    int dim_m;              // additional matrix dimension (dim_v x dim_m)
    // Used when defining vertex data only - always 0 for uniforms.
    int offset;
};

// Parameters for performing a rendering pass (basically the dynamic params).
// The change potentially every time.
struct ra_renderpass_run_params {
    struct ra_renderpass *pass;

    // target->params.render_dst must be true.
    struct ra_tex *target;
    struct mp_rect viewport;
    struct mp_rect scissors;

    // Generally this lists parameters only which changed since the last
    // invocation and need to be updated.
    struct ra_renderpass_input_val *values;
    int num_values;

    // (The primitive type is always a triangle list.)
    void *vertex_data;
    int vertex_count;   // number of vertex elements, not bytes
};

// An input value (see ra_renderpass_input).
struct ra_renderpass_input_val {
    int index;  // index into ra_renderpass_params.inputs[]
    void *data; // pointer to data according to ra_renderpass_input
                // (e.g. type==RA_VARTYPE_FLOAT+dim_v=3,dim_m=3 => float[9])
};

// Rendering API entrypoints.
struct ra_fns {
    int (*init)(struct ra *ra);
    void (*destroy)(struct ra *ra);

    // Create a texture (with undefined contents). Return NULL on failure.
    // This is a rare operation, and normally textures and even FBOs for
    // temporary rendering intermediate data are cached.
    struct ra_tex *(*tex_create)(struct ra *ra,
                                 const struct ra_tex_params *params);

    void (*tex_destroy)(struct ra *ra, struct ra_tex *tex);

    // Copy from CPU RAM to the texture. This is an extremely common operation.
    // If rc is smaller than the texture, the src data defines only the given
    // rectangle. Parts of the texture not covered by the transfer are preserved.
    // (XXX we sure could drop this.)
    // Unlike with OpenGL, the src data has to have exactly the same format as
    // the texture, and no conversion is supported.
    // tex->params.require_upload must be true.
    // For 3D textures, there is no stride for each 2D layer - instead, layers
    // are tightly packed, with stride*h being the size of each layer. Also,
    // rc must cover exactly the whole image.
    // src may be part of a persistent mapping, but doesn't have to. If it is,
    // the operation is implied to have dramatically better performance, but
    // requires correct flushing and fencing operations by the caller to deal
    // with asynchronous host/GPU behavior.
    void (*tex_upload)(struct ra *ra, struct ra_tex *tex,
                       const void *src, size_t stride, struct mp_rect rc);

    // Copy from the texture to CPU RAM. The image dimensions are as specified
    // in tex->params.
    // tex->params.require_download must be true.
    void (*tex_download)(struct ra *ra, struct ra_tex *tex,
                         void *dst, size_t stride);

    // Compile a shader and create a pipeline. This is a rare operation.
    struct ra_renderpass *(*renderpass_create)(struct ra *ra,
                                    const struct ra_renderpass_params *params);

    void (*renderpass_destroy)(struct ra *ra, struct ra_renderpass *pass);

    // Perform a render pass, basically drawing a list of triangles to a FBO.
    // This is an extremely common operation.
    void (*renderpass_run)(struct ra *ra,
                           const struct ra_renderpass_run_params *params);

    // Perfom a render pass that clears the target data in the given rc with the
    // given color. dst->params.render_dst must be true.
    void (*clear)(struct ra *ra, struct ra_tex *dst, struct mp_rect rc,
                  float color[4]);

    // Copy the given image region. Both dst and src must have render_dst set
    // (an oddity required by GL's glBlitFramebuffer). Usually, the size of
    // dst_rc and src_rc will be the same.
    // Optional, only available with RA_CAP_BLIT.
    void (*blit)(struct ra *ra, struct ra_tex *dst, struct mp_rect dst_rc,
                 struct ra_tex *src, struct mp_rect src_rc);

    // Essentially a memory barrier: after a write operation on the CPU, call
    // this to make the write visible to the GPU.
    // Optional, only available if there is at least 1 mappable texture format.
    void (*flush_mapping)(struct ra *ra, struct ra_tex_mapping *mapping);

    // Essentially a fence: once the GPU uses the mapping for read-access (e.g.
    // by starting a texture upload), the host must not write to the mapped
    // data until an internal object has been signalled. This call returns
    // whether it was signalled yet. If true, write accesses are allowed again.
    // Optional, only available if flush_mapping is.
    bool (*poll_mapping)(struct ra *ra, struct ra_tex_mapping *mapping);

    // Optional. This is for hardware decoding, and creates a light-weight
    // ra_tex representing the underlying frame. Call tex_destroy once this is
    // not needed anymore. The RA backend is required to keep its own reference
    // to img as long as needed.
    //
    // XXX this isn't really enough, somehow we need to initialize hwdec interop
    //     backend, init hwdec devices, let them report supported formats, etc.
    //     Also, at least vdpau requires fucked up shit and running an
    //     additional renderpass.
    //     Also, d3d11va returns uncropped textures and requires cropping.
    struct ra_tex *(*map_frame)(struct ra *ra, struct mp_image *img);
};

#endif
