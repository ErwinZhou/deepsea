#include "BigFish.hpp"

#include "data_path.hpp"

#include <cmath>
#include <random>

static std::mt19937 rng{std::random_device{}()};

//how far the jaws open, as a fraction of the jaw width
static constexpr float MaxGap = 0.6f;
//how far in front of the jaws it can suck things in, in jaw widths
static constexpr float SuckReach = 1.0f;

BigFish::BigFish() : jaw_texture(data_path("big-fish-jaw.png")) {
}

void BigFish::reset(glm::vec2 const &start) {
	state = Waiting;
	position = start;
	mouth_phase = 0.0f;
	//based on https://en.cppreference.com/w/cpp/numeric/random/uniform_real_distribution
	timer = std::uniform_real_distribution< float >(MinDelay, MaxDelay)(rng);
}

void BigFish::update(float elapsed) {
	if (state == Waiting) {
		timer -= elapsed;
		if (timer <= 0.0f) state = Normal;
		return;
	}

	mouth_phase += elapsed / MouthPeriod;
	mouth_phase -= std::floor(mouth_phase);

	if (state == Normal) {
		position.x += speed * elapsed;
	} else {
		//rocked or stunned, so stay still until the timer runs out
		timer -= elapsed;
		if (timer <= 0.0f) state = Normal;
	}
}

void BigFish::rock() {
	if (state == Waiting) return;
	state = Rocked;
	timer = RockedTime;
}

void BigFish::stun() {
	if (state == Waiting) return;
	state = Stunned;
	timer = StunnedTime;
}

float BigFish::gap() const {
	float open = 0.5f - 0.5f * std::cos(mouth_phase * 2.0f * float(M_PI));
	return open * MaxGap * size;
}

bool BigFish::can_suck(glm::vec2 const &p) const {
	if (state != Normal) return false;
	glm::vec2 d = p - position;
	if (d.x < -0.5f * size || d.x > (0.5f + SuckReach) * size) return false;
	return std::abs(d.y) < 0.5f * gap();
}

void BigFish::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	if (state == Waiting) return;

	//draw it darker while it's stopped
	glm::u8vec4 tint = (state == Normal ? glm::u8vec4(0xff) : glm::u8vec4(0x77, 0x77, 0x77, 0xff));
	float h = size * float(jaw_texture.pixel_size.y) / float(jaw_texture.pixel_size.x);
	float y = 0.5f * gap() + 0.5f * h;

	sprites.draw(jaw_texture, world_to_clip, position + glm::vec2(0.0f, y), glm::vec2(size, h), 0.0f, false, tint);
	//turning it half a circle and mirroring it flips the top jaw upside down
	sprites.draw(jaw_texture, world_to_clip, position - glm::vec2(0.0f, y), glm::vec2(size, h), float(M_PI), true, tint);
}
