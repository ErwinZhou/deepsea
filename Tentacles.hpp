#pragma once

#include "GL.hpp"
#include "UnderwaterPhysics.hpp"

#include <glm/glm.hpp>

#include <array>

struct Tentacles {
	Tentacles();
	~Tentacles();
	Tentacles(Tentacles const &) = delete;
	Tentacles &operator=(Tentacles const &) = delete;

	void setup(glm::vec2 center);
	void setup(glm::vec2 center, glm::vec2 guard, glm::vec2 span, float base_width, float solid_fraction);
	void distract(glm::vec2 const &position, bool already_holding = false);
	void release_pearl();
	bool update(float elapsed, glm::vec2 const &fish, bool active);
	void push_fish(Body2D &fish, float clock);
	bool holding_fish() const { return caught; }
	glm::vec2 grip_position() const { return target; }
	bool touches(glm::vec2 const &position, float radius, float clock) const;
	void draw(glm::mat4 const &world_to_clip, float clock, bool grip_only = false) const;
	void draw_backdrop(glm::mat4 const &world_to_clip) const;

	//reach up and hold the big fish's jaws
	void shoot(std::array<glm::vec2, 8> const &roots);
	void hold_big_fish(std::array<glm::vec2, 8> const &targets, float elapsed);
	bool holding_big_fish() const;
	bool shooting = false;
	std::array<glm::vec2, 8> grip_targets{};
	std::array<float, 8> shoot_extend{}; //0 at the root, 1 at the jaw

private:
	glm::vec2 spine(int arm, float along, float clock) const;
	float width(int arm, float along) const;
	glm::vec2 roots[8] = {};
	glm::vec2 guard_center{0.0f};
	glm::vec2 size{19.0f, 16.0f};
	float thickness = 1.25f;
	float solid_base = 0.0f;
	glm::vec2 target{0.0f};
	glm::vec2 pearl{0.0f};
	glm::vec2 caught_position{0.0f};
	bool caught = false;
	float caught_time = 0.0f;
	int strike_arm = 0;
	bool distracted = false;
	float distraction = 0.0f;
	float strike_time = 0.0f;
	float shoot_time = 0.0f; //seconds since shoot, staggers each arm's reach
	glm::vec2 shoot_spine(int arm, float along, float clock) const;
	GLuint buffer = 0;
	GLuint vao = 0;
};
