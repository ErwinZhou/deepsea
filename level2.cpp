#include "level2.hpp"

#include "data_path.hpp"
#include "load_save_png.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <random>

#include <stdexcept>
#include <vector>

//based on https://en.cppreference.com/w/cpp/numeric/random/uniform_real_distribution,
//used for:
//(1) small scary fish random appear seconds interval generation
//(2) small scary fish group sizes random generation
//(3) small scary fish group initial positioning
static std::mt19937 mimic_rng{std::random_device{}()};

static float mimic_random_between(float low, float high) {
	return std::uniform_real_distribution<float>(low, high)(mimic_rng);
}

//upper center in the asset from the top left corner
static constexpr glm::vec2 TunnelEndUV(991.0f / 1180.0f, 0.0f);
//entrance coordinates from the asset
static constexpr glm::vec2 TunnelEntranceUV(637.0f / 1180.0f, 541.0f / 820.0f);
//inside lamp rests on this ledge
static constexpr glm::vec2 InsideFlashlightUV(848.0f / 1180.0f, 292.0f / 820.0f);
//mounts follow the passage surfaces
static constexpr glm::vec3 CoralMounts[] = {
	{783.0f / 1180.0f, 336.0f / 820.0f, -16.0f},
	{934.0f / 1180.0f, 74.0f / 820.0f, 180.0f}
};

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

// pushes move the flashlight angle
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

Level2::Level2(SpriteTexture const &normal_texture, float fish_height, float max_swim_speed)
	: tunnel_texture(data_path("level2/tunnel.png")), key_texture(data_path("level2/key.png")),
	coral_texture(data_path("coral.png")),
	flashlight_texture(data_path("level2/flashlight.png")), flashlight_base_texture(data_path("small_rock.png")),
	worm{SpriteTexture(data_path("level2/worm.png")),
		Worm::speed_fraction * max_swim_speed, {}, {}, {}},
	small_fish_texture(normal_texture), small_fish_height(fish_height),
	small_fish_fast_speed(mimic_speed_multiplier * max_swim_speed),
	fish_eyes_texture(data_path("level2/small_fish_eyes.png")),
	fish_group_speed(fish_group_speed_multiplier * max_swim_speed),
	door_texture(data_path("level2/door.png")), door_wheel_texture(data_path("level2/door_wheel.png")),
	pipe_texture(data_path("level2/pipe.png")) {
}
	
