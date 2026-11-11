#include "level1.hpp"

#include "data_path.hpp"
#include "load_save_png.hpp"
#include "DrawLines.hpp"
#include "Load.hpp"

#include <cassert>

#include <algorithm>
#include <cmath>
#include <random>
#include <limits>
#include <string>
#include <cstddef>

// ================== Sound Sample Loading ==================
static Load<Sound::Sample> fish_roar(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_big_fish_roar.opus"));
});

static Load<Sound::Sample> fish_roar_near(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_big_fish_roar1.opus"));
});

static Load<Sound::Sample> charge_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_big_fish_roar_charge.opus"));
});

static Load<Sound::Sample> chomp_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_chomp.opus"));
});

static Load<Sound::Sample> rumble_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_rumble.opus"));
});

static Load<Sound::Sample> rock_hit_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_rock_hit.opus"));
});

static Load<Sound::Sample> ambient_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("ambient_deep.opus"));
});

static Load<Sound::Sample> presence_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("big_fish_presence.opus"));
});

static std::mt19937 rng{std::random_device{}()};
//based on https://en.cppreference.com/w/cpp/numeric/random/uniform_real_distribution
static float random_between(float low, float high) {
	return std::uniform_real_distribution< float >(low, high)(rng);
}

// ================== Game Constants ==================
//how thick the walls are, and where the wall art sits across its png, as fractions of the png width
static constexpr float WallThickness = 3.0f;
static constexpr float WallArtLeft = 0.28f;
static constexpr float WallArtRight = 0.84f;
static constexpr int WallHits = 3;
static constexpr float CrackLeeway = 0.35f;
static constexpr float HangingCrackLeeway = 0.55f;
static constexpr glm::vec2 WallCrackHalfSize = glm::vec2(0.15f, 0.5f);
static constexpr float WallCrackHitboxScale = 1.4f;
static constexpr float HangingCrackHitboxScale = 1.2f;
static constexpr float ExitShift = 2.0f;
//the hanging rock's png is drawn this many units square, with its top against the ceiling
static constexpr float HangingSize = 9.0f;
static constexpr float CoralSize = 3.5f;
static constexpr int HangingHits = 3;
//leave room to collect stones between the pillar and final wall
static constexpr float PillarWallGap = 40.0f;
//how it falls once it breaks loose. it drifts left because the crack is on its right
static constexpr float FallGravity = 25.0f;
static constexpr float FallDrift = 2.5f;
static constexpr float FallSpin = 0.6f;
//how long the big fish is held up when the rock lands on it, or when it has to get past the pile
static constexpr float PinTime = 9.0f;
static constexpr float ChewTime = 3.0f;
//how far in front of its teeth the rock can land and still catch the big fish
static constexpr float PinReach = 2.0f;
static constexpr float PinHalfWidth = 0.35f;

//require a fast impact to damage a wall or pillar
static constexpr float RockHitSpeed = 5.0f;
//shake damaged rock and knock off chips
static constexpr float ShudderTime = 0.25f;
static constexpr float ShudderSize = 0.15f;
static constexpr int HitChips = 8;
//a hit off the crack only knocks off a couple of chips
static constexpr int MissChips = 2;
static constexpr int BreakChips = 24;
static constexpr float HitCooldown = 0.3f;
static constexpr float LooseRockRadius = 0.4f;
//the final wall needs extra stones dropped by hitting the pillar
static glm::vec2 const FinalRockOffsets[] = {{6.0f, -2.0f}, {8.0f, 2.0f}};
//drop stones beside the pillar, then let them settle in the water
static Body2D const RockfallStones[] = {
	{.position = {6.0f, 0.0f}, .velocity = {-0.4f, -3.0f}, .radius = LooseRockRadius, .current_scale = 0.0f},
	{.position = {8.0f, 0.0f}, .velocity = {0.6f, -4.0f}, .radius = LooseRockRadius, .current_scale = 0.0f}
};
//chips show where the stones fell from
static constexpr int RockfallChips = 4;
//keep rockfall shake weaker than the big fish's arrival
static constexpr float RockfallShakeSize = 0.06f;
//particles slow down in the water at this rate per second
static constexpr float WaterDrag = 2.0f;
//height of the fallen rock's middle above the floor, as a fraction of HangingSize
static constexpr float RestHeight = 0.32f;

//build collision boxes and crack hit locations from the wall or pillar image
static Level1::Target load_target(std::string const &filename) {
	glm::uvec2 size;
	std::vector< glm::u8vec4 > pixels;
	load_png(filename, &size, &pixels, UpperLeftOrigin);
	Level1::Target target;
	target.texel_size = 1.0f / glm::vec2(size);
	constexpr unsigned strips = 16;
	for (unsigned strip = 0; strip < strips; ++strip) {
		glm::vec4 part(1.0f, 0.0f, 1.0f, 0.0f);
		for (unsigned y = strip * size.y / strips; y < (strip + 1) * size.y / strips; ++y) {
			for (unsigned x = 0; x < size.x; ++x) {
				glm::u8vec4 const &pixel = pixels[size_t(y) * size.x + x];
				if (pixel.a < 128) continue;
				part.x = std::min(part.x, float(x) / float(size.x));
				part.y = std::max(part.y, float(x + 1) / float(size.x));
				part.z = std::min(part.z, float(y) / float(size.y));
				part.w = std::max(part.w, float(y + 1) / float(size.y));
				//the crack paint is gray against the black rock
				if (std::max(pixel.r, std::max(pixel.g, pixel.b)) >= 96) {
					target.cracks.emplace_back((float(x) + 0.5f) / float(size.x), (float(y) + 0.5f) / float(size.y));
					if (target.cracks.back().x < target.entry.x) target.entry = target.cracks.back();
				}
			}
		}
		if (part.x < part.y) target.parts.push_back(part);
	}
	return target;
}

