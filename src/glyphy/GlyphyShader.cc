/*
 * Copyright 2012 Google, Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Google Author(s): Behdad Esfahbod
 */

#include "GlyphyShader.h"

#include "GobanShader.h"

static unsigned int
glyph_encode (unsigned int atlas_x ,  /* 7 bits */
	      unsigned int atlas_y,   /* 7 bits */
	      unsigned int corner_x,  /* 1 bit */
	      unsigned int corner_y,  /* 1 bit */
	      unsigned int nominal_w, /* 6 bits */
	      unsigned int nominal_h  /* 6 bits */)
{
  assert (0 == (atlas_x & ~0x7F));
  assert (0 == (atlas_y & ~0x7F));
  assert (0 == (corner_x & ~1));
  assert (0 == (corner_y & ~1));
  assert (0 == (nominal_w & ~0x3F));
  assert (0 == (nominal_h & ~0x3F));

  unsigned int x = (((atlas_x << 6) | nominal_w) << 1) | corner_x;
  unsigned int y = (((atlas_y << 6) | nominal_h) << 1) | corner_y;

  return (x << 16) | y;
}

static void
glyph_vertex_encode (double x, double y,
		     unsigned int corner_x, unsigned int corner_y,
		     const glyph_info_t *gi,
		     glyph_vertex_t *v)
{
  unsigned int encoded = glyph_encode (gi->atlas_x, gi->atlas_y,
				       corner_x, corner_y,
				       gi->nominal_w, gi->nominal_h);
  v->x = x;
  v->y = y;
  v->g16hi = encoded >> 16;
  v->g16lo = encoded & 0xFFFF;
}

void
demo_shader_add_glyph_vertices (const glyphy_point_t        &p,
				double                       font_size,
				glyph_info_t                *gi,
				std::vector<glyph_vertex_t> *vertices,
				glyphy_extents_t            *extents,
				bool extents_only)
{
  if (gi->is_empty)
    return;

  glyph_vertex_t v[4];

#define ENCODE_CORNER(_cx, _cy) \
  do { \
    double _vx = p.x + font_size * ((1-_cx) * gi->extents.min_x + _cx * gi->extents.max_x); \
    double _vy = p.y - font_size * ((1-_cy) * gi->extents.min_y + _cy * gi->extents.max_y); \
    glyph_vertex_encode (_vx, _vy, _cx, _cy, gi, &v[_cx * 2 + _cy]); \
  } while (0)
  ENCODE_CORNER (0, 0);
  ENCODE_CORNER (0, 1);
  ENCODE_CORNER (1, 0);
  ENCODE_CORNER (1, 1);
#undef ENCODE_CORNER

  if (!extents_only) {
	  vertices->push_back(v[0]);
	  vertices->push_back(v[1]);
	  vertices->push_back(v[2]);

	  vertices->push_back(v[1]);
	  vertices->push_back(v[2]);
	  vertices->push_back(v[3]);
  }

  if (extents) {
    glyphy_extents_clear (extents);
    for (unsigned int i = 0; i < 4; i++) {
      glyphy_point_t p = {v[i].x, v[i].y};
      glyphy_extents_add (extents, &p);
    }
  }
}




static GLuint
compile_shader (GLenum         type,
		GLsizei        count,
		const GLchar* const * sources,
		std::shared_ptr<spdlog::logger> console)
{
  GLuint shader;
  GLint compiled;

  if (!(shader = glCreateShader (type)))
    return shader;

  glShaderSource (shader, count, sources, 0);
  glCompileShader (shader);

  glGetShaderiv (shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    GLint info_len = 0;
    console->warn("{} shader failed to compile\n",
	     type == GL_VERTEX_SHADER ? "Vertex" : "Fragment");
    glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &info_len);

    if (info_len > 0) {
      char *info_log = (char*) malloc (info_len);
      glGetShaderInfoLog (shader, info_len, NULL, info_log);

      console->warn(info_log);
      free (info_log);
    }

    abort ();
  }

  return shader;
}

static GLuint
link_program (GLuint vshader,
	      GLuint fshader,
	      std::shared_ptr<spdlog::logger> console)
{
  GLuint program;
  GLint linked;

  program = glCreateProgram ();
  glAttachShader (program, vshader);
  glAttachShader (program, fshader);
  glLinkProgram (program);
  glDeleteShader (vshader);
  glDeleteShader (fshader);

  glGetProgramiv (program, GL_LINK_STATUS, &linked);
  if (!linked) {
    GLint info_len = 0;
    console->warn("Program failed to link");
    glGetProgramiv (program, GL_INFO_LOG_LENGTH, &info_len);

    if (info_len > 0) {
      char *info_log = (char*) malloc (info_len);
      glGetProgramInfoLog (program, info_len, NULL, info_log);

      console->warn(info_log);
      free (info_log);
    }

    abort ();
  }

  return program;
}

#ifdef GL_ES_VERSION_2_0
# define GLSL_HEADER_STRING \
  "#extension GL_OES_standard_derivatives : enable\n" \
  "precision highp float;\n" \
  "precision highp int;\n"
#else
# define GLSL_HEADER_STRING \
  "#version 110\n"
#endif

GLuint
demo_shader_create_program(std::shared_ptr<spdlog::logger> console)
{
  GLuint vshader, fshader, program;

  std::string vshadersrc(createShader("data/glsl/overlay-vertex.glsl", false));
  std::string fshadersrc(createShader("data/glsl/overlay-fragment.glsl", false));
  std::string atlassrc(createShader("data/glsl/overlay-atlas.glsl", false));

  const std::array<const GLchar * const ,2> vshader_sources = { GLSL_HEADER_STRING, vshadersrc.c_str() };

  vshader = compile_shader (GL_VERTEX_SHADER, vshader_sources.size(), vshader_sources.data(), console);



  const std::array<const GLchar * const, 6> fshader_sources = {GLSL_HEADER_STRING,
				     atlassrc.c_str(),
				     glyphy_common_shader_source (),
				     "#define GLYPHY_SDF_PSEUDO_DISTANCE 1\n",
				     glyphy_sdf_shader_source (),
				     fshadersrc.c_str() };

   fshader = compile_shader (GL_FRAGMENT_SHADER, fshader_sources.size(), fshader_sources.data(), console);

  program = link_program (vshader, fshader, console);
  return program;
}