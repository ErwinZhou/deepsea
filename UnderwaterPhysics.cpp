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

float Body2D::wheel_clearance(glm::vec2 point, glm::vec2 &normal) const {
	const glm::vec2 offset = point - position;
	const float distance = glm::length(offset);
	normal = distance > 0.00001f ? offset / distance : glm::vec2(1.0f, 0.0f);

	//scale the rim and handles to the current wheel size
	const float scale = radius / 348.0f;
	const float rim = 250.0f * scale;
	float clearance = distance - rim;

	if (distance < rim || distance > radius * 1.5f) {
		return clearance;
	}

	//counterclockwise tips around the wheel, starting from the right
	static constexpr float tips[] = {299, 351, 354, 352, 300, 309, 343, 294};
	const float grip = 22.0f * scale;

	for (unsigned i = 0; i < 8; ++i) {
		const float angle = wheel_angle + float(i) * glm::radians(45.0f);
		const glm::vec2 axis(std::cos(angle), std::sin(angle));
		//find the closest point along the handle
		const float along = glm::clamp(glm::dot(offset, axis), rim - grip, tips[i] * scale - grip);
		const glm::vec2 delta = offset - axis * along;
		const float length = glm::length(delta);

		if (length - grip < clearance) {
			clearance = length - grip;
			normal = length > 0.00001f ? delta / length : glm::vec2(-axis.y, axis.x);
		}
	}
	return clearance;
}

void UnderwaterPhysics::resolve_contact(Body2D &a, Body2D &b) const {
	if (!a.active || !b.active) return;

	float inverse_sum = a.inverse_mass + b.inverse_mass;
	if (inverse_sum <= 0.0f) return;

	glm::vec2 delta = b.position - a.position;
	//account for the straight section of the pipe
	float reach = a.half_segment + b.half_segment;
	delta.x -= glm::clamp(delta.x, -reach, reach);
	
	float distance_squared = glm::dot(delta, delta);
	float radius = a.radius + b.radius;
	float distance = std::sqrt(distance_squared);

	glm::vec2 normal = distance > 0.00001f ? delta / distance : glm::vec2(1.0f, 0.0f);
	float penetration = radius - distance;

	if (a.kind == Body2D::Wheel) {
		penetration = b.radius - a.wheel_clearance(b.position, normal);
	} else if (b.kind == Body2D::Wheel) {
		penetration = a.radius - b.wheel_clearance(a.position, normal);
		//keep the contact normal pointing from a to b
		normal = -normal;
	}

	if (penetration < 0.0f) return;
	//split the overlap by inverse mass so the fixed wheel stays put
	glm::vec2 correction = normal * (penetration / inverse_sum);
	a.position -= correction * a.inverse_mass;
	b.position += correction * b.inverse_mass;

	float closing_speed = glm::dot(b.velocity - a.velocity, normal);
	// already moving apart, so no bounce
	if (closing_speed >= 0.0f) return;
	//a hit on the wheel handles starts a turn
	if (a.kind == Body2D::Wheel || b.kind == Body2D::Wheel) {
		Body2D &wheel = a.kind == Body2D::Wheel ? a : b;
		const Body2D &body = a.kind == Body2D::Wheel ? b : a;
		const glm::vec2 offset = body.position - wheel.position;
		const float along_rim = offset.x * body.velocity.y - offset.y * body.velocity.x;
		wheel.spin += std::copysign(-closing_speed / wheel.radius, along_rim);
	}

	glm::vec2 impulse = normal * (-(1.0f + restitution) * closing_speed / inverse_sum);
	a.velocity -= impulse * a.inverse_mass;
	b.velocity += impulse * b.inverse_mass;
}

void UnderwaterPhysics::resolve_contact(Body2D &body, StaticBox2D const &wall) const {
	if (!body.active || body.inverse_mass <= 0.0f) return;

	const glm::vec2 reach(body.half_segment, 0.0f);
	const StaticBox2D box{wall.min - reach, wall.max + reach};

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
		const float extent = body.radius + (axis == 0 ? body.half_segment : 0.0f);
		float low = bounds_min[axis] + extent;
		float high = bounds_max[axis] - extent;

		if (body.position[axis] < low) {
			body.position[axis] = low;

			if (body.velocity[axis] < 0.0f) body.velocity[axis] *= -restitution;
		} else if (body.position[axis] > high) {
			body.position[axis] = high;

			if (body.velocity[axis] > 0.0f) body.velocity[axis] *= -restitution;
		}
	}
}