Level1::Level1() : wall_textures{SpriteTexture(data_path("level1/wall1.png")), SpriteTexture(data_path("level1/wall2.png")), SpriteTexture(data_path("level1/wall3.png"))},
	hanging_textures{SpriteTexture(data_path("level1/big_rock1.png")), SpriteTexture(data_path("level1/big_rock2.png")), SpriteTexture(data_path("level1/big_rock3.png"))},
	exit_texture(data_path("level1/exit.png")),
	coral_texture(data_path("coral.png")),
	chip_texture(data_path("small_rock.png")),
	fade_texture(data_path("cavern_background.png")),
	jellyfish_texture(data_path("jellyfish.png")) {
	for (int stage = 0; stage < 3; ++stage) {
		wall_targets[stage] = load_target(data_path("level1/wall" + std::to_string(stage + 1) + ".png"));
		hanging_targets[stage] = load_target(data_path("level1/big_rock" + std::to_string(stage + 1) + ".png"));
	}
}

void Level1::setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body) {
	fish_body = player_body;
	assert(physics.bodies.size() == 1 && fish_body == 0 && physics.bodies[fish_body].kind == Body2D::Fish
		&& "level 1 setup requires only the player at body zero");
	float far_roar_length = float(fish_roar->data.size()) / 48000.0f;
	float passing_time = (far_start - far_end) / far_speed;
	warning_length = far_begin + std::max(far_roar_length, passing_time) + near_roar_gap;
	far_roar_end = far_begin + far_roar_length;
	near_roar_end = warning_length + float(fish_roar_near->data.size()) / 48000.0f;
	camera.view_size = glm::vec2(camera_height * (16.0f / 9.0f), camera_height);
	camera.dead_zone = glm::vec2(5.0f, 2.5f);
	camera.follow_speed = 4.0f;
	physics.bounds_min = glm::vec2(-0.5f * level_length, -0.5f * corridor_height);
	physics.bounds_max = glm::vec2(0.5f * level_length, 0.5f * corridor_height);
	camera.center = camera.constrain(glm::vec2(physics.bounds_min.x, 0.0f), physics.bounds_min, physics.bounds_max);
	physics.bodies.push_back({.radius = 0.35f});
	physics.bodies.push_back({.inverse_mass = 0.625f, .radius = 0.43f});
	bounds_min = physics.bounds_min;
	bounds_max = physics.bounds_max;

	auto place_wall = [&](Wall &wall, float x, int hits) {
		wall.x = x;
		wall.hits_left = hits;
		wall.box = StaticBox2D{glm::vec2(x - 0.5f * WallThickness, bounds_min.y), glm::vec2(x + 0.5f * WallThickness, bounds_max.y)};
	};
	//positions are measured from the left end of the level
	place_wall(first_wall, bounds_min.x + 32.0f, WallHits);
	place_wall(last_wall, bounds_max.x - 12.0f, WallHits);
	exit_x = bounds_max.x - 5.0f + ExitShift;

	hanging_rock = HangingRock();
	hanging_rock.hits_left = HangingHits;
	hanging_rock.center = glm::vec2(last_wall.x - PillarWallGap, bounds_max.y - 0.5f * HangingSize);

	float middle_y = 0.5f * (bounds_min.y + bounds_max.y);
	light_spots = {
		glm::vec3(exit_x, middle_y, 2.4f),
		glm::vec3(0.0f),
	};
	update_coral();

	for (glm::vec2 const &offset : FinalRockOffsets) {
		physics.bodies.push_back({.position = glm::vec2(hanging_rock.center.x + offset.x, offset.y), .radius = LooseRockRadius});
	}
	//place stones relative to the wall so changing the camera cannot move them into it
	physics.bodies[1].position = glm::vec2(first_wall.x - 6.0f, 6.5f);
	physics.bodies[2].position = glm::vec2(first_wall.x - 11.0f, -4.0f);
	jelly_body = physics.bodies.size();
	glm::vec2 jelly_start = wall_crack_position(first_wall) - glm::vec2(4.0f, 0.0f);
	physics.bodies.push_back({.position = jelly_start, .radius = jelly_radius, .drag = jelly_drag, .current_scale = 0.0f, .kind = Body2D::Jelly});
	//start beside the jellyfish with the crack in view
	physics.bodies[fish_body].position = jelly_start - glm::vec2(2.4f, -0.2f);
	const glm::vec2 extra_rock_offset(-4.0f, 2.0f);
	physics.bodies.push_back({.position = physics.bodies[fish_body].position + extra_rock_offset, .radius = 0.35f});
	camera.center = camera.constrain(physics.bodies[fish_body].position, physics.bounds_min, physics.bounds_max);
	big_fish.reset_for_world(physics.bounds_min, physics.bounds_max);
	//starts silent and fades in with the picture
	ambient = Sound::loop(*ambient_sound, 0.0f);
	ambient->set_volume(1.0f, fade_in_time);
}

