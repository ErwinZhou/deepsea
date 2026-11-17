#pragma once

#include "GL.hpp"

#include <glm/glm.hpp>

struct Tentacles {
	Tentacles();
	~Tentacles();
	Tentacles(Tentacles const &) = delete;
	Tentacles &operator=(Tentacles const &) = delete;

	void setup(glm::vec2 left, glm::vec2 right);
	void update(float elapsed, glm::vec2 const &fish, bool active);
	void draw(glm::mat4 const &world_to_clip, float clock) const;

private:
	glm::vec2 roots[2] = {};
	glm::vec2 target{0.0f};
	float strike_time = 0.0f;
	GLuint buffer = 0;
	GLuint vao = 0;
};
