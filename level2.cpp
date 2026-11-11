#include "level2.hpp"

#include <cassert>

void Level2::setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body) {
	assert(physics.bodies.size() == 1 && player_body == 0 && physics.bodies[player_body].kind == Body2D::Fish
		&& "level 2 setup requires only the player at body zero");
	physics.bounds_min = glm::vec2(-0.5f * level_length, -0.5f * corridor_height);
	physics.bounds_max = glm::vec2(0.5f * level_length, 0.5f * corridor_height);
	physics.obstacles.clear();
	physics.bodies[player_body].position = glm::vec2(physics.bounds_min.x + entrance_distance, 0.0f);

	camera.view_size = glm::vec2(camera_height * (16.0f / 9.0f), camera_height);
	camera.dead_zone = glm::vec2(5.0f, 2.5f);
	camera.follow_speed = 4.0f;
	camera.center = camera.constrain(physics.bodies[player_body].position, physics.bounds_min, physics.bounds_max);
}

void Level2::update(float elapsed) {
	clock += elapsed;
}

void Level2::prepare_water(Water::Look &look) const {
	look.glows.clear();
	look.blockers.clear();
	look.bubble_columns.clear();
	look.glow_radius = glm::vec2(5.5f, 4.0f);
	look.brightness = scene_brightness;
	look.time = clock;
}
