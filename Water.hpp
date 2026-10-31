#pragma once

#include "GL.hpp"

#include <glm/glm.hpp>
#include <cstddef>
#include <string>
#include <vector>

// lighting and haze for the cave
struct Water {
	Water();
	~Water();
	Water(Water const &) = delete;
	Water &operator=(Water const &) = delete;

	struct Look {
		glm::vec2 view_min = glm::vec2(0.0f);
		glm::vec2 view_max = glm::vec2(0.0f);
		// light positions and their strength
		std::vector< glm::vec3 > glows;
		glm::vec2 glow_radius = glm::vec2(1.0f);
		float brightness = 1.0f;
		float time = 0.0f;
		std::vector< glm::vec4 > blockers;
		std::vector< glm::vec3 > bubble_columns;
	};
	static constexpr size_t max_glows = 8;
	static constexpr size_t max_blockers = 5;
	static constexpr size_t max_columns = 3;

	struct Uniforms {
		Uniforms() = default;
		explicit Uniforms(GLuint program);
		void upload(Look const &look) const;
	private:
		GLuint program = 0;
		GLint view_min = -1, view_max = -1;
		GLint glow_count = -1, glows = -1, glow_radius = -1;
		GLint brightness = -1, time = -1;
		GLint blocker_count = -1, blockers = -1;
		GLint column_count = -1, columns = -1;
	};

	// draw into the scene texture, capped at 1600 by 900
	void begin(Look const &look);
	void haze();
	// draw the scene to the window
	void finish();

	// shader functions used by both water and sprites
	static std::string const glsl;

private:
	GLuint framebuffer = 0;
	GLuint scene_texture = 0;
	glm::uvec2 scene_size = glm::uvec2(0);
	GLint viewport[4] = {0, 0, 0, 0};
	GLuint frame = 0;

	GLuint haze_program = 0;
	GLuint finish_program = 0;
	GLint frame_uniform = -1;
	Uniforms haze_uniforms;
	// haze and the final pass use the same fullscreen triangle
	GLuint empty_vertex_array = 0;
};
