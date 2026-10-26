#pragma once

#include <glm/glm.hpp>

#include <cmath>

/*
 * camera view, with c = center
 *
 *            +y
 *             ^
 *   +-------------------+
 *   |    +---------+    |
 *   |    |    c    |    | view_size.y
 *   |    |dead_zone|    |
 *   |    +---------+    |
 *   +-------------------+ --> +x
 *   <--- view_size.x --->
 *
 * target inside inner box: camera stays
 * target outside inner box: camera follows
 */
struct Camera2D {
	// world position at the middle of the view
	glm::vec2 center{0.0f};
	// visible width and height in world units
	glm::vec2 view_size{32.0f, 18.0f};
	// half-width and half-height of the inner box
	glm::vec2 dead_zone{6.0f, 3.0f};
	// larger values make the camera catch up faster
	float follow_speed = 8.0f;

	// limit a proposed center so the view stays inside the world
	glm::vec2 constrain(glm::vec2 const &position, glm::vec2 const &bounds_min, glm::vec2 const &bounds_max) const {
		glm::vec2 midpoint = 0.5f * (bounds_min + bounds_max);
		// leave room for half the view on each side, centering if the world is smaller
		glm::vec2 min = glm::min(bounds_min + 0.5f * view_size, midpoint);
		glm::vec2 max = glm::max(bounds_max - 0.5f * view_size, midpoint);
		return glm::clamp(position, min, max);
	}

	// follow the target once it leaves the inner box
	void update(glm::vec2 const &target, float elapsed, glm::vec2 const &bounds_min, glm::vec2 const &bounds_max) {
		if (!std::isfinite(elapsed) || elapsed <= 0.0f) return;
		glm::vec2 offset = target - center;
		// shift only by the distance beyond the inner box
		glm::vec2 desired = center + offset - glm::clamp(offset, -dead_zone, dead_zone);
		desired = constrain(desired, bounds_min, bounds_max);
		// ease toward it at the same speed at any frame rate
		center = glm::mix(center, desired, 1.0f - std::exp(-follow_speed * elapsed));
		center = constrain(center, bounds_min, bounds_max);
	}
};
