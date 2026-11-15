#include "level2.hpp"

#include "data_path.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

Level2::Level2(SpriteTexture const &normal_texture, float fish_height, float max_swim_speed)
	: small_fish_texture(normal_texture), small_fish_height(fish_height),
	  small_fish_fast_speed(mimic_speed_multiplier * max_swim_speed) {
	assert(fish_height > 0.0f && max_swim_speed > 0.0f && "small scary fish require a positive size and swim speed");
}

std::optional<size_t> Level2::setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body,
	std::optional<Body2D> const &belly_contents) {
	assert(physics.bodies.size() == 1 && player_body == 0 && physics.bodies[player_body].kind == Body2D::Fish
		&& "level 2 setup requires only the player at body zero");
	physics.bounds_min = glm::vec2(-0.5f * level_length, -0.5f * corridor_height);
	physics.bounds_max = glm::vec2(0.5f * level_length, 0.5f * corridor_height);
	physics.obstacles.clear();
	physics.bodies[player_body].position = glm::vec2(physics.bounds_min.x + entrance_distance, 0.0f);
	previous_player_position = physics.bodies[player_body].position;
	small_fish = SmallScaryFish{};
	small_fish.radius = physics.bodies[player_body].radius;

	camera.view_size = glm::vec2(camera_height * (16.0f / 9.0f), camera_height);
	camera.dead_zone = glm::vec2(5.0f, 2.5f);
	camera.follow_speed = 4.0f;
	camera.center = camera.constrain(physics.bodies[player_body].position, physics.bounds_min, physics.bounds_max);
	std::optional<size_t> carried_body;
	if (belly_contents) {
		assert((belly_contents->kind == Body2D::Rock || belly_contents->kind == Body2D::Jelly)
			&& "only a swallowed rock or jellyfish can transfer between levels");
		Body2D body = *belly_contents;
		body.position = physics.bodies[player_body].position;
		body.velocity = glm::vec2(0.0f);
		body.force = glm::vec2(0.0f);
		body.active = false;
		body.player_spat = false;
		carried_body = physics.bodies.size();
		physics.bodies.push_back(body);
		if (body.kind == Body2D::Jelly) {
			jelly_body = carried_body;
			jellyfish_texture.emplace(data_path("jellyfish.png"));
		}
	}
	return carried_body;
}

void Level2::update(float elapsed, UnderwaterPhysics const &physics, Camera2D const &camera, size_t player_body,
	glm::vec2 const &player_movement, float player_angle) {
	clock += elapsed;
	Body2D const &player = physics.bodies[player_body];
	const glm::vec2 player_step = player.position - previous_player_position;
	previous_player_position = player.position;
	if (!player.active) {
		death_time += elapsed;
		small_fish.velocity = glm::vec2(0.0f);
		return;
	}
	update_small_fish(std::min(elapsed, 0.1f), physics, camera, player, player_step, player_movement, player_angle);
}

bool Level2::small_fish_path_clear(glm::vec2 const &from, glm::vec2 const &to, UnderwaterPhysics const &physics) const {
	const glm::vec2 low = physics.bounds_min + small_fish.radius;
	const glm::vec2 high = physics.bounds_max - small_fish.radius;
	for (unsigned axis = 0; axis < 2; ++axis) {
		if (from[axis] < low[axis] || from[axis] > high[axis] || to[axis] < low[axis] || to[axis] > high[axis]) {
			return false;
		}
	}
	const glm::vec2 delta = to - from;
	for (StaticBox2D const &box : physics.obstacles) {
		//expand walls by the body radius to keep the whole fish clear along the segment
		const glm::vec2 box_min = box.min - small_fish.radius;
		const glm::vec2 box_max = box.max + small_fish.radius;
		float enter = 0.0f;
		float leave = 1.0f;
		for (unsigned axis = 0; axis < 2; ++axis) {
			if (std::abs(delta[axis]) < 0.00001f) {
				if (from[axis] < box_min[axis] || from[axis] > box_max[axis]) {
					enter = 2.0f;
					break;
				}
			} else {
				const float a = (box_min[axis] - from[axis]) / delta[axis];
				const float b = (box_max[axis] - from[axis]) / delta[axis];
				enter = std::max(enter, std::min(a, b));
				leave = std::min(leave, std::max(a, b));
			}
		}
		if (enter <= leave) {
			return false;
		}
	}
	return true;
}