//find when a moving rock hits the collision box, or infinity if it misses
static float hit_time(Body2D const &rock, StaticBox2D const &box, float elapsed, glm::vec2 *contact) {
	float miss = std::numeric_limits<float>::infinity();
	glm::vec2 away = rock.position - glm::clamp(rock.position, box.min, box.max);
	if (glm::dot(rock.velocity, away) >= 0.0f) return miss;
	float first = 0.0f;
	float last = elapsed;
	for (int axis = 0; axis < 2; ++axis) {
		float low = box.min[axis] - rock.radius;
		float high = box.max[axis] + rock.radius;
		if (std::abs(rock.velocity[axis]) < 0.00001f) {
			if (rock.position[axis] < low || rock.position[axis] > high) return miss;
		} else {
			float a = (low - rock.position[axis]) / rock.velocity[axis];
			float b = (high - rock.position[axis]) / rock.velocity[axis];
			first = std::max(first, std::min(a, b));
			last = std::min(last, std::max(a, b));
		}
	}
	if (first > last) return miss;
	glm::vec2 center = rock.position + rock.velocity * first;
	*contact = glm::clamp(center, box.min, box.max);
	if (glm::dot(center - *contact, center - *contact) > rock.radius * rock.radius + 0.00001f) {
		//at a corner the circle reaches the box after its bounding square does
		glm::vec2 offset = rock.position - *contact;
		float a = glm::dot(rock.velocity, rock.velocity);
		float b = glm::dot(offset, rock.velocity);
		float c = glm::dot(offset, offset) - rock.radius * rock.radius;
		float discriminant = b * b - a * c;
		if (discriminant < 0.0f) return miss;
		first = (-b - std::sqrt(discriminant)) / a;
		if (first < 0.0f || first > last) return miss;
		*contact = glm::clamp(rock.position + rock.velocity * first, box.min, box.max);
	}
	return first;
}

static glm::vec2 world_point(glm::vec2 const &point, glm::vec2 const &center, glm::vec2 const &size, float angle) {
	glm::vec2 offset = size * glm::vec2(point.x - 0.5f, 0.5f - point.y);
	float co = std::cos(angle);
	float si = std::sin(angle);
	return center + glm::vec2(co * offset.x - si * offset.y, si * offset.x + co * offset.y);
}

static StaticBox2D world_box(glm::vec4 const &part, glm::vec2 const &center, glm::vec2 const &size, float angle) {
	glm::vec2 middle = world_point(glm::vec2(0.5f * (part.x + part.y), 0.5f * (part.z + part.w)), center, size, angle);
	glm::vec2 half = 0.5f * glm::abs(size) * glm::vec2(part.y - part.x, part.w - part.z);
	float co = std::abs(std::cos(angle));
	float si = std::abs(std::sin(angle));
	glm::vec2 extent(co * half.x + si * half.y, si * half.x + co * half.y);
	return StaticBox2D{middle - extent, middle + extent};
}

glm::vec2 Level1::wall_crack_position(Wall const &wall) const {
	float png_width = WallThickness / (WallArtRight - WallArtLeft);
	float art_middle = 0.5f * (WallArtLeft + WallArtRight);
	glm::vec2 center(wall.x - (art_middle - 0.5f) * png_width, 0.5f * (bounds_min.y + bounds_max.y));
	float height = bounds_max.y - bounds_min.y;
	return world_point(wall_targets[std::min(2, wall_stage(wall))].entry, center, glm::vec2(png_width, &wall == &first_wall ? -height : height), 0.0f);
}

int Level1::wall_stage(Wall const &wall) const {
	return WallHits - wall.hits_left;
}

