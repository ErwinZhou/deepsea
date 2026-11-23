#include "level2.hpp"

#include "data_path.hpp"
#include "load_save_png.hpp"
#include "Load.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <random>

#include <stdexcept>
#include <vector>

static Load<Sound::Sample> ambient_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("ambient_deep.opus"));
});

static Load<Sound::Sample> seal_hit_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_rock_hit.opus"));
});

//based on https://en.cppreference.com/w/cpp/numeric/random/uniform_real_distribution,
//used for:
//(1) small scary fish random appear seconds interval generation
//(2) small scary fish group sizes random generation
//(3) small scary fish group initial positioning
//(4) decorations random size and random distances in between
static std::mt19937 mimic_rng{Level2::random_seed};

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
	{944.0f / 1180.0f, 46.0f / 820.0f, -22.0f}
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
	: mound_texture(data_path("level2/rock_mound.png")),
	column_textures{SpriteTexture(data_path("level1/wall1.png")), SpriteTexture(data_path("level1/wall2.png")),
		SpriteTexture(data_path("level1/wall3.png"))},
	tunnel_texture(data_path("level2/tunnel.png")), key_texture(data_path("level2/key.png")),
	coral_texture(data_path("coral.png")),
	flashlight_texture(data_path("level2/flashlight.png")), flashlight_base_texture(data_path("small_rock.png")),
	worm{SpriteTexture(data_path("level2/worm.png")),
		Worm::speed_fraction * max_swim_speed, {}, {}, {}},
	small_fish_texture(normal_texture), small_fish_height(fish_height),
	small_fish_fast_speed(mimic_speed_multiplier * max_swim_speed),
	fish_eyes_texture(data_path("level2/small_fish_eyes.png")),
	fish_group_speed(fish_group_speed_multiplier * max_swim_speed),
	door_texture(data_path("level2/door.png")),
	floor_texture(glm::u8vec4(0, 0, 0, 255)),
	wheel_mark_texture(glm::u8vec4(255)),
	door_wheel_texture(data_path("level2/door_wheel.png")),
	big_rock_texture(data_path("level1/big_rock3.png")) {
	for (int stage = 0; stage < 3; ++stage) {
		column_targets[stage] = Level1::load_target(data_path("level1/wall" + std::to_string(stage + 1) + ".png"));
	}
	big_rock_target = Level1::load_target(data_path("level1/big_rock3.png"));
}

float Level2::camera_floor(float x) const {
	float across = 1.0f - glm::smoothstep(pit_half_width - 4.0f, pit_half_width + 6.0f,
		std::abs(x - pit_center.x));
	return seafloor_y - pit_depth * across;
}

float Level2::pit_reveal(glm::vec2 const &position) const {
	float across = 1.0f - glm::smoothstep(pit_half_width - 4.0f, pit_half_width + 6.0f,
		std::abs(position.x - pit_center.x));
	float below = 1.0f - glm::smoothstep(seafloor_y - 6.0f, seafloor_y + 6.0f, position.y);
	return across * below;
}

void Level2::setup_octopus_pit(UnderwaterPhysics &physics) {
	ground = {{
		{physics.bounds_min, glm::vec2(pit_center.x - pit_half_width, seafloor_y)},
		{glm::vec2(pit_center.x + pit_half_width, physics.bounds_min.y), glm::vec2(physics.bounds_max.x, seafloor_y)}
	}};
	for (StaticBox2D const &part : ground) physics.obstacles.push_back(part);
	mound_centers = {pit_center + glm::vec2(-22.0f, 3.0f - 0.5f * pit_depth),
		pit_center + glm::vec2(22.0f, 3.0f - 0.5f * pit_depth)};
	glm::uvec2 size;
	std::vector<glm::u8vec4> pixels;
	load_png(data_path("level2/rock_mound.png"), &size, &pixels, UpperLeftOrigin);
	const glm::vec2 pixel_size = mound_size / glm::vec2(size);
	const glm::vec2 clearance = glm::min(glm::vec2(0.04f), 0.25f * pixel_size);
	//match collisions to the banks in the image
	for (size_t side = 0; side < mound_centers.size(); ++side) {
		glm::vec2 origin = mound_centers[side] - 0.5f * mound_size;
		for (unsigned y = 0; y < size.y; y += 4) {
			unsigned sample_y = std::min(y + 2, size.y - 1);
			unsigned start = 0;
			bool in_run = false;
			for (unsigned x = 0; x <= size.x; ++x) {
				unsigned sample_x = side == 0 ? size.x - 1 - std::min(x, size.x - 1) : std::min(x, size.x - 1);
				bool solid = x < size.x && pixels[size_t(sample_y) * size.x + sample_x].a >= 128;
				if (solid && !in_run) { start = x; in_run = true; }
				if (!solid && in_run) {
					physics.obstacles.push_back({
						origin + pixel_size * glm::vec2(float(start), float(size.y - std::min(y + 4, size.y))) + clearance,
						origin + pixel_size * glm::vec2(float(x), float(size.y - y)) - clearance});
					in_run = false;
				}
			}
		}
	}
	ledge_center = pit_center + glm::vec2(-5.0f, -chest_depth - 4.3f);
	Level1::Target ledge = Level1::load_target(data_path("small_rock.png"));
	for (glm::vec4 const &part : ledge.parts) {
		physics.obstacles.push_back(Level1::world_box(part, ledge_center, ledge_size, 0.0f));
	}
}

