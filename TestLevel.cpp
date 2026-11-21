#include "TestLevel.hpp"

#include "data_path.hpp"
#include "load_save_png.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include <vector>

//inside lamp rests on this ledge
static constexpr glm::vec2 InsideFlashlightUV(828.0f / 1180.0f, 292.0f / 820.0f);
//lens and pivot heights for flashlight
static constexpr float FlashlightLensUV = 100.0f / 682.0f;
static constexpr float FlashlightPivotUV = 545.0f / 682.0f;
//handle and head bounds relative to the pivot
static constexpr StaticBox2D FlashlightParts[] = {
	{glm::vec2(-52.0f, -99.0f) / 682.0f, glm::vec2(52.0f, 255.0f) / 682.0f},
	{glm::vec2(-158.0f, 255.0f) / 682.0f, glm::vec2(158.0f, 495.0f) / 682.0f}
};
//4px cells keep collisions close to the silhouette without closing the narrow opening
static constexpr unsigned TunnelCollisionPixels = 4;
struct FlashlightContact {
	glm::vec2 point{0.0f};
	glm::vec2 normal{0.0f};
	float depth = 0.0f;
};

static FlashlightContact flashlight_contact(glm::vec2 const &position, float radius, StaticBox2D const &part) {
	const glm::vec2 closest = glm::clamp(position, part.min, part.max);
	const glm::vec2 delta = position - closest;
	const float distance = glm::length(delta);
	if (distance >= radius) {
		return {};
	}
	if (distance > 0.00001f) {
		return {closest, delta / distance, radius - distance};
	}

	//an overlapping center leaves through the nearest side
	const glm::vec2 gaps = glm::min(position - part.min, part.max - position);
	const unsigned axis = gaps.x < gaps.y ? 0 : 1;
	glm::vec2 normal(0.0f);
	normal[axis] = position[axis] - part.min[axis] < part.max[axis] - position[axis] ? -1.0f : 1.0f;
	return {closest + normal * gaps[axis], normal, radius + gaps[axis]};
}

TestLevel::TestLevel()
	: tunnel_texture(data_path("level2/tunnel.png")), key_texture(data_path("level2/key.png")),
	  flashlight_texture(data_path("level2/flashlight.png")), flashlight_base_texture(data_path("small_rock.png")) {
}