void Level2::setup_tunnel(UnderwaterPhysics &physics) {
	tunnel_size = tunnel_width * glm::vec2(1.0f, float(tunnel_texture.pixel_size.y) / float(tunnel_texture.pixel_size.x));
	const float tunnel_left = physics.bounds_min.x + tunnel_start_fraction * (physics.bounds_max.x - physics.bounds_min.x);
	tunnel_center = glm::vec2(tunnel_left + 0.5f * tunnel_size.x,
		physics.bounds_max.y - 0.5f * tunnel_size.y);
	key_position = tunnel_center + glm::vec2((TunnelEndUV.x - 0.5f) * tunnel_size.x - key_left_offset,
		(0.5f - TunnelEndUV.y) * tunnel_size.y - key_end_inset);

	// the tail inside the upper wall
	const glm::vec2 spawn_uv(888.0f / 1180.0f, 1.0f - 15.0f / 820.0f);
	worm.head = tunnel_center + (spawn_uv - 0.5f) * tunnel_size;
	worm.trail = {worm.head - glm::vec2(5.0f, 0.0f), worm.head};

	for (size_t i = 0; i < corals.size(); ++i) {
		glm::vec3 const &mount = CoralMounts[i];
		const float angle = glm::radians(mount.z);
		const glm::vec2 outward(-std::sin(angle), std::cos(angle));
		corals[i] = {
			tunnel_center + (glm::vec2(mount.x, 1.0f - mount.y) - 0.5f) * tunnel_size
				+ outward * (0.5f * coral_size - coral_base_inset),
			angle
		};
	}

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

	// draw the tunnel
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

void Level2::setup_flashlight(UnderwaterPhysics &physics) {
	/**
	 * We have two flashlight, one inside and one outside
	 * Used in the tunnel puzzle
	 */
	flashlights = {};

	const float entrance_x = tunnel_center.x + (TunnelEntranceUV.x - 0.5f) * tunnel_size.x;
	flashlights[0].anchor = glm::vec2(entrance_x + flashlight_entrance_offset, physics.bounds_min.y - flashlight_burial_depth);
	flashlights[1].anchor = tunnel_center + (glm::vec2(InsideFlashlightUV.x, 1.0f - InsideFlashlightUV.y) - 0.5f) * tunnel_size;
	flashlights[1].tilt = -flashlight_initial_tilt; //aim up and right for the inside tunnel
	flashlights[1].height = inside_flashlight_height;
	flashlights[1].range = inside_flashlight_range;

	//static base blocks swimming and can not be swallowed
	physics.obstacles.push_back(StaticBox2D{
		glm::vec2(flashlights[0].anchor.x - 0.5f * flashlight_base_collision_size.x, physics.bounds_min.y - flashlight_base_size.y),
		glm::vec2(flashlights[0].anchor.x + 0.5f * flashlight_base_collision_size.x, physics.bounds_min.y + flashlight_base_collision_size.y)});
	
	for (Flashlight &flashlight : flashlights) {
		aim_flashlight(flashlight, physics);
	}
}

void Level2::aim_flashlight(Flashlight &flashlight, UnderwaterPhysics const &physics) {
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

void Level2::push_flashlight(UnderwaterPhysics &physics, size_t player_body, glm::vec2 const &movement, float elapsed) {
	/**
	 * Put flashlight changes it angle
	 */
	assert(player_body < physics.bodies.size() && "Push flashlight requires a valid player");

	for (Body2D &body : physics.bodies) {
		// all the moveable body in the water should be able to push it, such as rock or key
		if (!body.active) {
			continue;
		}
		const bool player = &body == &physics.bodies[player_body];
		if (!player && body.kind != Body2D::Key && body.kind != Body2D::Rock) {
			continue;
		}

		for (Flashlight &flashlight : flashlights) {
			const glm::vec2 push = player ? movement : body.velocity;
			push_flashlight(flashlight, physics, body, push, elapsed);
		}
	}
}

void Level2::push_flashlight(Flashlight &flashlight, UnderwaterPhysics &physics, Body2D &body,
	glm::vec2 const &movement, float elapsed) {
	//resolve contact again after turning
	for (unsigned pass = 0; pass < 3; ++pass) {
		const float angle = glm::radians(flashlight.tilt);
		const glm::vec2 sideways(std::cos(angle), std::sin(angle));
		const glm::vec2 upward(-sideways.y, sideways.x);
		const glm::vec2 offset = body.position - flashlight.anchor;
		const glm::vec2 position(glm::dot(offset, sideways), glm::dot(offset, upward));

		FlashlightContact contact;
		for (StaticBox2D const &part : FlashlightParts) {
			const StaticBox2D bounds{part.min * flashlight.height, part.max * flashlight.height};
			const FlashlightContact candidate = flashlight_contact(position, body.radius, bounds);
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

		body.position += outward * contact.depth;
		body.position = glm::clamp(body.position, physics.bounds_min + body.radius, physics.bounds_max - body.radius);
		
		//stop inward motion but allow sliding
		const float closing_speed = glm::dot(body.velocity, outward);
		if (closing_speed < 0.0f) {
			body.velocity -= closing_speed * outward;
		}
	}
}

std::optional<size_t> Level2::setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body,
	std::optional<Body2D> const &belly_contents) {
	/**
	 * Whole Level setup
	 * (1) Tunnel
	 * (2) Small scary fish
	 * 
	 * TODO:
	 * (1) Octopus with chest
	 * (2) Door
	 * (3) Pipes with trash
	 * (4) Clamp with Pearl
	 * (5) Keymachine
	 */
	assert(physics.bodies.size() == 1 && player_body == 0 && physics.bodies[player_body].kind == Body2D::Fish
		&& "level 2 setup requires only the player at body zero");

	/**
	 * Belly logic of carrying the items to level2
	 */
	std::optional<size_t> carried_body;
	if (belly_contents) {
		assert((belly_contents->kind == Body2D::Rock || belly_contents->kind == Body2D::Jelly)
			&& "Wrong Item to Level2: bro, what did it bring to the next level...");
		Body2D body = *belly_contents;
		body.position = physics.bodies[player_body].position;

		// if a stone, clears its speed and force
		body.velocity = glm::vec2(0.0f);
		body.force = glm::vec2(0.0f);
		body.active = false; //swallowed contents remain hidden
		body.player_spat = false;
		carried_body = physics.bodies.size();
		physics.bodies.push_back(body);
		if (body.kind == Body2D::Jelly) {
			jelly_body = carried_body;
			jellyfish_texture.emplace(data_path("jellyfish.png"));
		}
	}

	/**
	 * Tunnel setup
	 */
	//the cave is centered at the origin, so each boundary is half its full width or height away
	physics.bounds_min = glm::vec2(-0.5f * level_length, -0.5f * corridor_height);
	physics.bounds_max = glm::vec2(0.5f * level_length, 0.5f * corridor_height);
	physics.obstacles.clear();
	setup_tunnel(physics);
	setup_flashlight(physics);
	Body2D key{};
	key.position = key_position;
	key.radius = key_radius;
	key.current_scale = 0.0f;
	key.kind = Body2D::Key;
	key_body = physics.bodies.size();
	physics.bodies.push_back(key);
	physics.bodies[player_body].position = glm::vec2(physics.bounds_min.x + entrance_distance, 0.0f);

	/**
	 * Small scary fish logic setup
	 */
	previous_player_position = physics.bodies[player_body].position;
	small_fish = SmallScaryFish{}; // initialize to appear only one at a time
	small_fish.radius = physics.bodies[player_body].radius;
	//draw one deadline in level seconds, making sure a safe arrival path
	next_mimic_time = mimic_random_between(mimic_first_delay.x, mimic_first_delay.y);
	mimic_encounters_finished = false;

	//place seven fish at the shared encounter position
	const glm::vec2 group_center(
		physics.bounds_min.x + mimic_end_fraction * (physics.bounds_max.x - physics.bounds_min.x),
		physics.bounds_max.y - fish_group_depth_fraction * (physics.bounds_max.y - physics.bounds_min.y));

	//spaced starting points prevent overlap after random shifts
	const std::array<glm::vec2, fish_group_count> group_offsets{
		glm::vec2(-3.5f, 3.5f), glm::vec2(3.5f, 3.5f),
		glm::vec2(-7.0f, 0.0f), glm::vec2(0.0f, 0.0f), glm::vec2(7.0f, 0.0f),
		glm::vec2(-3.5f, -3.5f), glm::vec2(3.5f, -3.5f)};

	//give each fish a different size range
	std::array<float, fish_group_count> group_scales{};
	const float scale_step = (fish_group_scale_range.y - fish_group_scale_range.x) / float(fish_group_count);
	for (size_t i = 0; i < group_scales.size(); ++i) {
		const float low = fish_group_scale_range.x + float(i) * scale_step;
		const float high = std::min(fish_group_scale_range.y, low + scale_step);
		group_scales[i] = mimic_random_between(low, high);
	}

	//randomize which positions get the larger fish
	std::shuffle(group_scales.begin(), group_scales.end(), mimic_rng);

	//start every fish still and facing right
	glm::vec2 group_average(0.0f);
	for (size_t i = 0; i < fish_group.size(); ++i) {
		fish_group[i] = SmallScaryFish{};
		fish_group[i].state = SmallScaryFish::State::Dormant;
		fish_group[i].scale = group_scales[i];

		//grow the collision body with the drawn fish
		fish_group[i].radius = physics.bodies[player_body].radius * group_scales[i];

		//shift each fish randomly around its starting point
		fish_group[i].position = group_center + group_offsets[i] + glm::vec2(
			mimic_random_between(-fish_group_position_offset, fish_group_position_offset),
			mimic_random_between(-fish_group_position_offset, fish_group_position_offset));
		group_average += fish_group[i].position;
	}

	//keep the group centered at its original position
	group_average /= float(fish_group.size());
	for (SmallScaryFish &fish : fish_group) {
		fish.position += group_center - group_average;
	}

	fish_group_triggered = false;
	fish_group_turn_remaining = 0.0f;

	/**
	 * Door and wheel setup
	 */
	//align the visible right and bottom edges with the level bounds
	door_center = glm::vec2(physics.bounds_max.x - door_height * (887.0f - 512.0f) / 2048.0f,
		physics.bounds_min.y + door_height * (2020.0f - 1024.0f) / 2048.0f);
	Body2D wheel{};
	wheel.kind = Body2D::Wheel;
	wheel.position = door_center;
	wheel.inverse_mass = 0.0f;
	wheel.radius = wheel_radius;
	wheel_body = physics.bodies.size();
	physics.bodies.push_back(wheel);
	wheel_angle = 0.0f;
	door_open = false;

	Body2D pipe{};
	pipe.kind = Body2D::Pipe;
	pipe.radius = pipe_sprite_size * 0.06f;
	pipe.half_segment = pipe_sprite_size * 0.63f;
	pipe.active = false;
	pipe.position = door_center;
	pipe_body = physics.bodies.size();
	physics.bodies.push_back(pipe);

	// camera logic
	camera.view_size = glm::vec2(camera_height * (16.0f / 9.0f), camera_height); // reference view in world units 16:9
	//half etxtends a central box so the player does not move the camera
	camera.dead_zone = glm::vec2(5.0f, 2.5f);
	camera.follow_speed = 4.0f;
	camera.center = camera.constrain(physics.bodies[player_body].position, physics.bounds_min, physics.bounds_max);

	return carried_body;
}

void Level2::push_door_wheel(UnderwaterPhysics &physics, size_t player_body, glm::vec2 const &movement, float elapsed) {
	if (door_open) {
		// already open, just leave it there
		return;
	}

	const Body2D &player = physics.bodies[player_body];
	const glm::vec2 offset = player.position - door_center;
	const float distance = glm::length(offset);
	// wheel is pushable
	Body2D &wheel = physics.bodies[wheel_body];
	glm::vec2 contact_normal;
	if (player.active && distance > 0.0f && wheel.wheel_clearance(player.position, contact_normal) <= player.radius + 0.001f) {
		const glm::vec2 tangent(-offset.y / distance, offset.x / distance);
		wheel_angle += glm::dot(movement, tangent) * wheel_turn_speed * elapsed;
	}
	// wheel again changes
	wheel_angle += wheel.spin * elapsed;
	wheel.wheel_angle = wheel_angle;
	wheel.spin *= std::exp(-3.0f * elapsed);
	if (wheel_angle < wheel_open_angle) {
		return;
	}

	// enough angle, then open the door and active and launch the pipe
	door_open = true;
	wheel.active = false;
	Body2D &pipe = physics.bodies[pipe_body];
	pipe.active = true;
	pipe.apply_impulse(glm::vec2(-pipe_launch_impulse, 0.0f));
}

void Level2::update_worm(UnderwaterPhysics &physics, size_t player_body, glm::vec2 previous_position, float elapsed) {
	Body2D &player = physics.bodies[player_body];
	if (!player.active) {
		return;
	}

	//the first suck wakes the worm for the rest of the chase
	if (!worm.awake && key_body && !physics.bodies[*key_body].active) {
		worm.awake = true;
		shake_left = shake_duration;
	}
	if (!worm.awake) {
		return;
	}

	const glm::vec2 previous_head = worm.head; // make its body infinite long
	const glm::vec2 entrance = tunnel_center + (glm::vec2(TunnelEntranceUV.x, 1.0f - TunnelEntranceUV.y) - 0.5f) * tunnel_size;
	worm.chase(player.position, entrance, elapsed);

	//fit the bite to the oval inside the teeth
	const glm::vec2 forward = worm.facing();
	const glm::vec2 side(-forward.y, forward.x);
	const glm::vec2 radii = Worm::size * glm::vec2(0.29f, 0.14f) + player.radius;
	const auto mouth_offset = [&](glm::vec2 offset) {
		return glm::vec2(glm::dot(offset, side), glm::dot(offset, forward)) / radii;
	};

	//check the closest approach during this step
	const glm::vec2 start = mouth_offset(previous_position - previous_head);
	const glm::vec2 step = mouth_offset(player.position - worm.head) - start;
	const float length_squared = glm::dot(step, step);
	const float time = length_squared > 0.0f ? glm::clamp(-glm::dot(start, step) / length_squared, 0.0f, 1.0f) : 0.0f;
	const glm::vec2 closest = start + time * step;
	if (glm::dot(closest, closest) <= 1.0f) {
		player.active = false;
		player.velocity = glm::vec2(0.0f);
		player.force = glm::vec2(0.0f);
	}
}

void Level2::update(float elapsed, UnderwaterPhysics &physics, Camera2D const &camera, size_t player_body,
	glm::vec2 const &player_movement, float player_angle) {
	clock += elapsed;
	shake_left = std::max(0.0f, shake_left - elapsed);
	for (Flashlight &flashlight : flashlights) {
		if (!flashlight.turned) {
			continue;
		}
		aim_flashlight(flashlight, physics);
		flashlight.turned = false;
	}

	Body2D const &player = physics.bodies[player_body];

	const glm::vec2 player_step = player.position - previous_player_position;
	previous_player_position = player.position;

	//stop single fish half a screen before they encountered the seven small fish group
	const float quiet_start = physics.bounds_min.x + mimic_end_fraction * (physics.bounds_max.x - physics.bounds_min.x)
		- mimic_quiet_screen_fraction * camera.view_size.x;
	//keep single fish disabled even if the player swims back
	if (player.position.x >= quiet_start) {
		mimic_encounters_finished = true;
	}

	if (mimic_encounters_finished) {
		//cancel fish that have not appeared, and let visible fish swim away
		if (small_fish.state == SmallScaryFish::State::Waiting) {
			small_fish.state = SmallScaryFish::State::Gone;
		} else if (small_fish.state == SmallScaryFish::State::Arriving || small_fish.state == SmallScaryFish::State::Mimicking) {
			small_fish.state = SmallScaryFish::State::Fleeing;
			//clear copied speed so the fish chooses an escape direction
			small_fish.velocity = glm::vec2(0.0f);
		}
	}

	update_fish_group(std::min(elapsed, 0.1f), physics, player_body, player_step);

	if (!player.active) {
		shake_left = 0.0f;
		death_time += elapsed;
		//an arriving or copying fish immediately flees when the player dies
		if (small_fish.state == SmallScaryFish::State::Arriving || small_fish.state == SmallScaryFish::State::Mimicking) {
			small_fish.state = SmallScaryFish::State::Fleeing;
			small_fish.velocity = glm::vec2(0.0f);
		}
		if (small_fish.state != SmallScaryFish::State::Fleeing) {
			//only an escaping fish keeps moving after player death
			return;
		}
	}

	//use the same limit as player physics
	update_small_fish(std::min(elapsed, 0.1f), physics, camera, player, player_step, player_movement, player_angle);
}

bool Level2::small_fish_path_clear(glm::vec2 const &from, glm::vec2 const &to,
	UnderwaterPhysics const &physics, float radius) const {
	//leave room for the fish body at both ends of the path
	const glm::vec2 low = physics.bounds_min + radius;
	const glm::vec2 high = physics.bounds_max - radius;
	for (unsigned axis = 0; axis < 2; ++axis) {
		if (from[axis] < low[axis] || from[axis] > high[axis] || to[axis] < low[axis] || to[axis] > high[axis]) {
			return false;
		}
	}

	const glm::vec2 delta = to - from;
	for (StaticBox2D const &box : physics.obstacles) {
		//enlarge walls by the fish radius before checking its path
		const glm::vec2 box_min = box.min - radius;
		const glm::vec2 box_max = box.max + radius;

		float enter = 0.0f;
		float leave = 1.0f;
		for (unsigned axis = 0; axis < 2; ++axis) {
			if (std::abs(delta[axis]) < 0.00001f) {
				//parallel movement outside this wall cannot hit it
				if (from[axis] < box_min[axis] || from[axis] > box_max[axis]) {
					enter = 2.0f; //past the path end at 1, so this wall is missed
					break;
				}
			} else {
				//find where the path enters and leaves the wall on each axis
				const float a = (box_min[axis] - from[axis]) / delta[axis];
				const float b = (box_max[axis] - from[axis]) / delta[axis];
				enter = std::max(enter, std::min(a, b));
				leave = std::min(leave, std::max(a, b));
			}
		}

		//an overlap means the path hits this wall
		if (enter <= leave) {
			return false;
		}
	}
	return true;
}

void Level2::react_to_suction(glm::vec2 const &mouth, glm::vec2 const &facing, float range, float cone_cos, float any_side) {
	//only arriving or copying fish need to react to suction
	if (small_fish.state != SmallScaryFish::State::Arriving && small_fish.state != SmallScaryFish::State::Mimicking) {
		return;
	}

	const glm::vec2 offset = small_fish.position - mouth;
	const float distance = glm::length(offset);
	//use the same suction range and direction checks as the player
	if (distance <= range && (glm::dot(offset, facing) >= distance * cone_cos || distance <= any_side)) {
		small_fish.state = SmallScaryFish::State::Fleeing;
		small_fish.velocity = glm::vec2(0.0f);
	}
}

/**
 * Small Fish update position
 */
void Level2::update_small_fish(float elapsed, UnderwaterPhysics const &physics, Camera2D const &camera,
	Body2D const &player, glm::vec2 const &player_step, glm::vec2 const &player_movement, float player_angle) {
	//avoid division by zero when calculating velocity
	if (elapsed <= 0.0f) {
		return;
	}

	if (small_fish.state == SmallScaryFish::State::Gone) {
		//wait for the next appearance time while the player is alive and single fish are allowed
		if (!player.active || mimic_encounters_finished || clock < next_mimic_time) {
			return;
		}
		small_fish.state = SmallScaryFish::State::Waiting;
	}

	const glm::vec2 size(small_fish_height * float(small_fish_texture.pixel_size.x) / float(small_fish_texture.pixel_size.y),
		small_fish_height);
	//half the diagonal covers the sprite at any angle
	const float sprite_radius = 0.5f * glm::length(size);

	//screen corners in world units, 0.5 converts full width and height to half sizes
	const glm::vec2 view_min = camera.center - 0.5f * camera.view_size;
	const glm::vec2 view_max = camera.center + 0.5f * camera.view_size;
	const glm::vec2 player_facing(std::cos(player_angle), std::sin(player_angle));

	//distance to swim offscreen in the given direction
	const auto distance_to_edge = [&](glm::vec2 const &position, glm::vec2 const &direction) {
		float distance = std::numeric_limits<float>::max();
		for (unsigned axis = 0; axis < 2; ++axis) {
			//skip direction values too small to divide by
			if (std::abs(direction[axis]) > 0.00001f) {
				const float edge = direction[axis] > 0.0f ? view_max[axis] : view_min[axis];
				//choose the first screen edge reached
				distance = std::min(distance, (edge - position[axis]) / direction[axis]);
			}
		}

		//add body size and extra room beyond the edge
		return std::max(distance, 0.0f) + sprite_radius + mimic_offscreen_margin;
	};

	if (small_fish.state == SmallScaryFish::State::Waiting) {
		if (clock < next_mimic_time) {
			return;
		}

		float screen_length = std::numeric_limits<float>::max();
		//use screen width for sideways swimming and height for vertical swimming
		for (unsigned axis = 0; axis < 2; ++axis) {
			if (std::abs(player_facing[axis]) > 0.00001f) {
				screen_length = std::min(screen_length, camera.view_size[axis] / std::abs(player_facing[axis]));
			}
		}

		const glm::vec2 offset = -player_facing * (mimic_screen_fraction * screen_length);
		const glm::vec2 target = player.position + offset;
		const glm::vec2 start = player.position - player_facing * distance_to_edge(player.position, -player_facing);
		//wait until the fish fits onscreen and its arrival path is clear
		if (target.x - sprite_radius < view_min.x || target.x + sprite_radius > view_max.x
			|| target.y - sprite_radius < view_min.y || target.y + sprite_radius > view_max.y
			|| !small_fish_path_clear(start, target, physics, small_fish.radius)) {
			return;
		}

		small_fish.state = SmallScaryFish::State::Arriving;
		small_fish.position = start;
		small_fish.arrival_offset = offset;
		small_fish.angle = player_angle;
		return;
	}

	//check catch attempts while arriving or copying
	if (small_fish.state == SmallScaryFish::State::Arriving || small_fish.state == SmallScaryFish::State::Mimicking) {
		//use the player's previous position because the fish has not moved yet
		const glm::vec2 to_fish = small_fish.position - (player.position - player_step);
		const float distance = glm::length(to_fish);

		//input and actual movement must both point toward the fish
		const bool approaching = glm::dot(player_movement, to_fish) > mimic_approach_cos * distance
			&& glm::dot(player_step, to_fish) > 0.0f;

		//flee if the player gets close or swims toward the fish
		if (distance < mimic_approach_distance || approaching) {
			small_fish.state = SmallScaryFish::State::Fleeing;
			small_fish.velocity = glm::vec2(0.0f);
			// TODO: add sound effects once we have the creepy sound
		} else {
			//copy actual player movement, including drift
			glm::vec2 next = small_fish.position + player_step;

			//swim quickly to the following position before copying
			if (small_fish.state == SmallScaryFish::State::Arriving) {
				const glm::vec2 delta = player.position + small_fish.arrival_offset - small_fish.position;
				const float remaining = glm::length(delta);
				const float travel = small_fish_fast_speed * elapsed;
				//stop at the target before switching to copying
				next = remaining <= travel ? player.position + small_fish.arrival_offset
					: small_fish.position + delta * (travel / remaining);
				if (remaining <= travel) {
					small_fish.state = SmallScaryFish::State::Mimicking;
				}
			}

			//move only if the path stays inside the cave and avoids walls
			if (small_fish_path_clear(small_fish.position, next, physics, small_fish.radius)) {
				small_fish.velocity = (next - small_fish.position) / elapsed;
				small_fish.position = next;
				small_fish.angle = player_angle;
				return;
			}

			//if copying is blocked, find an escape from the current position
			small_fish.state = SmallScaryFish::State::Fleeing;
			small_fish.velocity = glm::vec2(0.0f);
		}
	}

	if (small_fish.state == SmallScaryFish::State::Fleeing) {
		//hide the fish after its whole body leaves the screen
		if (small_fish.position.x + sprite_radius < view_min.x || small_fish.position.x - sprite_radius > view_max.x
			|| small_fish.position.y + sprite_radius < view_min.y || small_fish.position.y - sprite_radius > view_max.y) {
			small_fish.state = SmallScaryFish::State::Gone;
			small_fish.velocity = glm::vec2(0.0f);
			if (player.active && !mimic_encounters_finished) {
				//start the next wait only after this fish leaves the screen
				next_mimic_time = clock + mimic_random_between(mimic_repeat_delay.x, mimic_repeat_delay.y);
			}
			return;
		}

		//check an exit direction and set escape velocity if the path is clear
		const auto try_direction = [&](glm::vec2 const &direction) {
			const glm::vec2 exit = small_fish.position + direction * distance_to_edge(small_fish.position, direction);
			if (!small_fish_path_clear(small_fish.position, exit, physics, small_fish.radius)) {
				return false;
			}

			small_fish.velocity = direction * small_fish_fast_speed;
			return true;
		};

		//keep the current escape direction if clear, otherwise find another
		if (glm::length(small_fish.velocity) < 0.00001f || !try_direction(glm::normalize(small_fish.velocity))) {
			const glm::vec2 away = small_fish.position - player.position;
			const float escape_angle = glm::length(away) > 0.00001f ? std::atan2(away.y, away.x) : player_angle + glm::radians(180.0f);
			small_fish.velocity = glm::vec2(0.0f);

			//try away from the player first, then turn in 45-degree steps
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

void Level2::update_fish_group(float elapsed, UnderwaterPhysics &physics, size_t player_body,
	glm::vec2 const &player_step) {
	Body2D &player = physics.bodies[player_body];

	//stop the group when the player dies
	if (!player.active) {
		for (SmallScaryFish &fish : fish_group) {
			fish.velocity = glm::vec2(0.0f);
		}
		return;
	}

	//approaching any fish activates the entire group
	if (!fish_group_triggered) {
		for (SmallScaryFish const &fish : fish_group) {
			if (glm::distance(player.position, fish.position) <= fish_group_trigger_distance) {
				fish_group_triggered = true;
				fish_group_turn_remaining = fish_group_turn_time;
				mimic_encounters_finished = true;
				break;
			}
		}

		if (!fish_group_triggered) {
			return;
		}

		for (SmallScaryFish &fish : fish_group) {
			fish.state = SmallScaryFish::State::Turning;
		}
	}

	//turn before moving or changing appearance
	if (fish_group_turn_remaining > 0.0f) {
		const float turn_step = std::min(elapsed, fish_group_turn_remaining);
		const float turn_fraction = turn_step / fish_group_turn_remaining;
		const float full_turn = glm::radians(360.0f);

		//take the shorter turn toward the player
		for (SmallScaryFish &fish : fish_group) {
			const glm::vec2 delta = player.position - fish.position;
			const float target = glm::length(delta) > 0.00001f ? std::atan2(delta.y, delta.x) : fish.angle;
			fish.angle = std::remainder(fish.angle + std::remainder(target - fish.angle, full_turn) * turn_fraction, full_turn);
		}

		//use remaining frame time for chasing
		fish_group_turn_remaining = std::max(0.0f, fish_group_turn_remaining - turn_step);
		elapsed -= turn_step;

		if (fish_group_turn_remaining > 0.0f) {
			return;
		}

		for (SmallScaryFish &fish : fish_group) {
			fish.state = SmallScaryFish::State::Chasing;
		}
	}

	//use the same starting positions for all fish
	const std::array<SmallScaryFish, fish_group_count> previous_fish = fish_group;
	const glm::vec2 previous_player = player.position - player_step;

	for (size_t i = 0; i < fish_group.size(); ++i) {
		SmallScaryFish &fish = fish_group[i];

		//aim toward the player
		const glm::vec2 to_player = player.position - previous_fish[i].position;
		glm::vec2 direction = glm::length(to_player) > 0.00001f ? glm::normalize(to_player) : glm::vec2(0.0f);

		//steer away from nearby fish
		for (size_t j = 0; j < previous_fish.size(); ++j) {
			if (j == i) {
				continue;
			}

			const glm::vec2 away = previous_fish[i].position - previous_fish[j].position;
			const float distance = glm::length(away);

			//larger fish need more space
			const float separation_distance = fish_group_separation_distance
				* (previous_fish[i].scale + previous_fish[j].scale) * 0.5f;
			if (distance > 0.00001f && distance < separation_distance) {
				direction += away / distance * (1.0f - distance / separation_distance) * fish_group_separation_weight;
			}
		}

		//keep speed fixed while changing direction
		fish.velocity = glm::length(direction) > 0.00001f ? glm::normalize(direction) * fish_group_speed : glm::vec2(0.0f);

		//keep the whole body inside the cave
		const glm::vec2 next = glm::clamp(fish.position + fish.velocity * elapsed,
			physics.bounds_min + fish.radius, physics.bounds_max - fish.radius);

		//slide along a wall when direct movement fails
		if (small_fish_path_clear(fish.position, next, physics, fish.radius)) {
			fish.position = next;
		} else if (small_fish_path_clear(fish.position, glm::vec2(next.x, fish.position.y), physics, fish.radius)) {
			fish.position.x = next.x;
		} else if (small_fish_path_clear(fish.position, glm::vec2(fish.position.x, next.y), physics, fish.radius)) {
			fish.position.y = next.y;
		}

		//face the direction actually traveled
		fish.velocity = elapsed > 0.0f ? (fish.position - previous_fish[i].position) / elapsed : glm::vec2(0.0f);
		if (glm::length(fish.velocity) > 0.00001f) {
			fish.angle = std::atan2(fish.velocity.y, fish.velocity.x);
		}

		//find closest distance during both player and fish movement
		const glm::vec2 relative_start = previous_fish[i].position - previous_player;
		const glm::vec2 relative_step = fish.position - player.position - relative_start;
		const float length_squared = glm::dot(relative_step, relative_step);
		const float contact_time = length_squared > 0.00000001f
			? glm::clamp(-glm::dot(relative_start, relative_step) / length_squared, 0.0f, 1.0f) : 0.0f;
		const glm::vec2 closest = relative_start + contact_time * relative_step;
		const float contact_radius = fish.radius + player.radius;

		//any fish touching the player causes death
		if (glm::dot(closest, closest) <= contact_radius * contact_radius) {
			player.active = false;
			player.velocity = glm::vec2(0.0f);
			player.force = glm::vec2(0.0f);

			//stop the remaining fish immediately
			for (SmallScaryFish &member : fish_group) {
				member.velocity = glm::vec2(0.0f);
			}
			return;
		}
	}
}

bool Level2::restart_requested() const {
	//restart after the death fade and a short wait
	return death_time > restart_delay;
}

glm::vec3 Level2::prepare_water(Water::Look &look, UnderwaterPhysics const &physics, size_t player_body,
	std::optional<size_t> carried_body) const {
	//clear last frame's lights before adding the current ones
	look.glows.clear();
	for (Coral const &coral : corals) {
		const glm::vec2 outward(-std::sin(coral.angle), std::cos(coral.angle));
		look.glows.emplace_back(coral.position + outward * coral_light_offset, coral_glow);
	}
	//share sprite and haze lighting with jellyfish and coral
	look.beams.clear();
	for (Flashlight const &flashlight : flashlights) {
		look.beams.push_back(flashlight.light);
	}
	look.blockers.clear();
	look.bubble_columns.clear();
	look.glow_radius = glm::vec2(5.5f, 4.0f); //light fade distances in world units, x then y
	look.brightness = scene_brightness;
	look.time = clock;

	if (!jelly_body) {
		return glm::vec3(0.0f);
	}

	Body2D const &jelly = physics.bodies[*jelly_body];
	const bool in_belly = carried_body && *carried_body == *jelly_body;
	//a swallowed jelly still lights the player's position
	if (!in_belly && !jelly.active) {
		return glm::vec3(0.0f);
	}

	const glm::vec2 position = in_belly ? physics.bodies[player_body].position : jelly_position(jelly);
	//pulse brightness at the same rate as the drawn jelly
	const float strength = jelly_glow + jelly_glow_pulse * std::sin(clock * 1.3f);
	const glm::vec3 source(position, strength); //x and y are position, z is brightness
	look.glows.push_back(source);
	return source;
}

glm::vec2 Level2::jelly_position(Body2D const &jelly) const {
	//draw up to 0.18 sideways and 0.25 vertically from the fixed physics position
	return jelly.position + glm::vec2(0.18f * std::sin(clock * 0.3f), 0.25f * std::sin(clock * 0.65f));
}

void Level2::Worm::chase(glm::vec2 player, glm::vec2 entrance, float elapsed) {
	if (!awake || stopped) {
		// at entrance it stopped for good
		return;
	}

	if (player.y <= entrance.y) {
		returning_to_entrance = true;
	}

	//follow the player through each turn
	const glm::vec2 target = returning_to_entrance ? entrance : player;
	if (tracks.empty() || glm::distance(tracks.back(), target) >= 0.25f) {
		tracks.push_back(target);
	}

	float travel = chase_speed * elapsed;
	while (travel > 0.0f && !tracks.empty()) {
		const glm::vec2 delta = tracks.front() - head;
		const float distance = glm::length(delta);
		if (distance > travel) {
			head += delta * (travel / distance);
			break;
		}
		head = tracks.front();
		tracks.pop_front();
		travel -= distance;
	}

	if (glm::distance(head, trail.back()) >= 0.35f) {
		trail.push_back(head);
	}

	// stop at the entrance
	stopped = returning_to_entrance && glm::distance(head, entrance) < 0.25f;
}

glm::vec2 Level2::Worm::facing() const {
	// change the worm facing
	glm::vec2 delta = head - trail.back();
	if (glm::length(delta) < 0.001f) {
		delta = trail.back() - trail[trail.size() - 2];
	}
	return glm::normalize(delta);
}

void Level2::Worm::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	if (!awake) {
		return;
	}

	const float row = 0.5f / float(texture.pixel_size.y);

	//overlap body segments to make it infinite long
	for (size_t i = 0; i < trail.size(); ++i) {
		const glm::vec2 start = trail[i];
		const glm::vec2 end = i == trail.size() - 1 ? head : trail[i + 1];
		const glm::vec2 delta = end - start;
		const float length = glm::length(delta);
		if (length < 0.001f) {
			continue;
		}

		const float angle = std::atan2(-delta.x, delta.y);
		sprites.draw(texture, world_to_clip, (start + end) * 0.5f,
			glm::vec2(size, length + 0.45f), angle, false, glm::u8vec4(255), 0.0f,
			DrawSprites::LightOnly, glm::vec4(0.0f, row, 1.0f, row));
	}

	const glm::vec2 forward = facing();
	sprites.draw(texture, world_to_clip, head, glm::vec2(size), std::atan2(-forward.x, forward.y),
		false, glm::u8vec4(255), 0.0f, DrawSprites::LightOnly);
}

void Level2::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics) const {
	/**
	 * Draw some assets:
	 * (1) Jellybody
	 * (2) Tunnel related: flashlight, corals, worms, keys
	 * (3) Small scary fish
	 * (4) Doors and pipes
	 */

	if (jelly_body && physics.bodies[*jelly_body].active) {
		const float pulse = 1.0f + 0.035f * std::sin(clock * 1.3f);
		sprites.draw(*jellyfish_texture, world_to_clip, jelly_position(physics.bodies[*jelly_body]), glm::vec2(1.8f * pulse, 2.15f / pulse),
			0.04f * std::sin(clock * 0.4f), false, glm::u8vec4(175, 185, 190, 165), 0.0f, DrawSprites::Emissive);
	}

	/**
	 * Small scary fish
	 */
	//keep drawing a fleeing fish even after the player dies
	if (small_fish.state != SmallScaryFish::State::Waiting && small_fish.state != SmallScaryFish::State::Gone) {
		draw_fish(sprites, world_to_clip, small_fish, small_fish_texture);
	}

	//use eyes png nce chasing starts
	for (SmallScaryFish const &fish : fish_group) {
		draw_fish(sprites, world_to_clip, fish,
			fish.state == SmallScaryFish::State::Chasing ? fish_eyes_texture : small_fish_texture);
	}

	/**
	 * Tunnel Related
	 */
	for (Flashlight const &flashlight : flashlights) {
		const glm::vec2 size(flashlight.height * float(flashlight_texture.pixel_size.x) / float(flashlight_texture.pixel_size.y),
			flashlight.height);
		sprites.draw(flashlight_texture, world_to_clip, flashlight.center, size, glm::radians(flashlight.tilt));
	}
	for (Coral const &coral : corals) {
		sprites.draw(coral_texture, world_to_clip, coral.position, glm::vec2(coral_size), coral.angle,
			false, glm::u8vec4(90, 90, 90, 255), 0.0f, DrawSprites::Emissive);
	}
	worm.draw(sprites, world_to_clip);
	//rock hides the buried bases
	sprites.draw(tunnel_texture, world_to_clip, tunnel_center, tunnel_size, 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Tunnel);
	sprites.draw(flashlight_base_texture, world_to_clip, glm::vec2(flashlights[0].anchor.x, physics.bounds_min.y),
		flashlight_base_size, 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	if (key_body && physics.bodies[*key_body].active) {
		const glm::vec2 key_size(key_height * float(key_texture.pixel_size.x) / float(key_texture.pixel_size.y), key_height);
		sprites.draw(key_texture, world_to_clip, physics.bodies[*key_body].position, key_size, 0.0f, false,
			glm::u8vec4(255), 0.0f, DrawSprites::LightOnly);
	}

	/**
	 * Door related
	 */
	const glm::vec2 door_size(door_height * float(door_texture.pixel_size.x) / float(door_texture.pixel_size.y), door_height);
	if (door_open) {
		//leave the opening dark and show the panel edge at the right hinge
		sprites.draw(door_texture, world_to_clip, door_center, door_size, 0.0f, false, glm::u8vec4(0, 0, 0, 255));
		const glm::vec2 edge_size(0.16f * door_size.x, door_size.y);
		const glm::vec2 hinge = door_center + glm::vec2(0.34f * door_size.x, 0.0f);
		sprites.draw(door_texture, world_to_clip, hinge, edge_size);

		// pops out the pipe once opened the door
		const Body2D &pipe = physics.bodies[pipe_body];
		if (pipe.active) {
			sprites.draw(pipe_texture, world_to_clip, pipe.position, glm::vec2(pipe_sprite_size), glm::radians(45.0f));
		}
		return;
	}
	sprites.draw(door_texture, world_to_clip, door_center, door_size, 0.0f);
	sprites.draw(door_wheel_texture, world_to_clip, door_center, door_size, wheel_angle);
}

void Level2::draw_fish(DrawSprites const &sprites, glm::mat4 const &world_to_clip,
	SmallScaryFish const &fish, SpriteTexture const &texture) const {
	//apply each fish's saved size
	const float height = small_fish_height * fish.scale;
	const glm::vec2 size(height * float(texture.pixel_size.x) / float(texture.pixel_size.y), height);

	//flip leftward fish to keep them upright
	const bool flip_x = std::abs(fish.angle) > glm::radians(90.0f);
	const float angle = flip_x ? fish.angle - std::copysign(glm::radians(180.0f), fish.angle) : fish.angle;

	sprites.draw(texture, world_to_clip, fish.position, size, angle, flip_x,
		glm::u8vec4(255), 0.0f, DrawSprites::Player);
}

glm::vec2 Level2::arrival_shake() const {
	//used for worm shaking now
	const float rise = (shake_duration - shake_left) / 0.25f;
	const float fade = shake_left / 0.6f;
	const float strength = shake_strength * std::min({1.0f, rise, fade});
	return strength * glm::vec2(std::sin(clock * 67.0f), std::cos(clock * 53.0f));
}

float Level2::fade_amount() const {
	//use the stronger fade, 0 is visible and 1 is black
	return std::max(std::max(0.0f, 1.0f - clock / fade_in_time), std::min(1.0f, death_time / death_fade_time));
}
