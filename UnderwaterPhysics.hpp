#pragma once

#include <glm/glm.hpp>

#include <vector>

struct Body2D {
	glm::vec2 position{0.0f};
	glm::vec2 velocity{0.0f};
	glm::vec2 force{0.0f};
	// zero inverse mass makes an active body immovable
	float inverse_mass = 1.0f;
	float radius = 0.5f;
	float drag = 0.8f;
	float current_scale = 1.0f;
	bool active = true;
	// what this body is, so game code can tell rocks from the fish
	enum Kind { Fish, Rock } kind = Rock;

	void add_force(glm::vec2 const &value);
	void apply_impulse(glm::vec2 const &value);
};

struct UnderwaterPhysics {
	static constexpr double fixed_step = 1.0 / 120.0;
	std::vector<Body2D> bodies;
	glm::vec2 bounds_min{-16.0f, -9.0f};
	glm::vec2 bounds_max{16.0f, 9.0f};
	glm::vec2 current_strength{0.22f, 0.16f};
	float restitution = 0.15f;

	// forces expire after each fixed step, reapply thrust
	void step();

private:
	double time = 0.0;
	void resolve_contact(Body2D &a, Body2D &b) const;
	void contain(Body2D &body) const;
};
