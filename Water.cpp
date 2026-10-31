#include "Water.hpp"

#include "gl_compile_program.hpp"
#include "gl_errors.hpp"

#include <algorithm>
#include <stdexcept>

static_assert(sizeof(glm::vec3) == 3 * sizeof(float), "Light arrays require packed vec3 values.");
static_assert(sizeof(glm::vec4) == 4 * sizeof(float), "Blocker arrays require packed vec4 values.");

std::string const Water::glsl =
	"#define MAX_GLOWS " + std::to_string(max_glows) + "\n"
	"uniform vec2 VIEW_MIN;\n"
	"uniform vec2 VIEW_MAX;\n"
	"uniform int GLOW_COUNT;\n"
	"uniform vec3 GLOWS[MAX_GLOWS];\n"
	"uniform vec2 GLOW_RADIUS;\n"
	"uniform float BRIGHTNESS;\n"
	"uniform float TIME;\n"
	"uniform int BLOCKER_COUNT;\n"
	"uniform vec4 BLOCKERS[" + std::to_string(max_blockers) + "];\n"
	"uniform int COLUMN_COUNT;\n"
	"uniform vec3 BUBBLE_COLUMNS[" + std::to_string(max_columns) + "];\n"
	// unlit water color from Sarah's cavern art
	"const vec3 WATER = vec3(0.025, 0.028, 0.029);\n"
	"const float DIM = 0.15;\n"
	//based on https://nullprogram.com/blog/2018/07/31/
	// lowbias32, public domain under the Unlicense.
	//based on https://github.com/skeeto/hash-prospector#discovered-hash-functions
	"uint hash(uint x) {\n"
	"	x ^= x >> 16;\n"
	"	x *= 0x7feb352dU;\n"
	"	x ^= x >> 15;\n"
	"	x *= 0x846ca68bU;\n"
	"	x ^= x >> 16;\n"
	"	return x;\n"
	"}\n"
	"float random(uint x) {\n"
	"	return float(hash(x)) / 4294967295.0;\n"
	"}\n"
	// rocks block light between a source and the sprite
	"float visible(vec2 source, vec2 world) {\n"
	"	vec2 delta = world - source;\n"
	"	vec2 direction = mix(vec2(-1.0), vec2(1.0), step(vec2(0.0), delta));\n"
	"	vec2 inverse = direction / max(abs(delta), vec2(0.0001));\n"
	"	for (int i = 0; i < BLOCKER_COUNT; ++i) {\n"
	"		vec2 a = (BLOCKERS[i].xy - source) * inverse;\n"
	"		vec2 b = (BLOCKERS[i].zw - source) * inverse;\n"
	"		vec2 near_t = min(a, b);\n"
	"		vec2 far_t = max(a, b);\n"
	"		if (max(max(near_t.x, near_t.y), 0.002) < min(min(far_t.x, far_t.y), 0.998)) return 0.0;\n"
	"	}\n"
	"	return 1.0;\n"
	"}\n"
	"float light(vec2 world, bool shadows) {\n"
	"	float amount = DIM;\n"
	"	for (int i = 0; i < GLOW_COUNT; ++i) {\n"
	"		vec2 offset = (world - GLOWS[i].xy) / GLOW_RADIUS;\n"
	"		float distance2 = dot(offset, offset);\n"
	"		float strength = distance2 < 9.0 ? exp(-distance2) : 0.0;\n"
	"		if (strength > 0.0001) amount += GLOWS[i].z * strength * (shadows ? visible(GLOWS[i].xy, world) : 1.0);\n"
	"	}\n"
	"	return BRIGHTNESS * amount;\n"
	"}\n"
	"float noise(vec2 p) {\n"
	"	ivec2 cell = ivec2(floor(p));\n"
	"	vec2 f = fract(p);\n"
	"	f = f * f * (3.0 - 2.0 * f);\n"
	"	uint key = uint(cell.x) * 1597334677U ^ uint(cell.y) * 3812015801U;\n"
	"	uint next_y = uint(cell.x) * 1597334677U ^ uint(cell.y + 1) * 3812015801U;\n"
	"	float a = random(key);\n"
	"	float b = random(uint(cell.x + 1) * 1597334677U ^ uint(cell.y) * 3812015801U);\n"
	"	float c = random(next_y);\n"
	"	float d = random(uint(cell.x + 1) * 1597334677U ^ uint(cell.y + 1) * 3812015801U);\n"
	"	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);\n"
	"}\n"
	"float mist(vec2 world) {\n"
	"	vec2 p = world * 0.28 + vec2(TIME * 0.018, -TIME * 0.009);\n"
	"	return 0.6 * noise(p) + 0.28 * noise(p * 2.07) + 0.12 * noise(p * 4.13);\n"
	"}\n"