void Level1::update_obstacles(UnderwaterPhysics &physics, float elapsed) {
	events = Events();
	float height = bounds_max.y - bounds_min.y;
	float png_width = WallThickness / (WallArtRight - WallArtLeft);
	float art_middle = 0.5f * (WallArtLeft + WallArtRight);
	auto wall_center = [&](Wall const &wall) {
		return glm::vec2(wall.x - (art_middle - 0.5f) * png_width, 0.5f * (bounds_min.y + bounds_max.y));
	};
	int hanging_stage = std::min(2, HangingHits - hanging_rock.hits_left);
	for (Wall *wall : {&first_wall, &last_wall}) {
		wall->shudder = std::max(0.0f, wall->shudder - elapsed);
	}
	hanging_rock.shudder = std::max(0.0f, hanging_rock.shudder - elapsed);
	for (Particle &particle : particles) {
		particle.velocity.y -= particle.gravity * elapsed;
		//exponential drag, the same way UnderwaterPhysics::step slows bodies
		particle.velocity *= std::exp(-WaterDrag * elapsed);
		particle.position += particle.velocity * elapsed;
		particle.angle += particle.spin * elapsed;
		particle.life -= elapsed;
	}
	//based on https://en.cppreference.com/w/cpp/algorithm/remove
	particles.erase(std::remove_if(particles.begin(), particles.end(), [](Particle const &particle) { return particle.life <= 0.0f; }), particles.end());

	hit_cooldown.resize(physics.bodies.size(), 0.0f);
	for (float &cooldown : hit_cooldown) cooldown = std::max(0.0f, cooldown - elapsed);

	//new drops wait until the next update so their cooldowns exist before hit checks
	for (size_t i = 0; i < hit_cooldown.size(); ++i) {
		//copy the body because a pillar hit appends stones and can reallocate the list
		Body2D body = physics.bodies[i];
		if (!body.active || body.kind != Body2D::Rock || !body.player_spat || hit_cooldown[i] > 0.0f || glm::length(body.velocity) < RockHitSpeed) continue;

		bool hit = false;
		bool missed = false;
		//chips fly back the way the rock came from
		glm::vec2 away = -glm::normalize(body.velocity);
		//damage the target when the impact is close enough to its painted crack
		auto strikes = [&](Target const &target, glm::vec2 const &center, glm::vec2 const &size, float angle, bool entry_only) {
			float first = std::numeric_limits<float>::infinity();
			glm::vec2 contact(0.0f);
			for (glm::vec4 const &part : target.parts) {
				glm::vec2 at;
				float when = hit_time(body, world_box(part, center, size, angle), std::min(elapsed, 0.1f), &at);
				if (when < first) { first = when; contact = at; }
			}
			if (!std::isfinite(first)) return false;
			physics.bodies[i].player_spat = false;
			events.at = contact;
			float reach = body.radius + (entry_only ? CrackLeeway : HangingCrackLeeway) + 0.5f * glm::length(target.texel_size * size);
			glm::vec2 offset = contact - center;
			float co = std::cos(angle);
			float si = std::sin(angle);
			glm::vec2 local_contact(co * offset.x + si * offset.y, -si * offset.x + co * offset.y);
			bool cracked = false;
			if (entry_only) {
				//aim at the point where the crack reaches the wall's left edge
				glm::vec2 delta = glm::abs(size * glm::vec2(target.entry.x - 0.5f, 0.5f - target.entry.y) - local_contact);
				glm::vec2 const tolerance = WallCrackHitboxScale * (WallCrackHalfSize + glm::vec2(reach));
				cracked = delta.x <= tolerance.x && delta.y <= tolerance.y;
			} else {
				const float tolerance = HangingCrackHitboxScale * reach;
				for (glm::vec2 const &pixel : target.cracks) {
					glm::vec2 delta = size * glm::vec2(pixel.x - 0.5f, 0.5f - pixel.y) - local_contact;
					if (glm::dot(delta, delta) <= tolerance * tolerance) { cracked = true; break; }
				}
			}
			if (!cracked) {
				throw_chips(contact, away, MissChips);
				missed = true;
				return false;
			}
			throw_chips(contact, away, HitChips);
			physics.bodies[i].active = false;
			return true;
		};
		for (Wall *wall : {&first_wall, &last_wall}) {
			if (hit || missed) break;
			if (wall->hits_left <= 0 || !strikes(wall_targets[wall_stage(*wall)], wall_center(*wall), glm::vec2(png_width, wall == &first_wall ? -height : height), 0.0f, true)) continue;
			hit = true;
			wall->hits_left -= 1;
			wall->shudder = ShudderTime;
			if (wall->hits_left == 0) {
				events.broke = true;
				for (int c = 0; c < BreakChips; ++c) {
					throw_chips(glm::vec2(wall->x, random_between(wall->box.min.y, wall->box.max.y)), away, 1);
				}
			}
			if (wall->hits_left == 0 && wall == &first_wall) {
				events.first_wall_broke = true;
			}
		}
		hanging_stage = std::min(2, HangingHits - hanging_rock.hits_left);
		if (hanging_rock.state == HangingRock::Hanging && !hit && !missed
			&& strikes(hanging_targets[hanging_stage], hanging_rock.center, glm::vec2(HangingSize), 0.0f, false)) {
			hit = true;
			hanging_rock.hits_left -= 1;
			hanging_rock.shudder = ShudderTime;
			for (Body2D stone : RockfallStones) {
				stone.position += glm::vec2(hanging_rock.center.x, bounds_max.y - stone.radius);
				physics.bodies.push_back(stone);
				throw_chips(stone.position, glm::vec2(0.0f, -1.0f), RockfallChips);
			}
			if (hanging_rock.hits_left == 0) {
				hanging_rock.state = HangingRock::Falling;
				events.broke = true;
			}
		}
		if (hit) events.hit = true;
		if (missed) events.missed = true;
		if (hit || missed) hit_cooldown[i] = HitCooldown;
	}

	float fallen_y = bounds_min.y + RestHeight * HangingSize;
	if (hanging_rock.state == HangingRock::Falling) {
		hanging_rock.velocity.x = -FallDrift;
		hanging_rock.velocity.y -= FallGravity * elapsed;
		hanging_rock.center += hanging_rock.velocity * elapsed;
		hanging_rock.angle += FallSpin * elapsed;
		if (hanging_rock.center.y <= fallen_y) {
			hanging_rock.center.y = fallen_y;
			hanging_rock.state = HangingRock::Fallen;
			events.crashed = true;
			events.at = hanging_rock.center;
			for (int c = 0; c < BreakChips; ++c) {
				throw_chips(hanging_rock.center + glm::vec2(random_between(-3.0f, 3.0f), -2.0f), glm::vec2(0.0f, 1.0f), 1);
			}
			//landing on the big fish's head pins it for a long time
			float rock_left = hanging_rock.center.x - PinHalfWidth * HangingSize;
			if (big_fish.swimming() && rock_left < big_fish.teeth_x() + PinReach && hanging_rock.center.x > big_fish.position.x) {
				big_fish.stun(PinTime);
				hanging_rock.used = true;
			}
		}
	}
	//fallen rubble slows the big fish once
	if (hanging_rock.state == HangingRock::Fallen && !hanging_rock.used && big_fish.swimming()
		&& big_fish.teeth_x() >= hanging_rock.center.x - PinHalfWidth * HangingSize) {
		big_fish.stun(ChewTime);
		hanging_rock.used = true;
	}

	physics.obstacles.clear();
	light_blockers.clear();
	auto add_target = [&](Target const &target, glm::vec2 const &center, glm::vec2 const &size, float angle, size_t shadow_parts) {
		for (size_t group = 0; group < shadow_parts; ++group) {
			StaticBox2D shadow{glm::vec2(std::numeric_limits<float>::infinity()), glm::vec2(-std::numeric_limits<float>::infinity())};
			for (size_t i = group * target.parts.size() / shadow_parts; i < (group + 1) * target.parts.size() / shadow_parts; ++i) {
				StaticBox2D box = world_box(target.parts[i], center, size, angle);
				physics.obstacles.push_back(box);
				shadow.min = glm::min(shadow.min, box.min);
				shadow.max = glm::max(shadow.max, box.max);
			}
			if (shadow.min.x <= shadow.max.x) light_blockers.push_back(shadow);
		}
	};
	for (Wall const *wall : {&first_wall, &last_wall}) {
		if (wall->hits_left > 0) add_target(wall_targets[wall_stage(*wall)], wall_center(*wall), glm::vec2(png_width, wall == &first_wall ? -height : height), 0.0f, 1);
	}
	hanging_stage = std::min(2, HangingHits - hanging_rock.hits_left);
	add_target(hanging_targets[hanging_stage], hanging_rock.center, glm::vec2(HangingSize), hanging_rock.angle, 3);
	update_coral();
}

