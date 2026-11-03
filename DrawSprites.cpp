// Sprite shader setup is adapted from the course base ColorTextureProgram.cpp.
#include "DrawSprites.hpp"

#include "gl_compile_program.hpp"
#include "load_save_png.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstddef>
#include <cmath>
#include <vector>

SpriteTexture::SpriteTexture(std::string const &filename) {
	std::vector<glm::u8vec4> pixels;
	load_png(filename, &pixel_size, &pixels, LowerLeftOrigin);
	glGenTextures(1, &texture);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, pixel_size.x, pixel_size.y,
		0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	//based on https://registry.khronos.org/OpenGL-Refpages/gl4/html/glGenerateMipmap.xhtml
	glGenerateMipmap(GL_TEXTURE_2D);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_2D, 0);
}

SpriteTexture::~SpriteTexture() {
	glDeleteTextures(1, &texture);
}

DrawSprites::DrawSprites() {
	std::string const material_defines =
		"#define EMISSIVE " + std::to_string(Emissive) + "\n" +
		"#define SURFACE " + std::to_string(Surface) + "\n" +
		"#define BACKGROUND " + std::to_string(Background) + "\n" +
		"#define STONE " + std::to_string(Stone) + "\n" +
		"#define PREDATOR " + std::to_string(Predator) + "\n" +
		"#define PLAYER " + std::to_string(Player) + "\n";

	program = gl_compile_program(
		"#version 330\n"
		"uniform mat4 OBJECT_TO_CLIP;\n"
		"in vec4 Position;\n"
		"in vec2 TexCoord;\n"
		"out vec2 texCoord;\n"
		"out vec2 clip;\n"
		"void main() {\n"
		"	gl_Position = OBJECT_TO_CLIP * Position;\n"
		"	texCoord = TexCoord;\n"
		"	clip = gl_Position.xy;\n"
		"}\n"
	,
		"#version 330\n" + Water::glsl + material_defines +
		"uniform sampler2D TEX;\n"
		"uniform vec4 TINT;\n"
		"uniform float FADE;\n"
		"uniform int MATERIAL;\n"
		"uniform vec4 TEX_RECT;\n"
		"uniform mat4 OBJECT_TO_CLIP;\n"
		"uniform vec2 PREDATOR_LIGHT;\n"
		"uniform float PREDATOR_LIGHT_STRENGTH;\n"
		"in vec2 texCoord;\n"
		"in vec2 clip;\n"
		"out vec4 fragColor;\n"
		"void main() {\n"
		"	vec2 sample_uv = mix(TEX_RECT.xy, TEX_RECT.zw, texCoord);\n"
		"	vec4 color = texture(TEX, sample_uv) * TINT;\n"
		"	float eye_halo = 0.0;\n"
		"	float eye_core = 0.0;\n"
		"	float eye_light = 0.0;\n"
		"	if (MATERIAL == PLAYER) {\n"
		// manually tweaked eye glow based on fish pngs
		"		vec2 eye_offset = sample_uv - vec2(0.7267, 0.5952);\n"
		"		eye_halo = exp(-dot(eye_offset, eye_offset) / 0.0025);\n"
		"		eye_core = exp(-dot(eye_offset, eye_offset) / 0.00015);\n"
		// eye glow
		"		eye_light = exp(-dot(eye_offset, eye_offset) / 0.03);\n"
		"	}\n"
		"	if (color.a < 0.0039 && eye_halo < 0.01) discard;\n"
		"	vec2 world = mix(VIEW_MIN, VIEW_MAX, 0.5 * clip + 0.5);\n"
		"	bool surface = MATERIAL == SURFACE || MATERIAL == STONE || MATERIAL == PREDATOR || MATERIAL == PLAYER;\n"
		"	float lit = MATERIAL != EMISSIVE ? light(world, surface) : 1.0;\n"
		"	float shade = surface ? 0.45 + 0.55 * lit : lit;\n"
		// per material lighting for each asset
		"	if (MATERIAL == PLAYER) shade = 0.03 + 0.55 * lit + 0.35 * eye_light;\n"
		"	float wet_shine = 0.0;\n"
		"	if (MATERIAL == PREDATOR) {\n"
		// the art is flat colors, so treat its blurred brightness as height. that makes each tooth a rounded shape the light can catch
		// based on https://en.wikipedia.org/wiki/Normal_mapping
		"		const float BLUR = 5.0;\n"
		"		vec2 reach = pow(2.0, BLUR) / vec2(textureSize(TEX, 0));\n"
		"		vec2 slope = vec2(\n"
		"			dot(textureLod(TEX, sample_uv + vec2(reach.x, 0.0), BLUR).rgb, vec3(1.0)) - dot(textureLod(TEX, sample_uv - vec2(reach.x, 0.0), BLUR).rgb, vec3(1.0)),\n"
		"			dot(textureLod(TEX, sample_uv + vec2(0.0, reach.y), BLUR).rgb, vec3(1.0)) - dot(textureLod(TEX, sample_uv - vec2(0.0, reach.y), BLUR).rgb, vec3(1.0)));\n"
		"		vec2 extent = VIEW_MAX - VIEW_MIN;\n"
		"		vec2 world_slope = normalize(OBJECT_TO_CLIP[0].xy * extent) * slope.x + normalize(OBJECT_TO_CLIP[1].xy * extent) * slope.y;\n"
		"		vec3 normal = normalize(vec3(-3.0 * world_slope, 1.0));\n"
		// the small fish's eye is the light, a little in front of the jaws. it only reaches a short way, so the rest of the mouth stays black
		"		vec2 to_eye = PREDATOR_LIGHT - world;\n"
		"		vec3 light_direction = normalize(vec3(to_eye, 3.0));\n"
		"		float reach_eye = PREDATOR_LIGHT_STRENGTH * exp(-dot(to_eye, to_eye) / (9.0 * 9.0));\n"
		"		shade = 0.015 + 0.3 * lit + 1.6 * reach_eye * max(0.0, dot(normal, light_direction));\n"
		// based on https://en.wikipedia.org/wiki/Blinn%E2%80%93Phong_reflection_model
		"		vec3 halfway = normalize(light_direction + vec3(0.0, 0.0, 1.0));\n"
		"		wet_shine = 0.5 * reach_eye * pow(max(0.0, dot(normal, halfway)), 30.0) * color.a * dot(color.rgb, vec3(1.0 / 3.0));\n"
		"	}\n"
		"	if (MATERIAL == STONE) shade = 0.12 + 0.6 * lit;\n"
		"	color.rgb *= shade;\n"
		// Light modulation informed by https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-2-rendering-water-caustics
		"	if (MATERIAL == BACKGROUND) {\n"
		"		vec2 p = world * 2.1 + vec2(TIME * 0.17, -TIME * 0.11);\n"
		"		float a = sin(p.x + sin(p.y * 1.21 + TIME * 0.12));\n"
		"		float b = sin(p.y + sin(p.x * 1.31 - TIME * 0.10));\n"
		"		float ripple = pow(max(0.0, 1.0 - abs(a + b) * 0.7), 10.0);\n"
		"		color.rgb += vec3(0.008, 0.009, 0.009) * ripple * max(0.0, lit - BRIGHTNESS * DIM);\n"
		"	}\n"
		"	color.rgb += vec3(wet_shine);\n"
		// Rim light placement follows https://blog.playdead.com/articles/the_lighting_of_inside/lighting_of_inside.html
		"	if (surface) {\n"
		"		vec2 texel = max(1.0 / vec2(textureSize(TEX, 0)), 1.5 * fwidth(sample_uv));\n"
		"		vec2 edge = -vec2(texture(TEX, sample_uv + vec2(texel.x, 0.0)).a - texture(TEX, sample_uv - vec2(texel.x, 0.0)).a,\n"
		"			texture(TEX, sample_uv + vec2(0.0, texel.y)).a - texture(TEX, sample_uv - vec2(0.0, texel.y)).a);\n"
		"		float edge_size = length(edge);\n"
		"		if (edge_size > 0.01) {\n"
		"			vec2 extent = VIEW_MAX - VIEW_MIN;\n"
		"			vec2 axis_x = normalize(OBJECT_TO_CLIP[0].xy * extent);\n"
		"			vec2 axis_y = normalize(OBJECT_TO_CLIP[1].xy * extent);\n"
		"			vec2 normal = normalize(axis_x * edge.x + axis_y * edge.y);\n"
		"			float rim = 0.0;\n"
		"			for (int i = 0; i < GLOW_COUNT; ++i) {\n"
		"				vec2 offset = GLOWS[i].xy - world;\n"
		"				vec2 falloff = offset / GLOW_RADIUS;\n"
		"				float facing = max(0.0, dot(normal, offset / max(length(offset), 0.001)));\n"
		"				rim += GLOWS[i].z * exp(-dot(falloff, falloff)) * facing * facing;\n"
		"			}\n"
		"			color.rgb += vec3(0.10, 0.12, 0.125) * min(1.0, edge_size) * rim * BRIGHTNESS;\n"
		"		}\n"
		"	}\n"
		// far away things fade into the water around them
		"	if (FADE > 0.0) {\n"
		"		float water_light = MATERIAL == BACKGROUND ? lit : light(world, false);\n"
		"		color.rgb = mix(color.rgb, WATER * water_light, FADE);\n"
		"	}\n"
		"	if (MATERIAL == PLAYER) {\n"
		"		color.rgb += vec3(0.045, 0.055, 0.055) * eye_halo + vec3(0.55, 0.60, 0.60) * eye_core;\n"
		"		color.a = max(color.a, 0.10 * eye_halo);\n"
		"	}\n"
		"	fragColor = color;\n"
		"}\n"
	);
	OBJECT_TO_CLIP_mat4 = glGetUniformLocation(program, "OBJECT_TO_CLIP");
	TINT_vec4 = glGetUniformLocation(program, "TINT");
	FADE_float = glGetUniformLocation(program, "FADE");
	MATERIAL_int = glGetUniformLocation(program, "MATERIAL");
	look_uniforms = Water::Uniforms(program);
	PREDATOR_LIGHT_vec2 = glGetUniformLocation(program, "PREDATOR_LIGHT");
	PREDATOR_LIGHT_STRENGTH_float = glGetUniformLocation(program, "PREDATOR_LIGHT_STRENGTH");
	TEX_RECT_vec4 = glGetUniformLocation(program, "TEX_RECT");
	glUseProgram(program);
	glUniform1i(glGetUniformLocation(program, "TEX"), 0);
	glUseProgram(0);
	GLuint Position_vec4 = glGetAttribLocation(program, "Position");
	GLuint TexCoord_vec2 = glGetAttribLocation(program, "TexCoord");

	struct Vertex {
		glm::vec2 position;
		glm::vec2 tex_coord;
	};
	Vertex const vertices[] = {
		{{-0.5f, -0.5f}, {0.0f, 0.0f}},
		{{ 0.5f, -0.5f}, {1.0f, 0.0f}},
		{{ 0.5f,  0.5f}, {1.0f, 1.0f}},
		{{-0.5f, -0.5f}, {0.0f, 0.0f}},
		{{ 0.5f,  0.5f}, {1.0f, 1.0f}},
		{{-0.5f,  0.5f}, {0.0f, 1.0f}}
	};
	glGenBuffers(1, &vertex_buffer);
	glGenVertexArrays(1, &vertex_array);
	glBindVertexArray(vertex_array);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glVertexAttribPointer(Position_vec4, 2, GL_FLOAT,
		GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, position)));
	glEnableVertexAttribArray(Position_vec4);
	glVertexAttribPointer(TexCoord_vec2, 2, GL_FLOAT,
		GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, tex_coord)));
	glEnableVertexAttribArray(TexCoord_vec2);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

