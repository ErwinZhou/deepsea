#include "level2.hpp"

#include "data_path.hpp"
#include "load_save_png.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <vector>

//opening centers measured in the 1180 by 820 artwork from its upper-left corner
static constexpr glm::vec2 TunnelEntranceUV(618.0f / 1180.0f, 558.0f / 820.0f);
static constexpr glm::vec2 TunnelEndUV(991.0f / 1180.0f, 0.0f);
//four-pixel cells keep collisions close to the silhouette without closing the narrow opening
static constexpr unsigned TunnelCollisionPixels = 4;

Level2::Level2() : tunnel_texture(data_path("level2/tunnel.png")), key_texture(data_path("level2/key.png")) {
}

void Level2::setup_tunnel(UnderwaterPhysics &physics) {
	tunnel_size = tunnel_width * glm::vec2(1.0f, float(tunnel_texture.pixel_size.y) / float(tunnel_texture.pixel_size.x));
	const float entrance_x = physics.bounds_min.x + tunnel_entrance_fraction * (physics.bounds_max.x - physics.bounds_min.x);
	tunnel_center = glm::vec2(entrance_x - (TunnelEntranceUV.x - 0.5f) * tunnel_size.x,
		physics.bounds_max.y - 0.5f * tunnel_size.y);
	key_position = tunnel_center + glm::vec2((TunnelEndUV.x - 0.5f) * tunnel_size.x,
		(0.5f - TunnelEndUV.y) * tunnel_size.y - key_end_inset);

	glm::uvec2 size(0);
	std::vector<glm::u8vec4> pixels;
	load_png(data_path("level2/tunnel.png"), &size, &pixels, UpperLeftOrigin);
	const glm::vec2 origin = tunnel_center - 0.5f * tunnel_size;
	const glm::vec2 pixel_size = tunnel_size / glm::vec2(size);
	const unsigned columns = (size.x + TunnelCollisionPixels - 1) / TunnelCollisionPixels;
	for (unsigned y = 0; y < size.y; y += TunnelCollisionPixels) {
		const unsigned sample_y = std::min(y + TunnelCollisionPixels / 2, size.y - 1);
		bool in_run = false;
		unsigned run_start = 0;
		for (unsigned column = 0; column <= columns; ++column) {
			const unsigned sample_x = std::min(column * TunnelCollisionPixels + TunnelCollisionPixels / 2, size.x - 1);
			const bool solid = column < columns && pixels[size_t(sample_y) * size.x + sample_x].a >= 128;
			if (solid && !in_run) {
				run_start = column * TunnelCollisionPixels;
				in_run = true;
			} else if (!solid && in_run) {
				//png rows run downward, while the cave's y axis runs upward
				const unsigned run_end = std::min(column * TunnelCollisionPixels, size.x);
				const unsigned bottom = std::min(y + TunnelCollisionPixels, size.y);
				physics.obstacles.push_back(StaticBox2D{
					origin + pixel_size * glm::vec2(float(run_start), float(size.y - bottom)),
					origin + pixel_size * glm::vec2(float(run_end), float(size.y - y))});
				in_run = false;
			}
		}
	}
	if (physics.obstacles.empty()) {
		throw std::runtime_error("level2/tunnel.png has no solid tunnel walls");
	}
}

std::optional<size_t> Level2::setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body,
	std::optional<Body2D> const &belly_contents) {
	assert(physics.bodies.size() == 1 && player_body == 0 && physics.bodies[player_body].kind == Body2D::Fish
		&& "level 2 setup requires only the player at body zero");
	physics.bounds_min = glm::vec2(-0.5f * level_length, -0.5f * corridor_height);
	physics.bounds_max = glm::vec2(0.5f * level_length, 0.5f * corridor_height);
	physics.obstacles.clear();
	setup_tunnel(physics);
	physics.bodies[player_body].position = glm::vec2(physics.bounds_min.x + entrance_distance, 0.0f);

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

void Level2::update(float elapsed, bool player_alive) {
	clock += elapsed;
	if (!player_alive) {
		death_time += elapsed;
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
	sprites.draw(tunnel_texture, world_to_clip, tunnel_center, tunnel_size);
	const glm::vec2 key_size(key_height * float(key_texture.pixel_size.x) / float(key_texture.pixel_size.y), key_height);
	sprites.draw(key_texture, world_to_clip, key_position, key_size, 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::LightOnly);
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