void Level1::throw_chips(glm::vec2 const &at, glm::vec2 const &away, int count) {
	for (int c = 0; c < count; ++c) {
		float turn = random_between(-0.9f, 0.9f);
		//based on https://en.wikipedia.org/wiki/Rotation_matrix
		glm::vec2 direction = glm::vec2(std::cos(turn) * away.x - std::sin(turn) * away.y, std::sin(turn) * away.x + std::cos(turn) * away.y);
		Particle particle;
		particle.position = at;
		particle.velocity = direction * random_between(3.0f, 8.0f);
		particle.size = random_between(0.08f, 0.25f);
		particle.spin = random_between(-8.0f, 8.0f);
		particle.max_life = particle.life = random_between(0.6f, 1.1f);
		particle.gravity = 6.0f;
		particles.push_back(particle);
	}
}

void Level1::drop_ceiling_dust(float left, float right, float amount, float elapsed) {
	//grains per second at full amount
	constexpr float dust_per_second = 40.0f;
	float expected = amount * dust_per_second * elapsed;
	int count = int(expected);
	if (random_between(0.0f, 1.0f) < expected - float(count)) count += 1;
	for (int c = 0; c < count; ++c) {
		Particle particle;
		particle.position = glm::vec2(random_between(left, right), bounds_max.y);
		particle.velocity = glm::vec2(random_between(-0.3f, 0.3f), -random_between(0.5f, 1.5f));
		particle.size = random_between(0.05f, 0.15f);
		particle.spin = random_between(-2.0f, 2.0f);
		particle.max_life = particle.life = 4.0f;
		particle.gravity = 0.3f;
		particles.push_back(particle);
	}
}

//fade out the sideways shake after an impact
static float shudder_offset(float shudder) {
	return ShudderSize * (shudder / ShudderTime) * std::sin(shudder * 90.0f);
}

void Level1::update_coral() {
	float co = std::cos(hanging_rock.angle);
	float si = std::sin(hanging_rock.angle);
	constexpr glm::vec2 mount(-3.6f, -0.8f);
	coral_position = hanging_rock.center + glm::vec2(co * mount.x - si * mount.y, si * mount.x + co * mount.y);
	if (hanging_rock.state == HangingRock::Hanging) coral_position.x += shudder_offset(hanging_rock.shudder);
	light_spots[1] = glm::vec3(coral_position + 0.6f * glm::vec2(co, si), 1.0f);
}

glm::vec2 Level1::shake() const {
	const float amount = RockfallShakeSize * hanging_rock.shudder / ShudderTime;
	return amount * glm::vec2(std::sin(hanging_rock.shudder * 67.0f), std::cos(hanging_rock.shudder * 53.0f));
}

void Level1::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics) const {
	if (physics.bodies[jelly_body].active) {
		const float pulse = 1.0f + 0.035f * std::sin(clock * 1.3f);
		sprites.draw(jellyfish_texture, world_to_clip, jelly_position(physics), glm::vec2(1.8f * pulse, 2.15f / pulse),
			0.04f * std::sin(clock * 0.4f), false, glm::u8vec4(175, 185, 190, uint8_t(165.0f * jelly_light)), 0.0f, DrawSprites::Emissive);
	}
	float height = bounds_max.y - bounds_min.y;
	float middle_y = 0.5f * (bounds_min.y + bounds_max.y);

	{ //the way out, past the last wall
		constexpr float exit_width = 6.0f;
		sprites.draw(exit_texture, world_to_clip, glm::vec2(bounds_max.x - 0.5f * exit_width + ExitShift, middle_y), glm::vec2(exit_width, height));
	}

	//the wall png is wider than its art, so it is shifted to put the art on the wall's box
	float png_width = WallThickness / (WallArtRight - WallArtLeft);
	float art_middle = 0.5f * (WallArtLeft + WallArtRight);
	for (Wall const *wall : {&first_wall, &last_wall}) {
		if (wall->hits_left <= 0) continue;
		glm::vec2 center = glm::vec2(wall->x - (art_middle - 0.5f) * png_width + shudder_offset(wall->shudder), middle_y);
		SpriteTexture const &texture = wall_textures[wall_stage(*wall)];
		//flip the first wall and its collision shapes together
		bool flip_y = wall == &first_wall;
		float top_v = flip_y ? 0.001f : 0.999f;
		float bottom_v = flip_y ? 0.999f : 0.001f;
		//extend only the silhouette, without stretching the painted cracks
		sprites.draw(texture, world_to_clip, glm::vec2(center.x, bounds_max.y + 16.0f),
			glm::vec2(png_width, 32.0f), 0.0f, false, glm::u8vec4(0, 0, 0, 255), 0.0f, DrawSprites::Surface,
			glm::vec4(0.0f, top_v, 1.0f, top_v));
		sprites.draw(texture, world_to_clip, glm::vec2(center.x, bounds_min.y - 16.0f),
			glm::vec2(png_width, 32.0f), 0.0f, false, glm::u8vec4(0, 0, 0, 255), 0.0f, DrawSprites::Surface,
			glm::vec4(0.0f, bottom_v, 1.0f, bottom_v));
		sprites.draw(texture, world_to_clip, center, glm::vec2(png_width, flip_y ? -height : height), 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Cracked);
	}

	for (Particle const &particle : particles) {
		//fade out over the last part of their life
		float alpha = std::min(1.0f, 2.0f * particle.life / particle.max_life);
		sprites.draw(chip_texture, world_to_clip, particle.position, glm::vec2(particle.size), particle.angle, false,
			glm::u8vec4(0xc0, 0xc0, 0xc0, uint8_t(255.0f * alpha)));
	}
}