void Level2::react_to_suction(glm::vec2 const &mouth, glm::vec2 const &facing, float range, float cone_cos, float any_side) {
	if (small_fish.state != SmallScaryFish::State::Arriving && small_fish.state != SmallScaryFish::State::Mimicking) {
		return;
	}
	const glm::vec2 offset = small_fish.position - mouth;
	const float distance = glm::length(offset);
	if (distance <= range && (glm::dot(offset, facing) >= distance * cone_cos || distance <= any_side)) {
		small_fish.state = SmallScaryFish::State::Fleeing;
		small_fish.velocity = glm::vec2(0.0f);
	}
}

void Level2::update_small_fish(float elapsed, UnderwaterPhysics const &physics, Camera2D const &camera,
	Body2D const &player, glm::vec2 const &player_step, glm::vec2 const &player_movement, float player_angle) {
	if (elapsed <= 0.0f || small_fish.state == SmallScaryFish::State::Gone) {
		return;
	}
	const glm::vec2 size(small_fish_height * float(small_fish_texture.pixel_size.x) / float(small_fish_texture.pixel_size.y),
		small_fish_height);
	const float sprite_radius = 0.5f * glm::length(size);
	const glm::vec2 view_min = camera.center - 0.5f * camera.view_size;
	const glm::vec2 view_max = camera.center + 0.5f * camera.view_size;
	const glm::vec2 player_facing(std::cos(player_angle), std::sin(player_angle));
	const auto distance_to_edge = [&](glm::vec2 const &position, glm::vec2 const &direction) {
		float distance = std::numeric_limits<float>::max();
		for (unsigned axis = 0; axis < 2; ++axis) {
			if (std::abs(direction[axis]) > 0.00001f) {
				const float edge = direction[axis] > 0.0f ? view_max[axis] : view_min[axis];
				distance = std::min(distance, (edge - position[axis]) / direction[axis]);
			}
		}
		return std::max(distance, 0.0f) + sprite_radius + mimic_offscreen_margin;
	};
	if (small_fish.state == SmallScaryFish::State::Waiting) {
		if (clock < mimic_start_delay) {
			return;
		}
		float screen_length = std::numeric_limits<float>::max();
		for (unsigned axis = 0; axis < 2; ++axis) {
			if (std::abs(player_facing[axis]) > 0.00001f) {
				screen_length = std::min(screen_length, camera.view_size[axis] / std::abs(player_facing[axis]));
			}
		}
		const glm::vec2 offset = -player_facing * (mimic_screen_fraction * screen_length);
		const glm::vec2 target = player.position + offset;
		const glm::vec2 start = player.position - player_facing * distance_to_edge(player.position, -player_facing);
		if (target.x - sprite_radius < view_min.x || target.x + sprite_radius > view_max.x
			|| target.y - sprite_radius < view_min.y || target.y + sprite_radius > view_max.y
			|| !small_fish_path_clear(start, target, physics)) {
			return;
		}
		small_fish.state = SmallScaryFish::State::Arriving;
		small_fish.position = start;
		small_fish.arrival_offset = offset;
		small_fish.angle = player_angle;
		return;
	}
	if (small_fish.state == SmallScaryFish::State::Arriving || small_fish.state == SmallScaryFish::State::Mimicking) {
		const glm::vec2 to_fish = small_fish.position - (player.position - player_step);
		const float distance = glm::length(to_fish);
		//equal displacement preserves the gap, so swimming toward the double is itself a catch attempt
		const bool approaching = glm::dot(player_movement, to_fish) > mimic_approach_cos * distance
			&& glm::dot(player_step, to_fish) > 0.0f;
		if (distance < mimic_approach_distance || approaching) {
			small_fish.state = SmallScaryFish::State::Fleeing;
			small_fish.velocity = glm::vec2(0.0f);
		} else {
			glm::vec2 next = small_fish.position + player_step;
			if (small_fish.state == SmallScaryFish::State::Arriving) {
				const glm::vec2 delta = player.position + small_fish.arrival_offset - small_fish.position;
				const float remaining = glm::length(delta);
				const float travel = small_fish_fast_speed * elapsed;
				next = remaining <= travel ? player.position + small_fish.arrival_offset
					: small_fish.position + delta * (travel / remaining);
				if (remaining <= travel) {
					small_fish.state = SmallScaryFish::State::Mimicking;
				}
			}
			if (small_fish_path_clear(small_fish.position, next, physics)) {
				small_fish.velocity = (next - small_fish.position) / elapsed;
				small_fish.position = next;
				small_fish.angle = player_angle;
				return;
			}
			small_fish.state = SmallScaryFish::State::Fleeing;
			small_fish.velocity = glm::vec2(0.0f);
		}
	}
	if (small_fish.state == SmallScaryFish::State::Fleeing) {
		if (small_fish.position.x + sprite_radius < view_min.x || small_fish.position.x - sprite_radius > view_max.x
			|| small_fish.position.y + sprite_radius < view_min.y || small_fish.position.y - sprite_radius > view_max.y) {
			small_fish.state = SmallScaryFish::State::Gone;
			small_fish.velocity = glm::vec2(0.0f);
			return;
		}
		const auto try_direction = [&](glm::vec2 const &direction) {
			const glm::vec2 exit = small_fish.position + direction * distance_to_edge(small_fish.position, direction);
			if (!small_fish_path_clear(small_fish.position, exit, physics)) {
				return false;
			}
			small_fish.velocity = direction * small_fish_fast_speed;
			return true;
		};
		if (glm::length(small_fish.velocity) < 0.00001f || !try_direction(glm::normalize(small_fish.velocity))) {
			const glm::vec2 away = small_fish.position - player.position;
			const float escape_angle = glm::length(away) > 0.00001f ? std::atan2(away.y, away.x) : player_angle + glm::radians(180.0f);
			small_fish.velocity = glm::vec2(0.0f);
			for (const float turn : {0.0f, 45.0f, -45.0f, 90.0f, -90.0f, 135.0f, -135.0f, 180.0f}) {
				const float angle = escape_angle + glm::radians(turn);
				if (try_direction(glm::vec2(std::cos(angle), std::sin(angle)))) {
					break;
				}
			}
		}
		if (glm::length(small_fish.velocity) > 0.00001f) {
			const glm::vec2 direction = glm::normalize(small_fish.velocity);
			const float travel = std::min(small_fish_fast_speed * elapsed, distance_to_edge(small_fish.position, direction));
			small_fish.position += direction * travel;
			small_fish.angle = std::atan2(small_fish.velocity.y, small_fish.velocity.x);
		}
	}
}

