#include "BigFish.hpp"

#include "data_path.hpp"

#include <algorithm>
#include <cmath>
#include <random>

static std::mt19937 rng{std::random_device{}()};

//these are measured off the jaw pngs, as fractions of the png width.
//if the art changes, these need to be measured again.
//how far the lower jaw png is raised so the two pngs meet at the back of the head
static constexpr float LowerLift = 0.045f;
//back and front of the mouth, measured from position
static constexpr float MouthBack = 0.3f;
static constexpr float MouthFront = 0.6f;
//inner edge of each jaw above and below the line where they meet
static constexpr float MouthTop = 0.125f;
static constexpr float MouthBottom = 0.04f;

//how far each jaw is turned away from the other while the mouth gapes
static constexpr float UpperOpenAngle = 5.0f * float(M_PI) / 180.0f;
static constexpr float LowerOpenAngle = 12.0f * float(M_PI) / 180.0f;
//how far up the lower jaw turns past the line to shut the mouth
static constexpr float LowerShutAngle = 18.0f * float(M_PI) / 180.0f;

BigFish::BigFish() : upper_jaw(data_path("upper_jaw.png")), lower_jaw(data_path("lower_jaw.png")) {
}

void BigFish::reset(glm::vec2 const &start) {
	state = Waiting;
	position = start;
	bite_timer = 0.0f;
	//based on https://en.cppreference.com/w/cpp/numeric/random/uniform_real_distribution
	timer = std::uniform_real_distribution< float >(MinDelay, MaxDelay)(rng);
}

void BigFish::update(float elapsed) {
	if (state == Waiting) {
		timer -= elapsed;
		if (timer <= 0.0f) state = Normal;
		return;
	}

	bite_timer = std::max(0.0f, bite_timer - elapsed);

	if (state == Normal) {
		position.x += speed * elapsed;
	} else {
		//stunned, so stay still until the timer runs out
		timer -= elapsed;
		if (timer <= 0.0f) state = Normal;
	}
}

void BigFish::stun(float seconds) {
	if (state == Waiting) return;
	state = Stunned;
	timer = seconds;
}

void BigFish::bite() {
	bite_timer = BiteTime;
}

float BigFish::closed() const {
	if (bite_timer <= 0.0f) return 0.0f;
	float t = 1.0f - bite_timer / BiteTime;
	//snaps shut fast, stays shut for a moment, then slowly opens
	if (t < 0.2f) return t / 0.2f;
	if (t < 0.6f) return 1.0f;
	return (1.0f - t) / 0.4f;
}

//counterclockwise turn of each jaw for how shut the mouth is
static float upper_angle(float closed) {
	return (1.0f - closed) * UpperOpenAngle;
}
static float lower_angle(float closed) {
	return closed * LowerShutAngle - (1.0f - closed) * LowerOpenAngle;
}

float BigFish::mouth_front() const {
	return MouthFront * size;
}

bool BigFish::in_mouth(glm::vec2 const &p) const {
	if (state != Normal) return false;
	glm::vec2 d = p - position;
	if (d.x < MouthBack * size || d.x > MouthFront * size) return false;
	float top = MouthTop * size + d.x * std::tan(upper_angle(closed()));
	float bottom = -MouthBottom * size + d.x * std::tan(lower_angle(closed()));
	return d.y < top && d.y > bottom;
}

bool BigFish::hits(glm::vec2 const &p, float radius) const {
	if (state == Waiting) return false;
	return p.x - radius < position.x + mouth_front();
}

void BigFish::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	if (state == Waiting) return;

	//draw it darker while it's stopped
	glm::u8vec4 tint = (state == Normal ? glm::u8vec4(0xff) : glm::u8vec4(0x77, 0x77, 0x77, 0xff));
	float h = size * float(upper_jaw.pixel_size.y) / float(upper_jaw.pixel_size.x);

	//each jaw turns around position, so its center swings around that point with it
	auto draw_jaw = [&](SpriteTexture const &jaw, glm::vec2 const &offset, float angle) {
		float c = std::cos(angle);
		float s = std::sin(angle);
		glm::vec2 center = position + glm::vec2(c * offset.x - s * offset.y, s * offset.x + c * offset.y);
		sprites.draw(jaw, world_to_clip, center, glm::vec2(size, h), angle, false, tint);
	};
	//the upper jaw is drawn last so it covers the lower jaw where they overlap
	draw_jaw(lower_jaw, glm::vec2(0.5f * size, -0.5f * h + LowerLift * size), lower_angle(closed()));
	draw_jaw(upper_jaw, glm::vec2(0.5f * size, 0.5f * h), upper_angle(closed()));
}