void Level2::setup_pit_seal(UnderwaterPhysics &physics) {
	pit_seal = {pit_center + glm::vec2(0.0f, 5.0f), glm::vec2(3.0f / 0.56f, 30.0f), -glm::half_pi<float>()};
	pit_obstacle_start = physics.obstacles.size();
	update_pit_seal_obstacles(physics);
	//place one stone per hit above the painted crack
	glm::vec2 entry = column_targets[0].entry;
	glm::vec2 offset = pit_seal.size * glm::vec2(entry.x - 0.5f, 0.5f - entry.y);
	float co = std::cos(pit_seal.angle), si = std::sin(pit_seal.angle);
	glm::vec2 crack = pit_seal.center + glm::vec2(co * offset.x - si * offset.y, si * offset.x + co * offset.y);
	for (int i = 0; i < pit_seal.hits_left; ++i) {
		Body2D stone;
		stone.position = crack + glm::vec2(2.0f * (float(i) - 0.5f * float(pit_seal.hits_left - 1)), 3.0f);
		stone.radius = 0.45f;
		stone.current_scale = 0.0f;
		physics.bodies.push_back(stone);
	}
}

void Level2::update_pit_seal_obstacles(UnderwaterPhysics &physics) const {
	physics.obstacles.resize(pit_obstacle_start);
	Seal const &seal = pit_seal;
	if (seal.hits_left == 0) return;
	Level1::Target const &target = column_targets[3 - seal.hits_left];
	for (glm::vec4 const &part : target.parts) {
		physics.obstacles.push_back(Level1::world_box(part, seal.center, seal.size, seal.angle));
	}
}

void Level2::check_puzzle_hits(UnderwaterPhysics &physics) {
	for (Body2D &body : physics.bodies) {
		if (!body.active || body.kind != Body2D::Rock || !body.player_spat || glm::length(body.velocity) < 5.0f) continue;
		Seal &seal = pit_seal;
		if (seal.hits_left == 0) continue;
		Level1::Target const &target = column_targets[3 - seal.hits_left];
		float first = std::numeric_limits<float>::infinity();
		glm::vec2 contact(0.0f);
		for (glm::vec4 const &part : target.parts) {
			glm::vec2 at;
			float when = hit_time(body, Level1::world_box(part, seal.center, seal.size, seal.angle),
				float(UnderwaterPhysics::fixed_step), &at);
			if (when < first) { first = when; contact = at; }
		}
		if (!std::isfinite(first)) continue;
		body.player_spat = false;
		float co = std::cos(seal.angle), si = std::sin(seal.angle);
		glm::vec2 offset = contact - seal.center;
		glm::vec2 local(co * offset.x + si * offset.y, -si * offset.x + co * offset.y);
		float tolerance = 1.2f * (body.radius + 0.55f + 0.5f * glm::length(target.texel_size * seal.size));
		bool cracked = false;
		for (glm::vec2 const &pixel : target.cracks) {
			if (glm::distance(seal.size * glm::vec2(pixel.x - 0.5f, 0.5f - pixel.y), local) <= tolerance) {
				cracked = true;
				break;
			}
		}
		glm::vec2 away = -glm::normalize(body.velocity);
		int count = cracked ? 8 : 2;
		if (cracked) {
			body.active = false;
			--seal.hits_left;
			seal.shudder = 0.25f;
			if (seal.hits_left == 0) {
				count = 24;
				progress.puzzle_flags |= Checkpoint::pit_open;
			}
			update_pit_seal_obstacles(physics);
		}
		glm::vec2 side(-away.y, away.x);
		for (int c = 0; c < count; ++c) {
			chips.push_back({contact, away * mimic_random_between(1.0f, 4.0f) + side * mimic_random_between(-3.0f, 3.0f), 0.7f});
		}
		Sound::play_3D(*seal_hit_sound, 0.25f, glm::vec3(contact, 0.0f), 18.0f);
	}
	puzzle.check_puzzle_hits(physics, &progress);
}

