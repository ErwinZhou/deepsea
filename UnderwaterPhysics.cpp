#include "UnderwaterPhysics.hpp"

#include <algorithm>
#include <cmath>

void Body2D::add_force(glm::vec2 const &value) {
	if (active && inverse_mass > 0.0f) force += value;
}

void Body2D::apply_impulse(glm::vec2 const &value) {
	if (active && inverse_mass > 0.0f) velocity += value * inverse_mass;
}

void UnderwaterPhysics::step() {
	float dt = float(fixed_step);
	for (auto &body : bodies) {
		if (body.active && body.inverse_mass > 0.0f) {
			glm::vec2 current(
				current_strength.x * std::cos(float(time) * 0.35f + body.position.y * 0.2f),
				current_strength.y * std::sin(float(time) * 0.45f + body.position.x * 0.15f)
			);
			body.add_force(current * body.current_scale);
			body.velocity += body.force * body.inverse_mass * dt;
			// exponential decay gives drag a rate per second rather than per frame
			body.velocity *= std::exp(-body.drag * dt);
			body.position += body.velocity * dt;
		}
		body.force = glm::vec2(0.0f);
	}

	// repeat contacts because separating a pair can push another body into a wall
	for (int pass = 0; pass < 4; ++pass) {
		for (size_t i = 0; i < bodies.size(); ++i) {
			for (size_t j = i + 1; j < bodies.size(); ++j) {
				resolve_contact(bodies[i], bodies[j]);
			}
		}
		for (auto &body : bodies) contain(body);
	}
	time += fixed_step;
}

void UnderwaterPhysics::resolve_contact(Body2D &a, Body2D &b) const {
	if (!a.active || !b.active) return;
	float inverse_sum = a.inverse_mass + b.inverse_mass;
	if (inverse_sum <= 0.0f) return;
	glm::vec2 delta = b.position - a.position;
	float distance_squared = glm::dot(delta, delta);
	float radius = a.radius + b.radius;
	if (distance_squared > radius * radius) return;

	float distance = std::sqrt(distance_squared);
	// coincident centers need a fixed normal to avoid division by zero
	glm::vec2 normal = distance > 0.00001f ? delta / distance : glm::vec2(1.0f, 0.0f);
	glm::vec2 correction = normal * ((radius - distance) / inverse_sum);
	a.position -= correction * a.inverse_mass;
	b.position += correction * b.inverse_mass;

	float closing_speed = glm::dot(b.velocity - a.velocity, normal);
	// overlapping bodies already moving apart must not receive another bounce
	if (closing_speed >= 0.0f) return;
	glm::vec2 impulse = normal * (-(1.0f + restitution) * closing_speed / inverse_sum);
	a.velocity -= impulse * a.inverse_mass;
	b.velocity += impulse * b.inverse_mass;
}

void UnderwaterPhysics::contain(Body2D &body) const {
	if (!body.active || body.inverse_mass <= 0.0f) return;
	for (int axis = 0; axis < 2; ++axis) {
		float low = bounds_min[axis] + body.radius;
		float high = bounds_max[axis] - body.radius;
		if (body.position[axis] < low) {
			body.position[axis] = low;
			if (body.velocity[axis] < 0.0f) body.velocity[axis] *= -restitution;
		} else if (body.position[axis] > high) {
			body.position[axis] = high;
			if (body.velocity[axis] > 0.0f) body.velocity[axis] *= -restitution;
		}
	}
}