bool Level2::restart_requested() const {
	return death_time > restart_delay;
}

glm::vec3 Level2::prepare_water(Water::Look &look, UnderwaterPhysics const &physics, size_t player_body,
	std::optional<size_t> carried_body) const {
	look.glows.clear();
	look.blockers.clear();
	look.bubble_columns.clear();
	look.glow_radius = glm::vec2(5.5f, 4.0f);
	look.brightness = scene_brightness;
	look.time = clock;
	if (!jelly_body) {
		return glm::vec3(0.0f);
	}
	Body2D const &jelly = physics.bodies[*jelly_body];
	const bool in_belly = carried_body && *carried_body == *jelly_body;
	if (!in_belly && !jelly.active) {
		return glm::vec3(0.0f);
	}
	const glm::vec2 position = in_belly ? physics.bodies[player_body].position : jelly_position(jelly);
	const float strength = jelly_glow + jelly_glow_pulse * std::sin(clock * 1.3f);
	const glm::vec3 source(position, strength);
	look.glows.push_back(source);
	return source;
}

glm::vec2 Level2::jelly_position(Body2D const &jelly) const {
	return jelly.position + glm::vec2(0.18f * std::sin(clock * 0.3f), 0.25f * std::sin(clock * 0.65f));
}

void Level2::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics) const {
	if (small_fish.state != SmallScaryFish::State::Waiting && small_fish.state != SmallScaryFish::State::Gone) {
		const glm::vec2 size(small_fish_height * float(small_fish_texture.pixel_size.x) / float(small_fish_texture.pixel_size.y),
			small_fish_height);
		const bool flip_x = std::abs(small_fish.angle) > glm::radians(90.0f);
		const float angle = flip_x ? small_fish.angle - std::copysign(glm::radians(180.0f), small_fish.angle) : small_fish.angle;
		sprites.draw(small_fish_texture, world_to_clip, small_fish.position, size, angle, flip_x,
			glm::u8vec4(255), 0.0f, DrawSprites::Player);
	}
	if (jelly_body && physics.bodies[*jelly_body].active) {
		assert(jellyfish_texture && "a level 2 jellyfish requires its texture");
		const float pulse = 1.0f + 0.035f * std::sin(clock * 1.3f);
		sprites.draw(*jellyfish_texture, world_to_clip, jelly_position(physics.bodies[*jelly_body]), glm::vec2(1.8f * pulse, 2.15f / pulse),
			0.04f * std::sin(clock * 0.4f), false, glm::u8vec4(175, 185, 190, 165), 0.0f, DrawSprites::Emissive);
	}
}

float Level2::fade_amount() const {
	return std::max(std::max(0.0f, 1.0f - clock / fade_in_time), std::min(1.0f, death_time / death_fade_time));
}
