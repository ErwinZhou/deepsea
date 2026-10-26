#include "UnderwaterPhysics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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
			// drag as exponential decay, so it doesn't depend on the step size
			body.velocity *= std::exp(-body.drag * dt);
			body.position += body.velocity * dt;
		}
		body.force = glm::vec2(0.0f);
	}

	// a few passes, since pushing two bodies apart can push one into a third
	for (int pass = 0; pass < 4; ++pass) {
		for (size_t i = 0; i < bodies.size(); ++i) {
			for (size_t j = i + 1; j < bodies.size(); ++j) {
				resolve_contact(bodies[i], bodies[j]);
			}
		}
		for (auto &body : bodies) {
			for (auto const &box : obstacles) resolve_contact(body, box);
			contain(body);
		}
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
	// centers on top of each other, so pick a direction
	glm::vec2 normal = distance > 0.00001f ? delta / distance : glm::vec2(1.0f, 0.0f);
	glm::vec2 correction = normal * ((radius - distance) / inverse_sum);
	a.position -= correction * a.inverse_mass;
	b.position += correction * b.inverse_mass;

	float closing_speed = glm::dot(b.velocity - a.velocity, normal);
	// already moving apart, so no bounce
	if (closing_speed >= 0.0f) return;
	glm::vec2 impulse = normal * (-(1.0f + restitution) * closing_speed / inverse_sum);
	a.velocity -= impulse * a.inverse_mass;
	b.velocity += impulse * b.inverse_mass;
}

void UnderwaterPhysics::resolve_contact(Body2D &body, StaticBox2D const &box) const {
	if (!body.active || body.inverse_mass <= 0.0f) return;
	glm::vec2 closest = glm::clamp(body.position, box.min, box.max);
	glm::vec2 delta = body.position - closest;
	float distance_squared = glm::dot(delta, delta);
	if (distance_squared >= body.radius * body.radius) return;

	glm::vec2 normal(0.0f);
	float penetration;
	if (distance_squared > 0.00000001f) {
		float distance = std::sqrt(distance_squared);
		normal = delta / distance;
		penetration = body.radius - distance;
	} else {
		// the center is inside the box, so push it out the nearest side
		float nearest = std::numeric_limits<float>::max();
		for (int axis = 0; axis < 2; ++axis) {
			for (float sign : {-1.0f, 1.0f}) {
				float distance = sign < 0.0f ? body.position[axis] - box.min[axis] : box.max[axis] - body.position[axis];
				if (distance >= nearest) continue;
				nearest = distance;
				normal = glm::vec2(0.0f);
				normal[axis] = sign;
			}
		}
		penetration = body.radius + nearest;
	}
	body.position += normal * penetration;
	// stop motion into the box but keep sliding along it
	float closing_speed = glm::dot(body.velocity, normal);
	if (closing_speed < 0.0f) body.velocity -= (1.0f + restitution) * closing_speed * normal;
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
