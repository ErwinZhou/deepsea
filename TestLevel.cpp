#include "TestLevel.hpp"

#include "data_path.hpp"
#include "load_save_png.hpp"
#include "Load.hpp"
#include "Sound.hpp"

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
static constexpr float ChestSize = 6.4f;
static constexpr float ScannerSize = 8.6f;
static constexpr float ClamSize = 7.0f;
static constexpr float ClamWarningTime = 0.8f;
static constexpr float ClamClosedTime = 1.5f;
static constexpr float PipeSize = 2.2f;

static Load<Sound::Sample> arm_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_rock_hit.opus"));
});
static Load<Sound::Sample> capture_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_chomp.opus"));
});

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
	: tunnel_texture(data_path("level2/tunnel.png")), pipe_texture(data_path("level2/pipe.png")),
	  pearl_texture(data_path("level2/pearl.png")), clam_open_texture(data_path("level2/shell_open.png")),
	  clam_closed_texture(data_path("level2/shell_closed.png")), coral_texture(data_path("coral.png")),
	  key_texture(data_path("level2/key.png")), trash_texture(data_path("level2/metal_trash.png")),
	  flashlight_texture(data_path("level2/flashlight.png")), flashlight_base_texture(data_path("small_rock.png")),
	  chest_texture(data_path("level2/chest.png")), keycard_texture(data_path("level2/keycard.png")),
	  scanner_locked_texture(data_path("level2/key_scanner_locked.png")),
	  scanner_unlocked_texture(data_path("level2/key_scanner_unlocked.png")) {
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

void TestLevel::setup_puzzle(UnderwaterPhysics &physics) {
	float floor = physics.bounds_min.y;
	clam_center = glm::vec2(-4.0f, floor + 0.5f * ClamSize);
	chest_center = glm::vec2(34.0f, floor + 0.38f * ChestSize);
	scanner_center = glm::vec2(66.0f, floor + 0.50f * ScannerSize);
	chest = {glm::vec2(chest_center.x - 2.7f, floor), chest_center + glm::vec2(2.7f, 1.9f)};
	scanner = {glm::vec2(scanner_center.x - 2.2f, floor), scanner_center + glm::vec2(2.3f, 4.1f)};
	physics.obstacles.push_back(chest);
	physics.obstacles.push_back(scanner);
	//only the lower shell is solid; the open mouth must be reachable
	physics.obstacles.push_back({clam_center + glm::vec2(-2.9f, -3.5f), clam_center + glm::vec2(2.9f, -2.4f)});
	tentacles.setup(glm::vec2(chest_center.x, floor - 0.1f));
	pipe_body = physics.bodies.size();
	physics.bodies.push_back({.position = glm::vec2(-8.8f, floor + 3.3f),
		.radius = 0.6f, .current_scale = 0.0f, .kind = Body2D::Pipe});
	pearl_body = physics.bodies.size();
	physics.bodies.push_back({.position = clam_center + glm::vec2(0.0f, -1.2f),
		.radius = 0.4f, .current_scale = 0.0f, .active = false, .kind = Body2D::Pearl});
	card_body = physics.bodies.size();
	physics.bodies.push_back({.position = chest_center + glm::vec2(0.0f, 4.2f),
		.radius = 0.46f, .current_scale = 0.0f, .active = false, .kind = Body2D::Keycard});
	key_body = physics.bodies.size();
	physics.bodies.push_back({.position = glm::vec2(12.0f, floor + 2.5f),
		.radius = 0.6f, .current_scale = 0.0f, .kind = Body2D::Key});
	corals = {glm::vec2(-10.5f, floor + 0.7f), glm::vec2(0.0f, floor + 0.7f),
		glm::vec2(14.0f, floor + 0.7f), glm::vec2(30.0f, floor + 0.7f), glm::vec2(68.0f, floor + 0.7f)};
	outlets = {glm::vec2(54.0f, 10.0f), glm::vec2(60.0f, 10.0f), glm::vec2(75.0f, 10.0f)};
	//keep the body list fixed while the puzzle is running
	for (size_t &index : trash_bodies) {
		index = physics.bodies.size();
		physics.bodies.push_back({.radius = 0.55f, .drag = 1.2f, .current_scale = 0.0f,
			.active = false, .kind = Body2D::Trash});
	}
}

void TestLevel::check_puzzle_hits(UnderwaterPhysics &physics) {
	auto hits = [](Body2D const &body, StaticBox2D const &target) {
		if (!body.active || !body.player_spat) return false;
		glm::vec2 offset = body.position - glm::clamp(body.position, target.min, target.max);
		glm::vec2 contact;
		return glm::dot(offset, offset) <= body.radius * body.radius ||
			std::isfinite(hit_time(body, target, float(UnderwaterPhysics::fixed_step), &contact));
	};
	Body2D &pipe = physics.bodies[pipe_body];
	Body2D &pearl = physics.bodies[pearl_body];
	Body2D &card = physics.bodies[card_body];
	Body2D &key = physics.bodies[key_body];
	StaticBox2D mouth{clam_center + glm::vec2(-2.5f, -1.9f), clam_center + glm::vec2(2.5f, 0.0f)};
	if (!clam_wedged && clam_time < ClamWarningTime && hits(pipe, mouth)) {
		pipe.active = false;
		pipe.player_spat = false;
		clam_wedged = true;
		clam_time = -1.0f;
		pearl.active = true;
	}
	if (!tentacles_distracted && pearl.active && pearl.player_spat &&
		tentacles.touches(pearl.position, pearl.radius, clock)) {
		pearl.active = false;
		pearl.player_spat = false;
		tentacles_distracted = true;
		held_pearl = pearl.position;
		tentacles.distract(held_pearl);
	}
	if (!chest_open && tentacles_distracted && hits(key, chest)) {
		key.active = false;
		key.player_spat = false;
		chest_open = true;
		card.active = true;
	}
	if (machine_time < 0.0f && hits(card, scanner)) {
		card.active = false;
		card.player_spat = false;
		machine_time = 0.0f;
	}
	for (size_t index : trash_bodies) {
		Body2D &trash = physics.bodies[index];
		if (trash.active && !trash.player_spat) trash.add_force(glm::vec2(0.0f, -6.0f));
	}
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
	setup_puzzle(physics);

	camera.view_size = glm::vec2(camera_height * (16.0f / 9.0f), camera_height);
	camera.dead_zone = glm::vec2(5.0f, 2.5f);
	camera.follow_speed = 4.0f;
	camera.center = camera.constrain(glm::vec2(0.0f), physics.bounds_min, physics.bounds_max);
}

void TestLevel::update(float elapsed, UnderwaterPhysics &physics, size_t player_body, Camera2D const &camera) {
	clock += elapsed;
	Body2D &fish = physics.bodies[player_body];
	StaticBox2D mouth{clam_center + glm::vec2(-2.5f, -1.9f), clam_center + glm::vec2(2.5f, 0.0f)};
	bool in_mouth = glm::distance(fish.position, glm::clamp(fish.position, mouth.min, mouth.max)) < fish.radius;
	if (!clam_wedged) {
		if (clam_time < 0.0f && fish.active && in_mouth) clam_time = 0.0f;
		if (clam_time >= 0.0f) {
			clam_time += elapsed;
			if (clam_time >= ClamWarningTime && in_mouth) fish.active = false;
			if (clam_time > ClamWarningTime + ClamClosedTime) clam_time = -1.0f;
		}
	}
	if (clam_wedged) wedge_time += elapsed;
	//let the player see the chest and guarding arms before an attack can start
	glm::vec2 half_view = 0.5f * camera.view_size;
	bool in_view = chest_center.x > camera.center.x - half_view.x + 5.0f &&
		chest_center.x < camera.center.x + half_view.x - 5.0f;
	if (tentacles.update(elapsed, fish.position, fish.active && !chest_open && in_view)) {
		glm::vec2 source = glm::mix(fish.position, chest_center, 0.5f);
		Sound::play_3D(*arm_sound, 0.3f, glm::vec3(source, 0.0f), 18.0f);
	}
	tentacles.push_fish(fish, clock);
	if (!fish.active && death_time == 0.0f) {
		Sound::play_3D(*capture_sound, 0.3f, glm::vec3(fish.position, 0.0f), 18.0f);
	}
	if (tentacles.holding_fish()) fish.position = tentacles.grip_position();
	if (machine_time >= 0.0f && fish.active) {
		machine_time += elapsed;
		if (machine_time >= next_drop) {
			next_drop += 0.9f;
			for (size_t index : trash_bodies) {
				Body2D &trash = physics.bodies[index];
				bool unused = drops_spawned < trash_bodies.size() && index == trash_bodies[drops_spawned];
				//reuse settled debris, never an item the player has swallowed
				bool settled = drops_spawned >= trash_bodies.size() && trash.active &&
					trash.position.y < physics.bounds_min.y + 1.5f;
				if (!unused && !settled) continue;
				trash.position = outlets[drops_spawned % outlets.size()] - glm::vec2(0.0f, 0.55f);
				trash.velocity = glm::vec2(0.0f, -2.0f);
				trash.force = glm::vec2(0.0f);
				trash.active = true;
				trash.player_spat = false;
				++drops_spawned;
				break;
			}
		}
	}
	for (Flashlight &flashlight : flashlights) {
		if (flashlight.turned) {
			aim_flashlight(flashlight, physics);
			flashlight.turned = false;
		}
	}
	if (!fish.active) death_time += elapsed;
}

bool TestLevel::restart_requested() const {
	return death_time > restart_delay;
}

void TestLevel::draw_backdrop(glm::mat4 const &world_to_clip) const {
	tentacles.draw_backdrop(world_to_clip);
}

bool TestLevel::show_dead_fish() const {
	return tentacles.holding_fish() && death_time < death_hold_time + death_fade_time;
}

void TestLevel::prepare_water(Water::Look &look) const {
	look.glows.clear();
	for (size_t i = 0; i < corals.size(); ++i) {
		float strength = i == 3 ? 0.35f : 0.6f;
		look.glows.emplace_back(corals[i] + glm::vec2(0.0f, 1.0f), strength);
	}
	if (machine_time >= 0.0f) {
		for (glm::vec2 const &outlet : outlets) look.glows.emplace_back(outlet, 0.25f);
	}
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
	for (glm::vec2 const &coral : corals) {
		sprites.draw(coral_texture, world_to_clip, coral, glm::vec2(2.3f),
			0.0f, false, glm::u8vec4(150, 150, 150, 255), 0.0f, DrawSprites::Emissive);
	}
	for (glm::vec2 const &outlet : outlets) {
		sprites.draw(pipe_texture, world_to_clip, outlet + glm::vec2(0.0f, 3.85f), glm::vec2(5.5f),
			-0.7853982f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
	for (size_t i = 0; i < trash_bodies.size(); ++i) {
		Body2D const &trash = physics.bodies[trash_bodies[i]];
		if (trash.active) {
			sprites.draw(trash_texture, world_to_clip, trash.position, glm::vec2(1.6f),
				0.25f * std::sin(clock + float(i)), i % 2 != 0, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
		}
	}
	bool closed = !clam_wedged && clam_time >= ClamWarningTime;
	float warning = !clam_wedged && clam_time >= 0.0f ? std::min(clam_time / ClamWarningTime, 1.0f) : 0.0f;
	float height = closed ? ClamSize : ClamSize - 0.6f * warning;
	if (clam_wedged) {
		//the upper shell settles onto the pipe while the lower shell stays put
		constexpr float seam = 0.46f;
		float settle = 0.2f * glm::smoothstep(0.0f, 0.35f, wedge_time);
		sprites.draw(clam_open_texture, world_to_clip, clam_center + glm::vec2(0.0f, 0.5f * seam * ClamSize - settle),
			glm::vec2(ClamSize, (1.0f - seam) * ClamSize), 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone,
			glm::vec4(0.0f, seam, 1.0f, 1.0f));
		sprites.draw(clam_open_texture, world_to_clip, clam_center + glm::vec2(0.0f, (seam - 1.0f) * 0.5f * ClamSize),
			glm::vec2(ClamSize, seam * ClamSize), 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone,
			glm::vec4(0.0f, 0.0f, 1.0f, seam));
	} else {
		sprites.draw(closed ? clam_closed_texture : clam_open_texture, world_to_clip,
			clam_center + glm::vec2(0.0f, 0.5f * (height - ClamSize)), glm::vec2(ClamSize, height),
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
	Body2D const &pipe = physics.bodies[pipe_body];
	if (pipe.active || clam_wedged) {
		sprites.draw(pipe_texture, world_to_clip, clam_wedged ? clam_center + glm::vec2(1.35f, -0.55f) : pipe.position,
			glm::vec2(PipeSize), clam_wedged ? 0.12f : 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
		if (clam_wedged) {
			//the front lip hides the lower end of the prop between the shells
			constexpr float lip = 0.30f;
			sprites.draw(clam_open_texture, world_to_clip, clam_center + glm::vec2(0.0f, (lip - 1.0f) * 0.5f * ClamSize),
				glm::vec2(ClamSize, lip * ClamSize), 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone,
				glm::vec4(0.0f, 0.0f, 1.0f, lip));
		}
	}
	Body2D const &pearl = physics.bodies[pearl_body];
	if (pearl.active || (!clam_wedged && !closed)) {
		sprites.draw(pearl_texture, world_to_clip, pearl.position, glm::vec2(1.2f),
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
	Body2D const &key = physics.bodies[key_body];
	if (key.active) {
		sprites.draw(key_texture, world_to_clip, key.position, glm::vec2(1.8f),
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
	tentacles.draw(world_to_clip, clock);
	if (chest_open) {
		//lift the lid using the top half of the chest art
		constexpr float seam = 0.45f;
		sprites.draw(chest_texture, world_to_clip, chest_center + glm::vec2(0.0f, (seam - 1.0f) * 0.5f * ChestSize),
			glm::vec2(ChestSize, seam * ChestSize), 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone,
			glm::vec4(0.0f, 0.0f, 1.0f, seam));
		sprites.draw(chest_texture, world_to_clip, chest_center + glm::vec2(-0.55f, 0.5f * seam * ChestSize + 0.85f),
			glm::vec2(ChestSize, (1.0f - seam) * ChestSize), 0.23f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone,
			glm::vec4(0.0f, seam, 1.0f, 1.0f));
	} else {
		sprites.draw(chest_texture, world_to_clip, chest_center, glm::vec2(ChestSize),
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
	if (tentacles_distracted) {
		sprites.draw(pearl_texture, world_to_clip, held_pearl, glm::vec2(1.2f),
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
	Body2D const &card = physics.bodies[card_body];
	if (card.active) {
		sprites.draw(keycard_texture, world_to_clip, card.position, glm::vec2(1.85f),
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
	bool unlocked = machine_time >= 0.0f;
	sprites.draw(unlocked ? scanner_unlocked_texture : scanner_locked_texture,
		world_to_clip, scanner_center, glm::vec2(ScannerSize),
		0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
}

float TestLevel::fade_amount() const {
	return std::max(std::max(0.0f, 1.0f - clock / fade_in_time),
		glm::clamp((death_time - death_hold_time) / death_fade_time, 0.0f, 1.0f));
}
