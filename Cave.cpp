#include "Cave.hpp"

#include "data_path.hpp"

#include <algorithm>
#include <cmath>
#include <random>

static std::mt19937 rng{std::random_device{}()};
//based on https://en.cppreference.com/w/cpp/numeric/random/uniform_real_distribution
static float random_between(float low, float high) {
	return std::uniform_real_distribution< float >(low, high)(rng);
}

//how thick the walls are, and where the wall art sits across its png, as fractions of the png width
static constexpr float WallThickness = 3.0f;
static constexpr float WallArtLeft = 0.28f;
static constexpr float WallArtRight = 0.84f;
static constexpr int FirstWallHits = 2;
static constexpr int LastWallHits = 3;
// target height ranges measured from wall1, wall2 and wall3, as fractions down from the png top
static glm::vec2 const WallCrackHeights[] = {
	{0.48f, 0.95f}, {0.16f, 0.95f}, {0.0f, 0.98f}
};

//a box on a png, as fractions of the png measured from its top left
struct PngBox {
	float left, right, top, bottom;
};

//the hanging rock's png is drawn this many units square, with its top against the ceiling
static constexpr float HangingSize = 9.0f;
//the hanging rock's outline, as three boxes stacked from the ceiling down because it narrows toward its tip
static constexpr PngBox HangingShape[] = {
	{0.13f, 0.94f, 0.0f, 0.42f},
	{0.2f, 0.85f, 0.42f, 0.66f},
	{0.33f, 0.7f, 0.66f, 0.87f},
};
//its crack runs out to the right side of the lower part
static constexpr PngBox HangingCrack = {0.6f, 1.0f, 0.35f, 0.72f};
static constexpr int HangingHits = 3;
//how it falls once it breaks loose. it drifts left because the crack is on its right
static constexpr float FallGravity = 25.0f;
static constexpr float FallDrift = 2.5f;
static constexpr float FallSpin = 0.6f;
//how big the pile is once it lands, as fractions of HangingSize
static constexpr float RubbleHalfWidth = 0.35f;
static constexpr float RubbleHeight = 0.45f;
//how long the big fish is held up when the rock lands on it, or when it has to get past the pile
static constexpr float PinTime = 9.0f;
static constexpr float ChewTime = 3.0f;
//how far in front of its teeth the rock can land and still catch the big fish
static constexpr float PinReach = 2.0f;

//a rock moving slower than this was pushed, not spat. pushing tops out a little under 4, spitting starts above 10
static constexpr float RockHitSpeed = 5.0f;
//a hit thing shudders and throws chips
static constexpr float ShudderTime = 0.25f;
static constexpr float ShudderSize = 0.15f;
static constexpr int HitChips = 8;
//a hit off the crack only knocks off a couple of chips
static constexpr int MissChips = 2;
static constexpr int BreakChips = 24;
static constexpr float HitCooldown = 0.3f;
static constexpr float LooseRockRadius = 0.4f;
//particles slow down in the water at this rate per second
static constexpr float WaterDrag = 2.0f;
//height of the fallen rock's middle above the floor, as a fraction of HangingSize
static constexpr float RestHeight = 0.32f;

Cave::Cave() : wall_textures{SpriteTexture(data_path("wall1.png")), SpriteTexture(data_path("wall2.png")), SpriteTexture(data_path("wall3.png"))},
	hanging_textures{SpriteTexture(data_path("big_rock1.png")), SpriteTexture(data_path("big_rock2.png")), SpriteTexture(data_path("big_rock3.png"))},
	exit_texture(data_path("exit.png")),
	chip_texture(data_path("small_rock.png")) {
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
	place_wall(first_wall, bounds_min.x + 32.0f, FirstWallHits);
	place_wall(last_wall, bounds_max.x - 12.0f, LastWallHits);
	exit_x = bounds_max.x - 5.0f;

	hanging_rock = HangingRock();
	hanging_rock.hits_left = HangingHits;
	hanging_rock.center = glm::vec2(bounds_min.x + 68.0f, bounds_max.y - 0.5f * HangingSize);

	//a rock breaks apart when it hits a crack, so every hit needs a new one.
	//the last three sit far back from the last wall, so there is only time to break it after pinning the big fish
	for (glm::vec2 offset : {glm::vec2(42.0f, -3.0f), glm::vec2(48.0f, 2.5f),
		glm::vec2(72.0f, -3.5f), glm::vec2(75.0f, 2.0f), glm::vec2(78.0f, -1.0f),
		glm::vec2(90.0f, -3.5f), glm::vec2(90.5f, 2.5f), glm::vec2(91.0f, -0.5f)}) {
		physics.bodies.push_back({.position = glm::vec2(bounds_min.x + offset.x, offset.y), .radius = LooseRockRadius});
	}
}