;

//based on https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGetUniformLocation.xhtml
Water::Uniforms::Uniforms(GLuint program_) : program(program_) {
	view_min = glGetUniformLocation(program, "VIEW_MIN");
	view_max = glGetUniformLocation(program, "VIEW_MAX");
	glow_count = glGetUniformLocation(program, "GLOW_COUNT");
	glows = glGetUniformLocation(program, "GLOWS[0]");
	glow_radius = glGetUniformLocation(program, "GLOW_RADIUS");
	brightness = glGetUniformLocation(program, "BRIGHTNESS");
	time = glGetUniformLocation(program, "TIME");
	blocker_count = glGetUniformLocation(program, "BLOCKER_COUNT");
	blockers = glGetUniformLocation(program, "BLOCKERS[0]");
	column_count = glGetUniformLocation(program, "COLUMN_COUNT");
	columns = glGetUniformLocation(program, "BUBBLE_COLUMNS[0]");
}

void Water::Uniforms::upload(Look const &look) const {
	glUseProgram(program);
	glUniform2fv(view_min, 1, &look.view_min.x);
	glUniform2fv(view_max, 1, &look.view_max.x);
	GLsizei count = GLsizei(std::min(look.glows.size(), max_glows));
	glUniform1i(glow_count, count);
	if (count > 0) glUniform3fv(glows, count, &look.glows[0].x);
	glUniform2fv(glow_radius, 1, &look.glow_radius.x);
	glUniform1f(brightness, look.brightness);
	glUniform1f(time, look.time);
	GLsizei blocker_size = GLsizei(std::min(look.blockers.size(), max_blockers));
	glUniform1i(blocker_count, blocker_size);
	if (blocker_size > 0) glUniform4fv(blockers, blocker_size, &look.blockers[0].x);
	GLsizei column_size = GLsizei(std::min(look.bubble_columns.size(), max_columns));
	glUniform1i(column_count, column_size);
	if (column_size > 0) glUniform3fv(columns, column_size, &look.bubble_columns[0].x);
	glUseProgram(0);
}

//based on https://registry.khronos.org/OpenGL-Refpages/gl4/html/gl_VertexID.xhtml
// one triangle covers the viewport
static char const *whole_view_vertex_shader =
	"#version 330\n"
	"out vec2 uv;\n"
	"void main() {\n"
	"	vec2 corner = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
	"	uv = corner;\n"
	"	gl_Position = vec4(2.0 * corner - 1.0, 0.0, 1.0);\n"
	"}\n";

