#pragma once

#include "GL.hpp"
#include "UnderwaterPhysics.hpp"

#include <glm/glm.hpp>

struct Tentacles {
	Tentacles();
	~Tentacles();
	Tentacles(Tentacles const &) = delete;
	Tentacles &operator=(Tentacles const &) = delete;

	void setup(glm::vec2 center);
	void distract(glm::vec2 const &position, bool already_holding = false);
	bool update(float elapsed, glm::vec2 const &fish, bool active);
	void push_fish(Body2D &fish, float clock);
	bool holding_fish() const { return caught; }
	glm::vec2 grip_position() const { return target; }
	bool touches(glm::vec2 const &position, float radius, float clock) const;
	void draw(glm::mat4 const &world_to_clip, float clock) const;
	void draw_backdrop(glm::mat4 const &world_to_clip) const;

private:
	glm::vec2 spine(int arm, float along, float clock) const;
	float width(int arm, float along) const;
	glm::vec2 roots[8] = {};
	glm::vec2 target{0.0f};
	glm::vec2 pearl{0.0f};
	glm::vec2 caught_position{0.0f};
	bool caught = false;
	float caught_time = 0.0f;
	int strike_arm = 0;
	bool distracted = false;
	float distraction = 0.0f;
	float strike_time = 0.0f;
	GLuint buffer = 0;
	GLuint vao = 0;
};