void Level1::draw_foreground(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	big_fish.draw(sprites, world_to_clip);
	//the pillar draws in front of the jaws
	if (hanging_rock.state == HangingRock::Hanging) {
		glm::vec2 center = hanging_rock.center + glm::vec2(shudder_offset(hanging_rock.shudder), 0.0f);
		SpriteTexture const &texture = hanging_textures[HangingHits - hanging_rock.hits_left];
		sprites.draw(texture, world_to_clip, glm::vec2(center.x, center.y + 0.5f * HangingSize + 16.0f),
			glm::vec2(HangingSize, 32.0f), 0.0f, false, glm::u8vec4(0, 0, 0, 255), 0.0f, DrawSprites::Surface,
			glm::vec4(0.0f, 0.999f, 1.0f, 0.999f));
		sprites.draw(texture, world_to_clip, center, glm::vec2(HangingSize), 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Pillar);
	} else {
		sprites.draw(hanging_textures[2], world_to_clip, hanging_rock.center, glm::vec2(HangingSize), hanging_rock.angle, false, glm::u8vec4(255), 0.0f, DrawSprites::Pillar);
	}
	sprites.draw(coral_texture, world_to_clip, coral_position, glm::vec2(CoralSize), hanging_rock.angle + 0.5f * float(M_PI), false,
		glm::u8vec4(90, 90, 90, 255), 0.0f, DrawSprites::Emissive);
}

Level1::~Level1() {
	//stop this run's sounds before restarting
	if (ambient) ambient->stop();
	if (presence) presence->stop();
	stop_roars(0.0f);
}

void Level1::stop_roars(float fade) {
	for (std::shared_ptr<Sound::PlayingSample> *roar : {&far_roar, &near_roar, &charge_roar}) {
		if (*roar) (*roar)->stop(fade);
		*roar = nullptr;
	}
}

float Level1::before_physics(UnderwaterPhysics &physics, float elapsed) {
	clock += elapsed;
	float hit_stop = 0.0f;
	rumble = std::max(0.0f, rumble - elapsed);
	update_obstacles(physics, elapsed);
	{ //freeze briefly when a spat rock lands, longer when something breaks
		constexpr float hit_pause_time = 0.06f;
		constexpr float break_pause_time = 0.12f;
		constexpr float crash_rumble = 0.6f;
		if (events.hit) hit_stop = hit_pause_time;
		if (events.broke) hit_stop = break_pause_time;
		if (events.crashed) rumble = crash_rumble;
	}
	{ //every impact is heard from where it happened
		glm::vec3 at = glm::vec3(events.at, 0.0f);
		if (events.missed) Sound::play_3D(*rock_hit_sound, rock_miss_volume, at, rock_reach);
		if (events.hit) Sound::play_3D(*rock_hit_sound, 1.0f, at, rock_reach);
		if (events.broke || events.crashed) Sound::play_3D(*rumble_sound, 1.0f, at, rumble_reach);
		if (events.crashed) Sound::play_3D(*rock_hit_sound, 1.0f, at, rumble_reach);
	}

	return hit_stop;
}

