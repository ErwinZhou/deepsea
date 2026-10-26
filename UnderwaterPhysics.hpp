#pragma once

#include <glm/glm.hpp>

#include <vector>

/**
 * - Body2D, Body2D: fish/rock and rock/rock collisions
 * - Body2D, StaticBox2D: rectangular obstacles
 * - Body2D, StaticPolygon2D: shaped obstacles like the pillar and wall
 */
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

struct StaticBox2D {
	glm::vec2 min{0.0f};
	glm::vec2 max{0.0f};
};

struct StaticPolygon2D {
	std::vector<glm::vec2> vertices;
};

struct UnderwaterPhysics {
	static constexpr double fixed_step = 1.0 / 120.0;
	std::vector<Body2D> bodies;
	std::vector<StaticBox2D> obstacles;
	std::vector<StaticPolygon2D> polygons;
	glm::vec2 bounds_min{-16.0f, -9.0f};
	glm::vec2 bounds_max{16.0f, 9.0f};
	glm::vec2 current_strength{0.22f, 0.16f};
	float restitution = 0.15f;

	// forces expire after each fixed step, reapply thrust
	void step();

private:
	double time = 0.0;
	// circle against circle, such as the fish bumping a loose rock or two rocks colliding
	void resolve_contact(Body2D &a, Body2D &b) const;
	// circle against a stationary rectangle aligned with the world axes
	void resolve_contact(Body2D &body, StaticBox2D const &box) const;
	// circle against a stationary outline, used by the hanging rock and right-side wall
	void resolve_contact(Body2D &body, StaticPolygon2D const &polygon) const;
	void contain(Body2D &body) const;
};