void TestLevel::setup_tunnel(UnderwaterPhysics &physics) {
	tunnel_size = tunnel_width * glm::vec2(1.0f, float(tunnel_texture.pixel_size.y) / float(tunnel_texture.pixel_size.x));
	const float tunnel_left = physics.bounds_min.x + 3.0f;
	//leave open water below the tunnel so the puzzle is reachable from spawn
	tunnel_center = glm::vec2(tunnel_left + 0.5f * tunnel_size.x,
		physics.bounds_max.y - 4.0f - 0.5f * tunnel_size.y);

	glm::uvec2 size(0);
	std::vector<glm::u8vec4> pixels;
	load_png(data_path("level2/tunnel.png"), &size, &pixels, UpperLeftOrigin);
	tunnel_alpha.resize(pixels.size());
	for (size_t i = 0; i < pixels.size(); ++i) {
		tunnel_alpha[i] = pixels[i].a;
	}
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

void TestLevel::setup_flashlight(UnderwaterPhysics &physics) {
	flashlights = {};
	flashlights[0].anchor = glm::vec2(-14.0f, physics.bounds_min.y - flashlight_burial_depth);
	flashlights[1].anchor = tunnel_center + (glm::vec2(InsideFlashlightUV.x, 1.0f - InsideFlashlightUV.y) - 0.5f) * tunnel_size;
	flashlights[1].height = inside_flashlight_height;
	flashlights[1].range = inside_flashlight_range;

	//static base blocks swimming and cannot be swallowed
	physics.obstacles.push_back(StaticBox2D{
		glm::vec2(flashlights[0].anchor.x - 0.5f * flashlight_base_collision_size.x, physics.bounds_min.y - flashlight_base_size.y),
		glm::vec2(flashlights[0].anchor.x + 0.5f * flashlight_base_collision_size.x, physics.bounds_min.y + flashlight_base_collision_size.y)});
	for (Flashlight &flashlight : flashlights) {
		aim_flashlight(flashlight, physics);
	}
}

void TestLevel::check_puzzle_hits(UnderwaterPhysics &physics) {
	puzzle.check_puzzle_hits(physics);
}

void TestLevel::aim_flashlight(Flashlight &flashlight, UnderwaterPhysics const &physics) {
	const float angle = glm::radians(flashlight.tilt);
	const glm::vec2 direction(-std::sin(angle), std::cos(angle));
	flashlight.center = flashlight.anchor + direction * ((FlashlightPivotUV - 0.5f) * flashlight.height);
	flashlight.light.origin = flashlight.anchor + direction * ((FlashlightPivotUV - FlashlightLensUV) * flashlight.height);
	flashlight.light.direction = direction;
	const float scale = flashlight.height / flashlight_height;
	flashlight.light.half_width = flashlight_beam_half_width * scale;
	flashlight.light.spill_radius = flashlight_spill_radius * scale;
	flashlight.light.range = flashlight.range;
	flashlight.light.strength = flashlight_beam_strength;

	const glm::vec2 sideways(direction.y, -direction.x);
	const float light_half_width = flashlight.light.half_width + flashlight.light.spill_radius;
	const glm::vec2 tunnel_min = tunnel_center - 0.5f * tunnel_size;
	const glm::vec2 pixel_size = tunnel_size / glm::vec2(tunnel_texture.pixel_size);
	//0.5px steps avoid skipping thin parts of the silhouette
	const float light_step = 0.5f * std::min(pixel_size.x, pixel_size.y);
	for (size_t i = 0; i < Water::beam_samples; ++i) {
		const float across = (2.0f * float(i) / float(Water::beam_samples - 1) - 1.0f) * light_half_width;
		const glm::vec2 origin = flashlight.light.origin + sideways * across;
		float reach = flashlight.range;
		for (unsigned axis = 0; axis < 2; ++axis) {
			if (std::abs(direction[axis]) > 0.00001f) {
				const float edge = direction[axis] > 0.0f ? physics.bounds_max[axis] : physics.bounds_min[axis];
				reach = std::min(reach, (edge - origin[axis]) / direction[axis]);
			}
		}

		//sample opacity to keep shadows smooth
		for (float travel = 0.0f; travel < reach; travel += light_step) {
			const glm::vec2 pixel = (origin + direction * travel - tunnel_min) / pixel_size;
			if (pixel.x < 0.0f || pixel.y < 0.0f || pixel.x >= float(tunnel_texture.pixel_size.x) || pixel.y >= float(tunnel_texture.pixel_size.y)) {
				continue;
			}
			const unsigned x = unsigned(pixel.x);
			const unsigned y = tunnel_texture.pixel_size.y - 1 - unsigned(pixel.y);
			if (tunnel_alpha[size_t(y) * tunnel_texture.pixel_size.x + x] >= 128) {
				reach = travel;
				break;
			}
		}
		flashlight.light.reaches[i] = std::max(0.0f, reach);
	}
}

void TestLevel::push_flashlight(UnderwaterPhysics &physics, size_t player_body, glm::vec2 const &movement, float elapsed) {
	assert(player_body < physics.bodies.size() && "Push flashlight requires a valid player");
	Body2D &player = physics.bodies[player_body];
	if (!player.active) {
		return;
	}
	for (Flashlight &flashlight : flashlights) {
		push_flashlight(flashlight, physics, player, movement, elapsed);
	}
}

void TestLevel::push_flashlight(Flashlight &flashlight, UnderwaterPhysics &physics, Body2D &player,
	glm::vec2 const &movement, float elapsed) {
	//resolve contact again after turning
	for (unsigned pass = 0; pass < 3; ++pass) {
		const float angle = glm::radians(flashlight.tilt);
		const glm::vec2 sideways(std::cos(angle), std::sin(angle));
		const glm::vec2 upward(-sideways.y, sideways.x);
		const glm::vec2 offset = player.position - flashlight.anchor;
		const glm::vec2 position(glm::dot(offset, sideways), glm::dot(offset, upward));
		FlashlightContact contact;

		for (StaticBox2D const &part : FlashlightParts) {
			const StaticBox2D bounds{part.min * flashlight.height, part.max * flashlight.height};
			const FlashlightContact candidate = flashlight_contact(position, player.radius, bounds);
			if (candidate.depth > contact.depth) {
				contact = candidate;
			}
		}
		if (contact.depth <= 0.0f) {
			return;
		}
		const glm::vec2 outward = sideways * contact.normal.x + upward * contact.normal.y;
		//pushing farther from the base turns it faster
		const float pressure = pass == 0 ? std::max(0.0f, -glm::dot(movement, outward)) : 0.0f;
		const float lever = contact.point.y * contact.normal.x - contact.point.x * contact.normal.y;
		const float lens_distance = (FlashlightPivotUV - FlashlightLensUV) * flashlight.height;
		const float turn = pressure * glm::clamp(lever / lens_distance, -1.0f, 1.0f);
		const float tilt = glm::clamp(flashlight.tilt + turn * flashlight_turn_speed * elapsed,
			flashlight_tilt_limits.x, flashlight_tilt_limits.y);
		if (tilt != flashlight.tilt) {
			flashlight.tilt = tilt;
			flashlight.turned = true;
			continue;
		}
		player.position += outward * contact.depth;
		player.position = glm::clamp(player.position, physics.bounds_min + player.radius, physics.bounds_max - player.radius);
		//stop inward motion but allow sliding
		const float closing_speed = glm::dot(player.velocity, outward);
		if (closing_speed < 0.0f) {
			player.velocity -= closing_speed * outward;
		}
	}
}

void TestLevel::setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body) {
	physics.bounds_min = glm::vec2(-44.0f, -12.0f);
	physics.bounds_max = glm::vec2(84.0f, 16.0f);
	physics.bodies[player_body].position = glm::vec2(-8.0f, physics.bounds_min.y + 7.0f);
	physics.obstacles.clear();
	setup_tunnel(physics);
	setup_flashlight(physics);
	size_t key = physics.bodies.size();
	physics.bodies.push_back({.position = glm::vec2(12.0f, physics.bounds_min.y + 2.5f),
		.radius = 0.6f, .current_scale = 0.0f, .kind = Body2D::Key});
	key_body = key;
	float floor = physics.bounds_min.y;
	puzzle.setup(physics, glm::vec2(-4.0f, floor + 0.5f * Level2Puzzle::clam_height),
		glm::vec2(34.0f, floor + 0.38f * Level2Puzzle::chest_height),
		glm::vec2(66.0f, floor + 0.5f * Level2Puzzle::scanner_height), key);

	camera.view_size = glm::vec2(camera_height * (16.0f / 9.0f), camera_height);
	camera.dead_zone = glm::vec2(5.0f, 2.5f);
	camera.follow_speed = 4.0f;
	camera.center = camera.constrain(glm::vec2(0.0f), physics.bounds_min, physics.bounds_max);
}