//true if a rock moving fast will touch box within about this frame.
//the nearest point on the box comes from the same clamp UnderwaterPhysics::resolve_contact uses
static bool hits_box(Body2D const &rock, StaticBox2D const &box, float elapsed) {
	float speed = glm::length(rock.velocity);
	if (speed < RockHitSpeed) return false;
	glm::vec2 away = rock.position - glm::clamp(rock.position, box.min, box.max);
	float distance = glm::length(away);
	if (distance > rock.radius + 2.0f * speed * elapsed) return false;
	//a rock already moving away from the box just bounced off it
	return glm::dot(rock.velocity, away) < 0.0f;
}

//where a box on a png drawn size units square around center ends up in the world
static StaticBox2D world_box(PngBox const &box, glm::vec2 const &center, float size) {
	return StaticBox2D{center + size * glm::vec2(box.left - 0.5f, 0.5f - box.bottom), center + size * glm::vec2(box.right - 0.5f, 0.5f - box.top)};
}

static bool inside(glm::vec2 const &point, StaticBox2D const &box, float pad) {
	return point.x >= box.min.x - pad && point.x <= box.max.x + pad && point.y >= box.min.y - pad && point.y <= box.max.y + pad;
}

static StaticBox2D wall_crack(Cave::Wall const &wall, int stage) {
	float height = wall.box.max.y - wall.box.min.y;
	return StaticBox2D{glm::vec2(wall.box.min.x, wall.box.max.y - WallCrackHeights[stage].y * height),
		glm::vec2(wall.box.max.x, wall.box.max.y - WallCrackHeights[stage].x * height)};
}

int Cave::wall_stage(Wall const &wall) const {
	// the two-hit first wall skips wall2 so its first hit shows the most damaged artwork
	if (&wall == &first_wall) {
		return wall.hits_left == FirstWallHits ? 0 : 2;
	}
	return LastWallHits - wall.hits_left;
}

static StaticBox2D rubble_box(Cave::HangingRock const &rock, float floor) {
	return StaticBox2D{glm::vec2(rock.center.x - RubbleHalfWidth * HangingSize, floor),
		glm::vec2(rock.center.x + RubbleHalfWidth * HangingSize, floor + RubbleHeight * HangingSize)};
}