void Level1::after_physics(UnderwaterPhysics &physics, Camera2D const &camera, std::optional<size_t> carried_body, float elapsed) {
	const float view_left = camera.center.x - 0.5f * camera.view_size.x;
	big_fish.prey = physics.bodies[fish_body].position;
	big_fish.update(elapsed);
	{ //the big fish wakes once the first room is behind you
		if (warning_time < 0.0f && view_left > first_wall.x + dark_past_wall) {
			warning_time = 0.0f;
			if (ambient) ambient->set_volume(ambient_quiet_volume, ambient_fade_down);
		}
		float before = warning_time;
		if (warning_time >= 0.0f) warning_time += elapsed;
		if (big_fish.state == BigFish::Waiting && warning_time >= 0.0f) {
			if (before < far_begin && warning_time >= far_begin) {
				glm::vec3 at = glm::vec3(big_fish.far_mouth(far_hinge(camera.center)), -far_roar_depth);
				far_roar = Sound::play_3D(*fish_roar, far_roar_volume, at, far_roar_reach);
				Sound::play_3D(*rumble_sound, far_roar_volume, at, far_roar_reach);
			}
			if (far_roar) far_roar->set_position(glm::vec3(big_fish.far_mouth(far_hinge(camera.center)), -far_roar_depth), elapsed);
			drop_ceiling_dust(view_left, view_left + camera.view_size.x, roar_amount(physics), elapsed);
			if (warning_time >= warning_length) {
				big_fish.reset_for_world(physics.bounds_min, physics.bounds_max);
				big_fish.enter(big_fish.out_of_sight_x(view_left));
				glm::vec3 at = glm::vec3(big_fish.mouth_position(), 0.0f);
				near_roar = Sound::play_3D(*fish_roar_near, near_roar_volume, at, near_roar_reach);
				presence = Sound::loop_3D(*presence_sound, 1.0f, at, presence_reach);
			}
		}
		glm::vec3 at = glm::vec3(big_fish.mouth_position(), 0.0f);
		if (presence) presence->set_position(at, elapsed);
		if (near_roar) near_roar->set_position(at, elapsed);
		if (charge_roar) charge_roar->set_position(at, elapsed);
	}
	{ //the deep water comes back only while the big fish is held up
		bool held_up = big_fish.state == BigFish::Stunned;
		if (ambient && held_up != was_held_up) ambient->set_volume(held_up ? 1.0f : ambient_quiet_volume, ambient_fade_up);
		was_held_up = held_up;
	}
	//a jellyfish left behind goes dark once the big fish reaches it
	if (!jelly_in_belly(carried_body) && physics.bodies[jelly_body].active && big_fish.state != BigFish::Waiting && big_fish.teeth_x() > jelly_position(physics).x) {
		jelly_light = std::max(0.0f, jelly_light - elapsed / jelly_fade_time);
		if (jelly_light <= 0.0f) physics.bodies[jelly_body].active = false;
	}
	float target_brightness = 1.0f;
	if (big_fish.state != BigFish::Waiting) target_brightness = 0.65f;
	else if (warning_time >= 0.0f) target_brightness = 1.0f - 0.45f * warning_amount();
	scene_brightness += (target_brightness - scene_brightness) * (1.0f - std::exp(-elapsed / 1.2f));

	{ //charge when the small fish gets too close, kill it when the jaws slam
		Body2D &fish = physics.bodies[fish_body];
		glm::vec3 at = glm::vec3(big_fish.mouth_position(), 0.0f);
		if (fish.active && !escaped && big_fish.in_reach(fish.position)) {
			big_fish.charge();
			//starts quiet and gets louder
			charge_roar = Sound::play_3D(*charge_sound, charge_roar_start_volume, at, near_roar_reach);
			charge_roar->set_volume(near_roar_volume, charge_roar_fade_in);
		}
		//pinned mid charge
		if (charge_roar && big_fish.state == BigFish::Stunned) {
			charge_roar->stop(charge_roar_fade_out);
			charge_roar = nullptr;
		}
		if (big_fish.slammed) {
			//no bite is heard once you're out
			if (!escaped) Sound::play_3D(*chomp_sound, 1.0f, at, roar_reach);
			if (charge_roar) charge_roar->stop();
			charge_roar = nullptr;
			if (fish.active && !escaped && big_fish.in_mouth(fish.position)) {
				//being caught starts the death fade
				fish.active = false;
				if (ambient) ambient->set_volume(0.0f, death_fade_time);
				if (presence) presence->stop(death_fade_time);
				stop_roars(death_fade_time);
			}
		}
	}

	{ //fade between tutorial hints
		char const *wanted = wanted_hint(physics, carried_body);
		float step = elapsed / hint_fade_time;
		hint_amount = wanted == shown_hint ? std::min(1.0f, hint_amount + step) : std::max(0.0f, hint_amount - step);
		if (hint_amount <= 0.0f) shown_hint = wanted;
	}

	if (physics.bodies[fish_body].active && physics.bodies[fish_body].position.x > exit_x) escaped = true;
	if (!physics.bodies[fish_body].active || escaped) end_time += elapsed;
}

bool Level1::restart_requested(UnderwaterPhysics const &physics) const {
	constexpr float restart_delay = 3.0f;
	return !physics.bodies[fish_body].active && end_time > restart_delay;
}

bool Level1::jelly_in_belly(std::optional<size_t> carried_body) const {
	return carried_body && *carried_body == jelly_body;
}

glm::vec2 Level1::jelly_position(UnderwaterPhysics const &physics) const {
	//let the jellyfish drift around its position
	return physics.bodies[jelly_body].position + glm::vec2(0.18f * std::sin(clock * 0.3f), 0.25f * std::sin(clock * 0.65f));
}

char const *Level1::wanted_hint(UnderwaterPhysics const &physics, std::optional<size_t> carried_body) const {
	Body2D const &fish = physics.bodies[fish_body];
	if (!fish.active || escaped) return nullptr;
	if (!has_swum) return "WASD to swim";
	if (carried_body) return has_spat ? nullptr : "Press Space to spit";
	if (has_sucked) return nullptr;
	for (size_t i = 0; i < physics.bodies.size(); ++i) {
		Body2D const &body = physics.bodies[i];
		if (i != fish_body && body.active && glm::length(body.position - fish.position) < suck_hint_range) return "Hold Space to suck";
	}
	return nullptr;
}

float Level1::warning_amount() const {
	if (warning_time < 0.0f) return 0.0f;
	return std::clamp((warning_time - warning_quiet) / (warning_length - warning_quiet), 0.0f, 1.0f);
}

float Level1::roar_amount(UnderwaterPhysics const &physics) const {
	if (escaped || !physics.bodies[fish_body].active) return 0.0f;
	auto envelope = [&](float begin, float end) {
		if (warning_time <= begin || warning_time >= end) return 0.0f;
		return std::min(1.0f, std::min((warning_time - begin) / 0.25f, (end - warning_time) / 0.6f));
	};
	return std::max(envelope(far_begin, far_roar_end), envelope(warning_length, near_roar_end));
}

bool Level1::far_passing() const {
	float passing_time = (far_start - far_end) / far_speed;
	return warning_time >= far_begin && warning_time < far_begin + passing_time;
}

glm::vec2 Level1::far_hinge(glm::vec2 const &view_center) const {
	return glm::vec2(view_center.x + far_start - far_speed * (warning_time - far_begin), far_height);
}

glm::vec2 Level1::shaken_center(glm::vec2 const &center, UnderwaterPhysics const &physics) const {
	glm::vec2 view_center = center - big_fish.shake() + shake();
	{ //shake the camera during rockfalls and roars
		constexpr float rumble_size = 0.25f;
		//the rumble eases off over its last moments
		constexpr float rumble_fade = 0.3f;
		constexpr float tremble_size = 0.08f;
		float amount = rumble_size * std::min(1.0f, rumble / rumble_fade);
		amount += tremble_size * roar_amount(physics);
		view_center += amount * glm::vec2(std::sin(clock * 67.0f), std::cos(clock * 53.0f));
	}
	return view_center;
}