DrawSprites::~DrawSprites() {
	glDeleteVertexArrays(1, &vertex_array);
	glDeleteBuffers(1, &vertex_buffer);
	glDeleteProgram(program);
}

void DrawSprites::set_look(Water::Look const &look) const {
	look_uniforms.upload(look);
}

void DrawSprites::set_predator_light(glm::vec2 const &position, float strength) const {
	glUseProgram(program);
	glUniform2fv(PREDATOR_LIGHT_vec2, 1, glm::value_ptr(position));
	glUniform1f(PREDATOR_LIGHT_STRENGTH_float, strength);
	glUseProgram(0);
}

void DrawSprites::draw(SpriteTexture const &texture, glm::mat4 const &world_to_clip,
	glm::vec2 const &center, glm::vec2 const &size, float angle, bool flip_x,
	glm::u8vec4 const &tint, float fade, Material material, glm::vec4 const &uv_rect) const {
	glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f));
	transform = glm::rotate(transform, angle, glm::vec3(0.0f, 0.0f, 1.0f));
	transform = glm::scale(transform, glm::vec3(flip_x ? -size.x : size.x, size.y, 1.0f));
	glm::mat4 object_to_clip = world_to_clip * transform;
	float extent_x = 0.5f * (std::abs(object_to_clip[0][0]) + std::abs(object_to_clip[1][0]));
	float extent_y = 0.5f * (std::abs(object_to_clip[0][1]) + std::abs(object_to_clip[1][1]));
	if (std::abs(object_to_clip[3][0]) > 1.0f + extent_x || std::abs(object_to_clip[3][1]) > 1.0f + extent_y) return;
	glUseProgram(program);
	glUniformMatrix4fv(OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(object_to_clip));
	glm::vec4 color = glm::vec4(tint) / 255.0f;
	glUniform4fv(TINT_vec4, 1, glm::value_ptr(color));
	glUniform1f(FADE_float, fade);
	glUniform1i(MATERIAL_int, GLint(material));
	glUniform4fv(TEX_RECT_vec4, 1, glm::value_ptr(uv_rect));
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture.texture);
	glBindVertexArray(vertex_array);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
}