Water::Water() {
	// Lighting placement follows https://blog.playdead.com/articles/the_lighting_of_inside/lighting_of_inside.html
	haze_program = gl_compile_program(whole_view_vertex_shader,
		"#version 330\n" + glsl +
		"in vec2 uv;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"	vec2 world = mix(VIEW_MIN, VIEW_MAX, uv);\n"
		"	float glow = max(0.0, light(world, false) - BRIGHTNESS * DIM);\n"
		"	vec2 drifting = world / 1.1 + vec2(TIME * 0.02, TIME * 0.045);\n"
		"	uvec2 cell = uvec2(ivec2(floor(drifting)));\n"
		"	uint key = hash(cell.x ^ hash(cell.y));\n"
		"	vec2 center = vec2(random(key), random(key ^ 173U));\n"
		"	float dust = (1.0 - smoothstep(0.006, 0.023, length(fract(drifting) - center))) * step(0.65, random(key ^ 719U));\n"
		"	float density = 0.04 * glow * (0.45 + 0.65 * mist(world));\n"
		"	density += dust * min(0.15, 0.08 * glow);\n"
		"	float bubbles = 0.0;\n"
		"	for (int column = 0; column < COLUMN_COUNT; ++column) {\n"
		"		vec3 vent = BUBBLE_COLUMNS[column];\n"
		"		if (abs(world.x - vent.x) > 0.9) continue;\n"
		"		for (int i = 0; i < 14; ++i) {\n"
		"			uint seed = uint(column * 41 + i + 1);\n"
		"			float phase = fract(TIME * mix(0.04, 0.065, random(seed)) + random(seed ^ 127U));\n"
		"			float y = mix(vent.y, vent.z, phase);\n"
		"			float x = vent.x + 0.5 * (random(seed ^ 71U) - 0.5) + 0.12 * sin(y * 0.5 + TIME * 0.7 + float(i));\n"
		"			vec2 offset = world - vec2(x, y);\n"
		"			float radius = mix(0.035, 0.08, random(seed ^ 317U));\n"
		"			float ring = 1.0 - smoothstep(0.008, 0.022, abs(length(offset) - radius));\n"
		"			float glint = 0.45 + 0.55 * max(0.0, dot(offset / max(length(offset), 0.001), vec2(-0.6, 0.8)));\n"
		"			float fade = smoothstep(0.0, 0.1, phase) * (1.0 - smoothstep(0.9, 1.0, phase));\n"
		"			bubbles = max(bubbles, ring * glint * fade * (0.12 + min(glow, 1.0) * 0.08));\n"
		"		}\n"
		"	}\n"
		"	density = max(density, bubbles);\n"
		"	fragColor = vec4(0.18, 0.20, 0.21, min(0.24, density));\n"
		"}\n"
	);

	finish_program = gl_compile_program(whole_view_vertex_shader,
		"#version 330\n" + glsl +
		"uniform sampler2D SCENE;\n"
		"uniform uint FRAME;\n"
		"const float EDGE_DARKNESS = 0.6;\n"
		"const float GRAIN = 0.012;\n"
		//based on https://en.wikipedia.org/wiki/SRGB#Transfer_function_(%22gamma%22)
		"vec3 to_srgb(vec3 linear) {\n"
		"	return mix(12.92 * linear, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, linear));\n"
		"}\n"
		"in vec2 uv;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"	vec3 color = texture(SCENE, uv).rgb;\n"
		// darker toward the edges, darkest in the corners
		"	color *= 1.0 - EDGE_DARKNESS * smoothstep(0.5, 1.45, length(2.0 * uv - 1.0));\n"
		// grain hides bands in dark gradients
		"	uvec2 pixel = uvec2(gl_FragCoord.xy);\n"
		"	float grain = random(pixel.x ^ hash(pixel.y ^ hash(FRAME)));\n"
		"	fragColor = vec4(to_srgb(max(color, 0.0)) + GRAIN * (grain - 0.5), 1.0);\n"
		"}\n"
	);
	haze_uniforms = Uniforms(haze_program);
	frame_uniform = glGetUniformLocation(finish_program, "FRAME");
	glUseProgram(finish_program);
	glUniform1i(glGetUniformLocation(finish_program, "SCENE"), 0);
	glUseProgram(0);

	glGenVertexArrays(1, &empty_vertex_array);
	glGenFramebuffers(1, &framebuffer);
	glGenTextures(1, &scene_texture);
	GL_ERRORS();
}

Water::~Water() {
	glDeleteTextures(1, &scene_texture);
	glDeleteFramebuffers(1, &framebuffer);
	glDeleteVertexArrays(1, &empty_vertex_array);
	glDeleteProgram(finish_program);
	glDeleteProgram(haze_program);
}

void Water::begin(Look const &look) {
	glGetIntegerv(GL_VIEWPORT, viewport);
	glm::uvec2 size = glm::uvec2(std::max(1, viewport[2]), std::max(1, viewport[3]));
	float render_scale = std::min(1.0f, std::min(1600.0f / float(size.x), 900.0f / float(size.y)));
	size = glm::max(glm::uvec2(1), glm::uvec2(glm::vec2(size) * render_scale));
	if (size != scene_size) {
		scene_size = size;
		glBindTexture(GL_TEXTURE_2D, scene_texture);
		// float storage keeps dark gradients smooth
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, scene_size.x, scene_size.y, 0, GL_RGBA, GL_FLOAT, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glBindTexture(GL_TEXTURE_2D, 0);
		//based on https://registry.khronos.org/OpenGL-Refpages/gl4/html/glBindFramebuffer.xhtml
		glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_texture, 0);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			throw std::runtime_error("water framebuffer is incomplete");
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glViewport(0, 0, scene_size.x, scene_size.y);
	haze_uniforms.upload(look);

	glDisable(GL_BLEND);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	GL_ERRORS();
}

void Water::haze() {
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(haze_program);
	glBindVertexArray(empty_vertex_array);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glUseProgram(0);
}

void Water::finish() {
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

	glUseProgram(finish_program);
	glUniform1ui(frame_uniform, frame++);
	glDisable(GL_BLEND);
	// add grain after converting to sRGB
	glDisable(GL_FRAMEBUFFER_SRGB);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, scene_texture);
	glBindVertexArray(empty_vertex_array);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glEnable(GL_FRAMEBUFFER_SRGB);
	glUseProgram(0);
	GL_ERRORS();
}