void Level2::draw_pit_seal(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	Seal const &seal = pit_seal;
	if (seal.hits_left > 0) {
		SpriteTexture const &texture = column_textures[3 - seal.hits_left];
		glm::vec2 shake(0.15f * std::sin(seal.shudder * 90.0f) * (seal.shudder / 0.25f), 0.0f);
		sprites.draw(texture, world_to_clip, seal.center + shake, seal.size, seal.angle, false,
			glm::u8vec4(255), 0.0f, DrawSprites::Pillar);
	}
	for (Chip const &chip : chips) {
		sprites.draw(wheel_mark_texture, world_to_clip, chip.position, glm::vec2(0.12f), 0.0f, false,
			glm::u8vec4(50, 50, 50, uint8_t(255.0f * std::min(1.0f, chip.life / 0.2f))), 0.0f, DrawSprites::Stone);
	}
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
	//a little clearance keeps the tight bends from catching the fish
	const glm::vec2 clearance(0.08f);
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
					origin + pixel_size * glm::vec2(float(run_start), float(size.y - bottom)) + clearance,
					origin + pixel_size * glm::vec2(float(run_end), float(size.y - y)) - clearance});
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
	flashlights[0].anchor = glm::vec2(entrance_x + flashlight_entrance_offset, seafloor_y - flashlight_burial_depth);
	flashlights[1].anchor = tunnel_center + (glm::vec2(InsideFlashlightUV.x, 1.0f - InsideFlashlightUV.y) - 0.5f) * tunnel_size;
	flashlights[1].tilt = -flashlight_initial_tilt; //aim up and right for the inside tunnel
	flashlights[1].height = inside_flashlight_height;
	flashlights[1].range = inside_flashlight_range;

	//static base blocks swimming and can not be swallowed
	physics.obstacles.push_back(StaticBox2D{
		glm::vec2(flashlights[0].anchor.x - 0.5f * flashlight_base_collision_size.x, seafloor_y - flashlight_base_size.y),
		glm::vec2(flashlights[0].anchor.x + 0.5f * flashlight_base_collision_size.x, seafloor_y + flashlight_base_collision_size.y)});

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
	assert(physics.bodies.size() == 1 && player_body == 0 && physics.bodies[player_body].kind == Body2D::Fish
		&& "level 2 setup requires only the player at body zero");

	/**
	 * Belly logic of carrying the items to level2
	 */
	std::optional<size_t> carried_body;
	if (belly_contents && (belly_contents->kind == Body2D::Rock || belly_contents->kind == Body2D::Jelly)) {
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
	//extend the world downward for the octopus pit
	physics.bounds_min = glm::vec2(-0.5f * level_length, seafloor_y - pit_depth - 6.0f);
	physics.bounds_max = glm::vec2(0.5f * level_length, 0.5f * corridor_height);
	physics.obstacles.clear();
	setup_tunnel(physics);
	//keep the tunnel's full height, with a lower roof over the open water
	float tunnel_left = tunnel_center.x - 0.5f * tunnel_size.x;
	float tunnel_right = tunnel_center.x + 0.5f * tunnel_size.x;
	float roof = seafloor_y + low_ceiling_height;
	float door_room_left = physics.bounds_max.x - 40.0f;
	ceiling = {{
		{glm::vec2(physics.bounds_min.x, roof), glm::vec2(tunnel_left, physics.bounds_max.y)},
		{glm::vec2(tunnel_right, roof), glm::vec2(door_room_left, physics.bounds_max.y)},
		{glm::vec2(door_room_left, seafloor_y + 34.0f), physics.bounds_max}
	}};
	for (StaticBox2D const &part : ceiling) physics.obstacles.push_back(part);
	setup_flashlight(physics);
	Body2D key{};
	key.position = key_position;
	key.radius = key_radius;
	key.current_scale = 0.0f;
	key.kind = Body2D::Key;
	key_body = physics.bodies.size();
	physics.bodies.push_back(key);
	physics.bodies[player_body].position = glm::vec2(physics.bounds_min.x + entrance_distance, seafloor_y + 10.0f);

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
		roof - fish_group_depth_fraction * low_ceiling_height);

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
		fish.position.y = glm::clamp(fish.position.y, seafloor_y + fish.radius,
			roof - fish.radius - 0.2f);
	}

	fish_group_triggered = false;
	fish_group_turn_remaining = 0.0f;

	/**
	 * Clamp with Pearl Puzzle
	 */
	//leave room between clamp and flashlight
	constexpr float clam_offset_from_lamp = 20.0f;
	const float clam_x = flashlights[0].anchor.x + clam_offset_from_lamp;

	const float floor = seafloor_y;
	pit_center = glm::vec2(glm::mix(physics.bounds_min.x, physics.bounds_max.x, 1.0f / 6.0f), floor);
	finale_trigger_x = pit_center.x + finale_trigger_offset;
	finale_started = false;
	const size_t ground_start = physics.obstacles.size();
	setup_octopus_pit(physics);
	puzzle.setup(physics, glm::vec2(clam_x, floor + 0.5f * Level2Puzzle::clam_height),
		pit_center + glm::vec2(0.0f, -chest_depth),
		glm::vec2(glm::mix(physics.bounds_min.x, physics.bounds_max.x, 5.0f / 6.0f), floor + 0.5f * Level2Puzzle::scanner_height), *key_body, pit_depth, ground_start);

	//align the visible right and bottom edges with the level bounds
	door_center = glm::vec2(physics.bounds_max.x - door_height * (887.0f - 512.0f) / 2048.0f,
		seafloor_y + door_height * (2020.0f - 1024.0f) / 2048.0f);

	// reserve the tunnel and school before placing pipes
	puzzle.decorate(physics, std::mt19937(random_seed),
		{tunnel_center.x - 0.5f * tunnel_size.x, tunnel_center.x + 0.5f * tunnel_size.x}, group_center.x, door_center, ceiling);

	Body2D wheel{};
	wheel.kind = Body2D::Wheel;
	wheel.position = door_center;
	wheel.inverse_mass = 0.0f;
	wheel.radius = wheel_radius;
	wheel_body = physics.bodies.size();
	physics.bodies.push_back(wheel);
	wheel_angle = 0.0f;
	clockwise_turn = 0.0f;
	door_open = false;
	puzzle.hide_pipe(physics, door_center);
	setup_pit_seal(physics);

	// camera logic
	camera.view_size = glm::vec2(camera_height * (16.0f / 9.0f), camera_height); // reference view in world units 16:9
	//half etxtends a central box so the player does not move the camera
	camera.dead_zone = glm::vec2(5.0f, 2.5f);
	camera.follow_speed = 4.0f;
	camera.center = camera.constrain(physics.bodies[player_body].position, physics.bounds_min, physics.bounds_max);

	// reuse the puzzle item already in this level
	if (belly_contents && !carried_body) {
		for (size_t i = 0; i < physics.bodies.size(); ++i) {
			Body2D &body = physics.bodies[i];
			if (body.kind == belly_contents->kind) {
				body.inverse_mass = belly_contents->inverse_mass;
				body.radius = belly_contents->radius;
				body.drag = belly_contents->drag;
				body.current_scale = belly_contents->current_scale;
				body.active = false;
				carried_body = i;
				break;
			}
		}
	}

	ambient = Sound::loop(*ambient_sound, 0.0f);
	ambient->set_volume(1.0f, fade_in_time);
	return carried_body;
}

