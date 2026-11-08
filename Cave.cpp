#include "Cave.hpp"

#include "data_path.hpp"
#include "load_save_png.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <limits>
#include <string>
#include <cstddef>

static std::mt19937 rng{std::random_device{}()};
//based on https://en.cppreference.com/w/cpp/numeric/random/uniform_real_distribution
static float random_between(float low, float high) {
	return std::uniform_real_distribution< float >(low, high)(rng);
}

//how thick the walls are, and where the wall art sits across its png, as fractions of the png width
static constexpr float WallThickness = 3.0f;
static constexpr float WallArtLeft = 0.28f;
static constexpr float WallArtRight = 0.84f;
static constexpr int WallHits = 3;
static constexpr float CrackLeeway = 0.35f;
static constexpr float HangingCrackLeeway = 0.55f;
static constexpr glm::vec2 WallCrackHalfSize = glm::vec2(0.15f, 0.5f);
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

// build collision boxes and crack hit locations from the wall or pillar image
static Cave::Target load_target(std::string const &filename) {
	glm::uvec2 size;
	std::vector< glm::u8vec4 > pixels;
	load_png(filename, &size, &pixels, UpperLeftOrigin);
	Cave::Target target;
	target.texel_size = 1.0f / glm::vec2(size);
	constexpr unsigned Strips = 16;
	for (unsigned strip = 0; strip < Strips; ++strip) {
		glm::vec4 part(1.0f, 0.0f, 1.0f, 0.0f);
		for (unsigned y = strip * size.y / Strips; y < (strip + 1) * size.y / Strips; ++y) {
			for (unsigned x = 0; x < size.x; ++x) {
				glm::u8vec4 const &pixel = pixels[size_t(y) * size.x + x];
				if (pixel.a < 128) continue;
				part.x = std::min(part.x, float(x) / float(size.x));
				part.y = std::max(part.y, float(x + 1) / float(size.x));
				part.z = std::min(part.z, float(y) / float(size.y));
				part.w = std::max(part.w, float(y + 1) / float(size.y));
				// the crack paint is gray against the black rock
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

Cave::Cave() : wall_textures{SpriteTexture(data_path("wall1.png")), SpriteTexture(data_path("wall2.png")), SpriteTexture(data_path("wall3.png"))},
	hanging_textures{SpriteTexture(data_path("big_rock1.png")), SpriteTexture(data_path("big_rock2.png")), SpriteTexture(data_path("big_rock3.png"))},
	exit_texture(data_path("exit.png")),
	coral_texture(data_path("coral.png")),
	chip_texture(data_path("small_rock.png")) {
	for (int stage = 0; stage < 3; ++stage) {
		wall_targets[stage] = load_target(data_path("wall" + std::to_string(stage + 1) + ".png"));
		hanging_targets[stage] = load_target(data_path("big_rock" + std::to_string(stage + 1) + ".png"));
	}
}

void Cave::setup(UnderwaterPhysics &physics) {
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
		// at a corner the circle reaches the box after its bounding square does
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

glm::vec2 Cave::wall_crack_position(Wall const &wall) const {
	float png_width = WallThickness / (WallArtRight - WallArtLeft);
	float art_middle = 0.5f * (WallArtLeft + WallArtRight);
	glm::vec2 center(wall.x - (art_middle - 0.5f) * png_width, 0.5f * (bounds_min.y + bounds_max.y));
	float height = bounds_max.y - bounds_min.y;
	return world_point(wall_targets[std::min(2, wall_stage(wall))].entry, center, glm::vec2(png_width, &wall == &first_wall ? -height : height), 0.0f);
}

int Cave::wall_stage(Wall const &wall) const {
	return WallHits - wall.hits_left;
}

void Cave::update(UnderwaterPhysics &physics, BigFish &big_fish, float elapsed) {
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

	// new drops wait until the next update so their cooldowns exist before hit checks
	for (size_t i = 0; i < hit_cooldown.size(); ++i) {
		// copy the body because a pillar hit appends stones and can reallocate the list
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
				glm::vec2 tolerance = WallCrackHalfSize + glm::vec2(reach);
				cracked = delta.x <= tolerance.x && delta.y <= tolerance.y;
			} else {
				for (glm::vec2 const &pixel : target.cracks) {
					glm::vec2 delta = size * glm::vec2(pixel.x - 0.5f, 0.5f - pixel.y) - local_contact;
					if (glm::dot(delta, delta) <= reach * reach) { cracked = true; break; }
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

void Cave::throw_chips(glm::vec2 const &at, glm::vec2 const &away, int count) {
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

void Cave::drop_ceiling_dust(float left, float right, float amount, float elapsed) {
	//grains per second at full amount
	constexpr float DustPerSecond = 40.0f;
	float expected = amount * DustPerSecond * elapsed;
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

void Cave::update_coral() {
	float co = std::cos(hanging_rock.angle);
	float si = std::sin(hanging_rock.angle);
	constexpr glm::vec2 Mount(-3.6f, -0.8f);
	coral_position = hanging_rock.center + glm::vec2(co * Mount.x - si * Mount.y, si * Mount.x + co * Mount.y);
	if (hanging_rock.state == HangingRock::Hanging) coral_position.x += shudder_offset(hanging_rock.shudder);
	light_spots[1] = glm::vec3(coral_position + 0.6f * glm::vec2(co, si), 1.0f);
}

glm::vec2 Cave::shake() const {
	const float amount = RockfallShakeSize * hanging_rock.shudder / ShudderTime;
	return amount * glm::vec2(std::sin(hanging_rock.shudder * 67.0f), std::cos(hanging_rock.shudder * 53.0f));
}

void Cave::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	float height = bounds_max.y - bounds_min.y;
	float middle_y = 0.5f * (bounds_min.y + bounds_max.y);

	{ //the way out, past the last wall
		constexpr float ExitWidth = 6.0f;
		sprites.draw(exit_texture, world_to_clip, glm::vec2(bounds_max.x - 0.5f * ExitWidth + ExitShift, middle_y), glm::vec2(ExitWidth, height));
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

void Cave::draw_over_big_fish(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	// the pillar draws in front of the jaws
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