glm::vec3 Level1::prepare_water(Water::Look &look, UnderwaterPhysics const &physics, std::optional<size_t> carried_body,
	glm::vec2 const &view_center, glm::vec2 const &view_size) const {
	constexpr float eye_out_time = 0.15f;
	constexpr glm::vec2 glow_radius = glm::vec2(5.5f, 4.0f);
	const float eye = physics.bodies[fish_body].active ? 1.0f : std::max(0.0f, 1.0f - end_time / eye_out_time);
	const glm::vec2 half_view = 0.5f * view_size;
	look.glows.clear();
	look.blockers.clear();
	for (glm::vec3 const &spot : light_spots) {
		if (std::abs(spot.x - view_center.x) < half_view.x + 3.0f * glow_radius.x) look.glows.push_back(spot);
	}
	glm::vec3 jelly_source(0.0f);
	if (jelly_in_belly(carried_body) || physics.bodies[jelly_body].active) {
		glm::vec2 position = jelly_in_belly(carried_body) ? physics.bodies[fish_body].position : jelly_position(physics);
		float strength = jelly_light * (jelly_glow + jelly_glow_pulse * std::sin(clock * 1.3f));
		if (jelly_in_belly(carried_body)) strength *= eye;
		jelly_source = glm::vec3(position, strength);
	}
	if (std::abs(jelly_source.x - view_center.x) < half_view.x + 3.0f * glow_radius.x) look.glows.push_back(jelly_source);
	for (StaticBox2D const &box : light_blockers) look.blockers.emplace_back(box.min, box.max);
	look.glow_radius = glow_radius;
	look.brightness = scene_brightness;
	look.time = clock;
	look.bubble_columns = {
		glm::vec3(first_wall.x - 17.0f, physics.bounds_min.y - 1.5f, physics.bounds_max.y + 2.0f),
		glm::vec3(hanging_rock.center.x + 6.0f, physics.bounds_min.y - 1.5f, physics.bounds_max.y + 2.0f),
		glm::vec3(last_wall.x - 4.0f, physics.bounds_min.y - 1.5f, physics.bounds_max.y + 2.0f)
	};
	return jelly_source;
}

void Level1::draw_distant(DrawSprites const &sprites, glm::mat4 const &world_to_clip, glm::vec2 const &view_center) const {
	if (far_passing()) {
		big_fish.draw_far(sprites, world_to_clip, far_hinge(view_center));
	}
}

void Level1::draw_overlay(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics,
	glm::vec2 const &view_center, glm::vec2 const &view_size) const {
	Body2D const &fish = physics.bodies[fish_body];
	const glm::vec2 half_view = 0.5f * view_size;
	{ //fades in from black at the start, and out to black after being eaten
		float black = 1.0f - std::min(1.0f, clock / fade_in_time);
		if (!fish.active) black = std::max(black, std::min(1.0f, end_time / death_fade_time));
		if (black > 0.0f) {
			sprites.draw(fade_texture, world_to_clip, view_center, view_size, 0.0f, false, glm::u8vec4(0x00, 0x00, 0x00, uint8_t(255.0f * black)));
		}
	}

	//center text on the requested position
	auto draw_centered_text = [&](char const *text, glm::vec2 const &center, float text_height, glm::u8vec4 const &color) {
		DrawLines lines(world_to_clip);
		glm::vec3 text_end;
		lines.draw_text(text, glm::vec3(0.0f), glm::vec3(text_height, 0.0f, 0.0f), glm::vec3(0.0f, text_height, 0.0f), color, &text_end);
		glm::vec3 shift = glm::vec3(center - 0.5f * glm::vec2(text_end.x, text_height), 0.0f);
		for (DrawLines::Vertex &vertex : lines.attribs) vertex.Position += shift;
	};

	if (shown_hint && hint_amount > 0.0f) { //over the small fish, or under it if the ceiling or the jellyfish is in the way
		constexpr float view_margin = 5.0f;
		constexpr glm::vec2 jelly_clearance = glm::vec2(5.5f, 2.0f);
		float above = fish.position.y + hint_above_fish;
		glm::vec2 to_jelly = glm::abs(jelly_position(physics) - glm::vec2(fish.position.x, above));
		bool jelly_in_way = physics.bodies[jelly_body].active && to_jelly.x < jelly_clearance.x && to_jelly.y < jelly_clearance.y;
		float y = above + hint_height < view_center.y + half_view.y && !jelly_in_way ? above : fish.position.y - hint_above_fish;
		float x = std::clamp(fish.position.x, view_center.x - half_view.x + view_margin, view_center.x + half_view.x - view_margin);
		draw_centered_text(shown_hint, glm::vec2(x, y), hint_height, glm::u8vec4(0xc8, 0xd2, 0xd6, uint8_t(255.0f * hint_amount)));
	}

	if (escaped) { //fade to black, then the text
		constexpr float fade_time = 2.0f;
		constexpr float text_fade_time = 1.0f;
		constexpr float text_height = 1.5f;
		float fade = std::min(1.0f, end_time / fade_time);
		float text_fade = std::clamp((end_time - fade_time) / text_fade_time, 0.0f, 1.0f);
		//the cave png tinted black covers the whole view
		sprites.draw(fade_texture, world_to_clip, view_center, view_size, 0.0f, false, glm::u8vec4(0x00, 0x00, 0x00, uint8_t(255.0f * fade)));

		draw_centered_text("You Escaped", view_center, text_height, glm::u8vec4(0xff, 0xff, 0xff, uint8_t(255.0f * text_fade)));
	}
}