void Level2::update_worm(UnderwaterPhysics &physics, size_t player_body, glm::vec2 previous_position, float elapsed) {
	Body2D &player = physics.bodies[player_body];
	if (!player.active) {
		return;
	}

	//swallowing the key or pushing it away starts the chase once
	if (!worm.awake) {
		if (worm.stopped || !key_body) {
			return;
		}

		const Body2D &key = physics.bodies[*key_body];
		if (key.active && glm::distance(key.position, key_position) < key_wake_distance) {
			return;
		}

		worm.awake = true;
		shake_left = shake_duration;
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

void Level2::check_tunnel_exit(Body2D const &player, glm::vec2 previous_position, std::optional<size_t> carried_body) {
	/**
	 * Tunnel puzzle is tricky to leave checkpoints
	 * Because it is not that the player catches the key means if it finished
	 * It must swim out the tunnel alive with key in its mouth
	 */
	// the key must leave with a living fish
	if (!player.active || !key_body || carried_body != key_body || tunnel_finished()) {
		return;
	}

	// the tunnel mouth opens downward at row 541
	const glm::vec2 entrance = tunnel_center + (glm::vec2(TunnelEntranceUV.x, 1.0f - TunnelEntranceUV.y) - 0.5f) * tunnel_size;
	if (previous_position.y <= entrance.y || player.position.y > entrance.y) {
		return;
	}

	// the opening spans pixels 615 through 660
	const float fraction = (entrance.y - previous_position.y) / (player.position.y - previous_position.y);
	const float crossing_x = glm::mix(previous_position.x, player.position.x, fraction);
	const float pixel_width = tunnel_size.x / 1180.0f;
	if (crossing_x >= entrance.x - 22.0f * pixel_width && crossing_x < entrance.x + 24.0f * pixel_width) {
		progress.finish_puzzle(Level2Checkpoint::Tunnel);
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

	pit_seal.shudder = std::max(0.0f, pit_seal.shudder - elapsed);
	for (Chip &chip : chips) {
		chip.life -= elapsed;
		chip.velocity.y -= 5.0f * elapsed;
		chip.velocity *= std::exp(-2.0f * elapsed);
		chip.position += chip.velocity * elapsed;
	}
	chips.erase(std::remove_if(chips.begin(), chips.end(), [](Chip const &chip) { return chip.life <= 0.0f; }), chips.end());
	puzzle.update(elapsed, physics, player_body, camera, pit_seal.hits_left == 0);
	update_big_fish(elapsed, physics.bodies[player_body], camera);
	update_fish_group(std::min(elapsed, 0.1f), physics, player_body, player_step);

	if (!finale_started && big_fish.swimming() && player.active
		&& big_fish.mouth_position().x <= finale_trigger_x
		&& big_fish.mouth_position().x < camera.center.x + 0.5f * camera.view_size.x
		&& player.position.x < big_fish.mouth_position().x) {
		// Octopus grabs big fish jaws and holds them open
		finale_started = true;
		big_fish.hold_open();
		// the octopus has lost its pearl
		puzzle.lose_pearl();
		stop_chase_sounds();

		// root new tentacles in the floor (illusion that they're reaching from pit)
		// lower-jaw arms (0-3) grab lower jaw, upper-jaw arms (4-7) grab upper jaw
		const float teeth_x = big_fish.mouth_position().x;
		const float root_y = seafloor_y - finale_root_depth;
		std::array<glm::vec2, 8> roots;
		for (size_t i = 0; i < 4; ++i) {
			roots[i] = glm::vec2(teeth_x - 2.0f + 0.8f * float(i), root_y);
			roots[4 + i] = glm::vec2(teeth_x - 3.0f - 1.5f * float(i), root_y);
		}
		arms.shoot(roots);
	}

	if (finale_started) {
		arms.hold_big_fish(finale_arm_targets(), elapsed);
		update_rockfall(elapsed, physics, physics.bodies[player_body], camera);
		if (player.active && !escaped && big_fish.in_mouth(big_fish.prey - glm::vec2(20.0f, 0.0f))) {
			// go to level 3
			escaped = true;
			if (ambient) ambient->set_volume(0.0f, death_fade_time);
		}
	}

	if (!player.active) {
		shake_left = 0.0f;
		if (death_time == 0.0f && ambient) ambient->set_volume(0.0f, death_fade_time);
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

		if (fish.trapped_time > 0.0f) {
			fish.trapped_time = std::max(0.0f, fish.trapped_time - elapsed);
			if (fish.trapped_time == 0.0f) fish.leaving_arm = true;
			fish.velocity = glm::vec2(0.0f);
			continue;
		}
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

		bool touching_arm = puzzle.touches_arm(fish.position, fish.radius);
		if (!touching_arm) fish.leaving_arm = false;
		if (touching_arm && !fish.leaving_arm) {
			fish.trapped_time = 60.0f;
			fish.velocity = glm::vec2(0.0f);
			continue;
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
	for (StaticBox2D const &part : ceiling) look.blockers.emplace_back(part.min, part.max);
	look.bubble_columns.clear();
	look.glow_radius = glm::vec2(5.5f, 4.0f); //light falloff in world units
	look.brightness = scene_brightness;
	look.time = clock;

	puzzle.add_lights(look);
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
	//leave room for the carried light after keeping the nearest scenery lights
	if (look.glows.size() == Water::max_glows) look.glows.pop_back();
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

	// draw loose decorations behind all puzzle pieces
	puzzle.draw_decorations(sprites, world_to_clip);

	// draw the door behind fish and loose items
	const glm::vec2 door_size(door_height * float(door_texture.pixel_size.x) / float(door_texture.pixel_size.y), door_height);
	if (door_open) {
		//leave the opening dark and show the panel edge at the right hinge
		sprites.draw(door_texture, world_to_clip, door_center, door_size, 0.0f, false, glm::u8vec4(0, 0, 0, 255));
		const glm::vec2 edge_size(0.16f * door_size.x, door_size.y);
		const glm::vec2 hinge = door_center + glm::vec2(0.34f * door_size.x, 0.0f);
		sprites.draw(door_texture, world_to_clip, hinge, edge_size);

	} else {
		sprites.draw(door_texture, world_to_clip, door_center, door_size);
		sprites.draw(door_wheel_texture, world_to_clip, door_center, door_size, wheel_angle);
		//the arrow fills as the wheel opens
		float progress = glm::clamp(clockwise_turn / wheel_open_angle, 0.0f, 1.0f);
		float radius = wheel_radius * 1.03f;
		auto stroke = [&](glm::vec2 const &a, glm::vec2 const &b, uint8_t shade) {
			glm::vec2 delta = b - a;
			sprites.draw(wheel_mark_texture, world_to_clip, 0.5f * (a + b), glm::vec2(glm::length(delta), 0.08f),
				std::atan2(delta.y, delta.x), false, glm::u8vec4(shade, shade, shade, 255), 0.0f, DrawSprites::Stone);
		};
		constexpr int steps = 32;
		for (int i = 0; i < steps; ++i) {
			float angle = glm::pi<float>() * float(i) / float(steps);
			float next = glm::pi<float>() * float(i + 1) / float(steps);
			glm::vec2 a = door_center + radius * glm::vec2(-std::cos(angle), std::sin(angle));
			glm::vec2 b = door_center + radius * glm::vec2(-std::cos(next), std::sin(next));
			stroke(a, b, float(i + 1) / float(steps) <= progress ? 235 : 95);
		}
		glm::vec2 tip = door_center + glm::vec2(radius, -0.4f);
		stroke(tip + glm::vec2(-0.45f, 0.7f), tip, 180);
		stroke(tip + glm::vec2(0.45f, 0.7f), tip, 180);
	}

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
	sprites.draw(flashlight_base_texture, world_to_clip, glm::vec2(flashlights[0].anchor.x, seafloor_y),
		flashlight_base_size, 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	if (key_body && physics.bodies[*key_body].active) {
		const glm::vec2 key_size(key_height * float(key_texture.pixel_size.x) / float(key_texture.pixel_size.y), key_height);
		sprites.draw(key_texture, world_to_clip, physics.bodies[*key_body].position, key_size, 0.0f, false,
			glm::u8vec4(255), 0.0f, DrawSprites::LightOnly);
	}

	for (size_t side = 0; side < mound_centers.size(); ++side) {
		sprites.draw(mound_texture, world_to_clip, mound_centers[side], mound_size, 0.0f, side == 0,
			glm::u8vec4(255), 0.0f, DrawSprites::Pillar);
	}
	sprites.draw(flashlight_base_texture, world_to_clip, ledge_center, ledge_size, 0.0f, false,
		glm::u8vec4(0, 0, 0, 255), 0.0f, DrawSprites::Pillar);
	puzzle.draw(sprites, world_to_clip, physics);
	draw_pit_seal(sprites, world_to_clip);
	draw_rockfall(sprites, world_to_clip);
	for (StaticBox2D const &part : ceiling) {
		sprites.draw(floor_texture, world_to_clip, 0.5f * (part.min + part.max), part.max - part.min,
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Emissive);
	}

	// draw the approaching jaws over the scenery
	big_fish.draw(sprites, world_to_clip);

	if (finale_started) {
		arms.draw(world_to_clip, clock);
	}

	//show solid ground wherever the camera looks below the swimming area
	for (StaticBox2D const &part : ground) {
		sprites.draw(floor_texture, world_to_clip, 0.5f * (part.min + part.max), part.max - part.min,
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Emissive);
	}
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

Level2::~Level2() {
	if (ambient) {
		ambient->stop(0.0f);
	}
	stop_chase_sounds();
}

void Level2::stop_chase_sounds() {
	// clear the old chase before the next life
	for (std::shared_ptr<Sound::PlayingSample> *sound : {&arrival_roar, &presence, &charge_roar}) {
		if (*sound) {
			(*sound)->stop();
			sound->reset();
		}
	}
}

float Level2::ceiling_at(float x, UnderwaterPhysics const &physics) const {
	float roof = physics.bounds_max.y;
	for (StaticBox2D const &part : ceiling) {
		if (x >= part.min.x && x <= part.max.x) roof = std::min(roof, part.min.y);
	}
	return roof;
}

void Level2::update_rockfall(float elapsed, UnderwaterPhysics const &physics, Body2D &player, Camera2D const &camera) {
	// start the clock once every arm has hold of the jaws
	if (grab_time < 0.0f) {
		if (!arms.holding_big_fish()) return;
		grab_time = 0.0f;
		next_rock_time = rock_delay;
	}
	grab_time += elapsed;

	// only drop rocks left of the fish, between the edge of the view and its teeth
	const float view_left = camera.center.x - 0.5f * camera.view_size.x;
	const float teeth_x = big_fish.mouth_position().x;
	if (!escaped && player.active && grab_time >= next_rock_time) {
		const float ramp = std::clamp((grab_time - rock_delay) / rock_ramp_time, 0.0f, 1.0f);
		next_rock_time = grab_time + glm::mix(rock_interval.x, rock_interval.y, ramp);

		const float half = 0.5f * rock_size;
		const float left = view_left + half;
		const float right = teeth_x - half - 1.0f;
		if (right > left) {
			// anywhere left of the fish
			const float x = mimic_random_between(left, right);
			FallingRock rock;
			// start hidden above the ceiling
			rock.position = glm::vec2(x, ceiling_at(x, physics) + half);
			rock.angle = mimic_random_between(0.0f, 6.2831853f);
			falling_rocks.push_back(rock);
		}
	}

	for (FallingRock &rock : falling_rocks) {
		if (rock.rest_time >= 0.0f) {
			rock.rest_time += elapsed;
			continue;
		}
		// same fall as level 1's hanging rock, straight down
		rock.velocity += rock_gravity * elapsed;
		rock.position.y -= rock.velocity * elapsed;
		rock.angle += rock_spin * elapsed;

		// crush the player if they touch the rock's silhouette, using the boxes built from the png
		const glm::vec2 size(rock_size);
		for (glm::vec4 const &part : big_rock_target.parts) {
			if (!player.active) break;
			const StaticBox2D box = Level1::world_box(part, rock.position, size, rock.angle);
			const glm::vec2 offset = player.position - glm::clamp(player.position, box.min, box.max);
			if (glm::dot(offset, offset) < player.radius * player.radius) {
				player.active = false;
				player.velocity = glm::vec2(0.0f);
				player.force = glm::vec2(0.0f);
			}
		}

		// settles with its middle the same height above the floor as level 1's fallen rock
		const float rest_y = camera_floor(rock.position.x) + rock_rest_height * rock_size;
		if (rock.position.y <= rest_y) {
			rock.position.y = rest_y;
			rock.rest_time = 0.0f;
			shake_left = std::max(shake_left, 0.35f);
			for (int c = 0; c < 10; ++c) {
				chips.push_back({glm::vec2(rock.position.x, rest_y - rock_rest_height * rock_size),
					glm::vec2(mimic_random_between(-4.0f, 4.0f), mimic_random_between(1.0f, 4.0f)), 0.7f});
			}
			Sound::play_3D(*seal_hit_sound, 0.6f, glm::vec3(rock.position, 0.0f), 30.0f);
		}
	}
	falling_rocks.erase(std::remove_if(falling_rocks.begin(), falling_rocks.end(),
		[](FallingRock const &rock) { return rock.rest_time > rock_fade_time; }), falling_rocks.end());
}

void Level2::draw_rockfall(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	for (FallingRock const &rock : falling_rocks) {
		// landed rocks fade out over their last second, drawn like level 1's hanging rock
		const float fade = rock.rest_time < 0.0f ? 1.0f : std::clamp(rock_fade_time - rock.rest_time, 0.0f, 1.0f);
		sprites.draw(big_rock_texture, world_to_clip, rock.position, glm::vec2(rock_size), rock.angle, false,
			glm::u8vec4(255, 255, 255, uint8_t(255.0f * fade)), 0.0f, DrawSprites::Pillar);
	}
}

std::array<glm::vec2, 8> Level2::finale_arm_targets() const {
	constexpr std::array<float, 4> along{0.95f, 0.88f, 0.81f, 0.74f};
	std::array<glm::vec2, 8> targets;
	for (size_t i = 0; i < along.size(); ++i) {
		targets[i] = big_fish.tentacle_point(false, along[i]);
		targets[7 - i] = big_fish.tentacle_point(true, along[i]);
	}
	return targets;
}

void Level2::update_big_fish(float elapsed, Body2D &player, Camera2D const &camera) {
	if (!puzzle.machine_running() || (!player.active && big_fish.state == BigFish::Waiting)) {
		return;
	}

	// start the chase again after a machine checkpoint
	if (big_fish.state == BigFish::Waiting) {
		big_fish.enter_from_right(camera.center.x + 0.5f * camera.view_size.x, player.position.y);
		const Sound::Sample &sound = BigFish::arrival_sound();
		roar_length = float(sound.data.size()) / 48000.0f;
		arrival_roar = Sound::play_3D(sound, BigFish::arrival_volume,
			glm::vec3(big_fish.mouth_position(), 0.0f), BigFish::arrival_reach);
		presence = BigFish::play_presence(glm::vec3(big_fish.mouth_position(), 0.0f));
	}

	// chase and bite with the level one jaws
	big_fish.prey = player.position;
	big_fish.update(elapsed);

	// start the roar when the jaws open
	const glm::vec3 mouth(big_fish.mouth_position(), 0.0f);
	if (player.active && big_fish.in_reach(player.position)) {
		big_fish.charge();
		charge_roar = BigFish::play_charge_roar(mouth);
	}

	// cut the roar when the jaws snap shut
	if (big_fish.slammed && charge_roar) {
		charge_roar->stop();
		charge_roar.reset();
	}
	if (player.active && big_fish.slammed && big_fish.in_mouth(player.position)) {
		player.active = false;
		player.velocity = glm::vec2(0.0f);
		player.force = glm::vec2(0.0f);
	}

	// quiet the chase as soon as the player dies
	if (!player.active) {
		stop_chase_sounds();
		return;
	}

	// let the roar follow the approaching mouth
	if (arrival_roar && big_fish.time <= roar_length) {
		arrival_roar->set_position(mouth, elapsed);
	}
	if (presence) {
		presence->set_position(mouth, elapsed);
	}
	if (charge_roar) {
		charge_roar->set_position(mouth, elapsed);
	}
}

glm::vec2 Level2::arrival_shake() const {
	//used for worm shaking now
	const float rise = (shake_duration - shake_left) / 0.25f;
	const float fade = shake_left / 0.6f;
	const float strength = shake_strength * std::min({1.0f, rise, fade});

	// keep the camera shaking throughout the chase
	const float chase = big_fish.state == BigFish::Waiting ? 0.0f : std::min(big_fish.time / 0.25f, 1.0f);
	return (strength + BigFish::arrival_shake_size * chase) * glm::vec2(std::sin(clock * 67.0f), std::cos(clock * 53.0f))
		- big_fish.shake();
}

float Level2::fade_amount() const {
	//use the stronger fade, 0 is visible and 1 is black
	float entrance = std::max(0.0f, 1.0f - clock / fade_in_time);
	return std::max(entrance, puzzle.fade_amount());
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
	float turn = wheel.spin * elapsed;
	glm::vec2 contact_normal;
	if (player.active && distance > 0.0f && wheel.wheel_clearance(player.position, contact_normal) <= player.radius + 0.001f) {
		const glm::vec2 tangent(-offset.y / distance, offset.x / distance);
		turn += glm::dot(movement, tangent) * wheel_turn_speed * elapsed;
	}

	// wheel again changes
	wheel_angle += turn;
	wheel.wheel_angle = wheel_angle;
	wheel.spin *= std::exp(-3.0f * elapsed);

	// a wrong turn starts the count over
	if (turn > 0.0f) {
		clockwise_turn = 0.0f;
	} else {
		clockwise_turn -= turn;
	}

	// wait for the full clockwise turn
	if (clockwise_turn < wheel_open_angle) {
		return;
	}

	// enough angle, then open the door and active and launch the pipe
	door_open = true;
	wheel.active = false;
	puzzle.launch_pipe(physics);
	progress.finish_puzzle(Level2Checkpoint::Door);
}

bool Level2::inside_tunnel(glm::vec2 position) const {
	const glm::vec2 distance = glm::abs(position - tunnel_center);
	return distance.x <= tunnel_size.x * 0.5f && distance.y <= tunnel_size.y * 0.5f;
}

void Level2::place_jelly_nearby(UnderwaterPhysics &physics, size_t player_body) {
	// bring back the jellyfish from level one
	Body2D jelly{.radius = 0.5f, .drag = 3.0f, .current_scale = 0.0f, .kind = Body2D::Jelly};
	const Body2D &fish = physics.bodies[player_body];
	jelly.position = fish.position + glm::vec2(0.0f, fish.radius + jelly.radius + 1.0f);

	jelly_body = physics.bodies.size();
	physics.bodies.push_back(jelly);
	jellyfish_texture.emplace(data_path("jellyfish.png"));
}

void Level2::resume_checkpoint(UnderwaterPhysics &physics, size_t player_body, Checkpoint const &entry,
	std::optional<size_t> carried_body) {
	using Point = Level2Checkpoint;

	// keep the last stop and solved puzzles
	progress.checkpoint = entry.checkpoint;
	progress.puzzle_flags = entry.puzzle_flags;

	// reopen the pit before placing the fish
	if ((progress.puzzle_flags & Checkpoint::pit_open) != 0) {
		pit_seal.hits_left = 0;
		update_pit_seal_obstacles(physics);
	}
	Body2D &player = physics.bodies[player_body];

	// leave room below the narrow tunnel mouth
	const glm::vec2 mouth = tunnel_center + (glm::vec2(TunnelEntranceUV.x, 1.0f - TunnelEntranceUV.y) - 0.5f) * tunnel_size;
	const glm::vec2 tunnel_spawn = mouth - glm::vec2(0.0f, 6.0f);
	if (tunnel_finished()) {
		worm.stopped = true;
	}

	// leave the wheel off the open door
	door_open = (entry.puzzle_flags & puzzle_flag(Point::Door)) != 0;
	if (door_open) {
		physics.bodies[wheel_body].active = false;
	}

	// only the latest puzzle places the player
	if (entry.checkpoint == Point::Tunnel) {
		player.position = tunnel_spawn;
	} else if (entry.checkpoint == Point::Door) {
		player.position = door_center - glm::vec2(wheel_radius + 2.0f, 0.0f);
	}

	// restore the shell, arms, chest and machine
	puzzle.restore(physics, player, entry, carried_body);
	previous_player_position = player.position;
}