void TestLevel::update(float elapsed, UnderwaterPhysics &physics, size_t player_body, Camera2D const &camera) {
	clock += elapsed;
	puzzle.update(elapsed, physics, player_body, camera);
	for (Flashlight &flashlight : flashlights) {
		if (flashlight.turned) {
			aim_flashlight(flashlight, physics);
			flashlight.turned = false;
		}
	}
	if (!physics.bodies[player_body].active) death_time += elapsed;
}

bool TestLevel::restart_requested() const {
	return death_time > restart_delay;
}

void TestLevel::draw_backdrop(glm::mat4 const &world_to_clip) const {
	puzzle.draw_backdrop(world_to_clip);
}

bool TestLevel::show_dead_fish() const {
	return puzzle.show_dead_fish();
}

void TestLevel::prepare_water(Water::Look &look) const {
	look.glows.clear();
	puzzle.add_lights(look);
	look.beams.clear();
	for (Flashlight const &flashlight : flashlights) look.beams.push_back(flashlight.light);
	look.blockers.clear();
	look.bubble_columns.clear();
	look.glow_radius = glm::vec2(5.0f, 4.0f);
	look.brightness = scene_brightness;
	look.time = clock;
}

void TestLevel::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics) const {
	for (Flashlight const &flashlight : flashlights) {
		const glm::vec2 size(flashlight.height * float(flashlight_texture.pixel_size.x) / float(flashlight_texture.pixel_size.y),
			flashlight.height);
		sprites.draw(flashlight_texture, world_to_clip, flashlight.center, size, glm::radians(flashlight.tilt));
	}
	//rock hides the buried handles
	sprites.draw(tunnel_texture, world_to_clip, tunnel_center, tunnel_size, 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Tunnel);
	sprites.draw(flashlight_base_texture, world_to_clip, glm::vec2(flashlights[0].anchor.x, physics.bounds_min.y),
		flashlight_base_size, 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	puzzle.draw(sprites, world_to_clip, physics);
	Body2D const &key = physics.bodies[key_body];
	if (key.active) {
		sprites.draw(key_texture, world_to_clip, key.position, glm::vec2(1.8f),
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
}

float TestLevel::fade_amount() const {
	return std::max(std::max(0.0f, 1.0f - clock / fade_in_time), puzzle.fade_amount());
}