void Cave::update(UnderwaterPhysics &physics, BigFish &big_fish, float elapsed) {
	events = Events();
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

	// new rubble waits until the next update so its cooldown exists before hit checks
	for (size_t i = 0; i < hit_cooldown.size(); ++i) {
		//a copy, since breaking a wall adds bodies and can move the list
		Body2D body = physics.bodies[i];
		if (!body.active || body.kind != Body2D::Rock || hit_cooldown[i] > 0.0f) continue;

		bool hit = false;
		bool missed = false;
		//chips fly back the way the rock came from
		glm::vec2 away = -glm::normalize(body.velocity);
		//true if the rock lands on the crack. landing anywhere else only knocks off a couple of chips
		auto strikes = [&](StaticBox2D const &box, StaticBox2D const &crack) {
			if (!hits_box(body, box, elapsed)) return false;
			glm::vec2 contact = glm::clamp(body.position, box.min, box.max);
			if (!inside(contact, crack, body.radius)) {
				throw_chips(contact, away, MissChips);
				missed = true;
				return false;
			}
			throw_chips(contact, away, HitChips);
			physics.bodies[i].active = false;
			return true;
		};
		for (Wall *wall : {&first_wall, &last_wall}) {
			if (wall->hits_left <= 0 || !strikes(wall->box, wall_crack(*wall, wall_stage(*wall)))) continue;
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
				//the broken wall leaves a couple of rocks to throw at the next thing
				for (float y : {-2.0f, 2.0f}) {
					physics.bodies.push_back({.position = glm::vec2(wall->x, y), .radius = LooseRockRadius});
				}
			}
		}
		StaticBox2D crack = world_box(HangingCrack, hanging_rock.center, HangingSize);
		for (PngBox const &part : HangingShape) {
			if (hanging_rock.state != HangingRock::Hanging || hit || missed) break;
			if (!strikes(world_box(part, hanging_rock.center, HangingSize), crack)) continue;
			hit = true;
			hanging_rock.hits_left -= 1;
			hanging_rock.shudder = ShudderTime;
			if (hanging_rock.hits_left == 0) {
				hanging_rock.state = HangingRock::Falling;
				events.broke = true;
			}
		}
		if (hit) events.hit = true;
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
			for (int c = 0; c < BreakChips; ++c) {
				throw_chips(hanging_rock.center + glm::vec2(random_between(-3.0f, 3.0f), -2.0f), glm::vec2(0.0f, 1.0f), 1);
			}
			//landing on the big fish's head pins it for a long time
			float rock_left = hanging_rock.center.x - RubbleHalfWidth * HangingSize;
			if (big_fish.state == BigFish::Normal && rock_left < big_fish.teeth_x() + PinReach && hanging_rock.center.x > big_fish.position.x) {
				big_fish.stun(PinTime);
				hanging_rock.used = true;
			}
		}
	}
	//a pile the big fish hasn't been caught by yet still holds it up while it gets past
	if (hanging_rock.state == HangingRock::Fallen && !hanging_rock.used && big_fish.state == BigFish::Normal
		&& big_fish.teeth_x() >= rubble_box(hanging_rock, bounds_min.y).min.x) {
		big_fish.stun(ChewTime);
		hanging_rock.used = true;
	}

	physics.obstacles.clear();
	for (Wall const *wall : {&first_wall, &last_wall}) {
		if (wall->hits_left > 0) physics.obstacles.push_back(wall->box);
	}
	if (hanging_rock.state == HangingRock::Hanging) {
		for (PngBox const &part : HangingShape) physics.obstacles.push_back(world_box(part, hanging_rock.center, HangingSize));
	}
	if (hanging_rock.state == HangingRock::Fallen) physics.obstacles.push_back(rubble_box(hanging_rock, bounds_min.y));
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

//sideways wobble for something hit shudder seconds ago, dying away as the shudder runs out
static float shudder_offset(float shudder) {
	return ShudderSize * (shudder / ShudderTime) * std::sin(shudder * 90.0f);
}

void Cave::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	float height = bounds_max.y - bounds_min.y;
	float middle_y = 0.5f * (bounds_min.y + bounds_max.y);

	{ //the way out, past the last wall
		constexpr float ExitWidth = 6.0f;
		sprites.draw(exit_texture, world_to_clip, glm::vec2(bounds_max.x - 0.5f * ExitWidth, middle_y), glm::vec2(ExitWidth, height));
	}

	//the wall png is wider than its art, so it is shifted to put the art on the wall's box
	float png_width = WallThickness / (WallArtRight - WallArtLeft);
	float art_middle = 0.5f * (WallArtLeft + WallArtRight);
	for (Wall const *wall : {&first_wall, &last_wall}) {
		if (wall->hits_left <= 0) continue;
		glm::vec2 center = glm::vec2(wall->x - (art_middle - 0.5f) * png_width + shudder_offset(wall->shudder), middle_y);
		sprites.draw(wall_textures[wall_stage(*wall)], world_to_clip, center, glm::vec2(png_width, height));
	}

	if (hanging_rock.state == HangingRock::Hanging) {
		glm::vec2 center = hanging_rock.center + glm::vec2(shudder_offset(hanging_rock.shudder), 0.0f);
		sprites.draw(hanging_textures[HangingHits - hanging_rock.hits_left], world_to_clip, center, glm::vec2(HangingSize));
	}

	for (Particle const &particle : particles) {
		//fade out over the last part of their life
		float alpha = std::min(1.0f, 2.0f * particle.life / particle.max_life);
		sprites.draw(chip_texture, world_to_clip, particle.position, glm::vec2(particle.size), particle.angle, false,
			glm::u8vec4(0xc0, 0xc0, 0xc0, uint8_t(255.0f * alpha)));
	}
}

void Cave::draw_over_big_fish(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	if (hanging_rock.state == HangingRock::Hanging) return;
	sprites.draw(hanging_textures[2], world_to_clip, hanging_rock.center, glm::vec2(HangingSize), hanging_rock.angle);
}
