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
//furthest right the jaw art reaches once the upper jaw is tilted
static constexpr float ArtFront = 0.66f;

//counterclockwise turn of each jaw with the mouth open and shut
static constexpr float Degrees = float(M_PI) / 180.0f;
static constexpr float UpperOpenAngle = -6.0f * Degrees;
static constexpr float UpperShutAngle = -11.0f * Degrees;
static constexpr float LowerOpenAngle = -9.0f * Degrees;
static constexpr float LowerShutAngle = 10.0f * Degrees;

//a bite creeps the jaws partway closed, slams them, holds, then slowly opens again
static constexpr float CreepTime = 1.0f;
static constexpr float CreepClosed = 0.3f;
static constexpr float SlamTime = 0.12f;
static constexpr float HoldTime = 0.5f;
static constexpr float OpenTime = 1.5f;
static constexpr float SlamDone = CreepTime + SlamTime;

//the mouth slowly opens and closes this much on its own, like breathing
static constexpr float BreatheClosed = 0.06f;
static constexpr float BreathePeriod = 4.0f;
//the whole fish drifts up and down this far
static constexpr float BobHeight = 0.5f;
static constexpr float BobPeriod = 6.0f;
//how far and how long the screen shakes after a slam
static constexpr float ShakeSize = 0.4f;
static constexpr float ShakeTime = 0.4f;

BigFish::BigFish() : upper_jaw(data_path("upper_jaw.png")), lower_jaw(data_path("lower_jaw.png")) {
}

void BigFish::reset(glm::vec2 const &start) {
	state = Waiting;
	position = start;
	base_y = start.y;
	bite_time = -1.0f;
	time = 0.0f;
	slammed = false;
	//based on https://en.cppreference.com/w/cpp/numeric/random/uniform_real_distribution
	timer = std::uniform_real_distribution< float >(MinDelay, MaxDelay)(rng);
}

void BigFish::update(float elapsed) {
	slammed = false;
	time += elapsed;
	if (state == Waiting) {
		timer -= elapsed;
		if (timer <= 0.0f) state = Normal;
		return;
	}

	position.y = base_y + BobHeight * std::sin(time * 2.0f * float(M_PI) / BobPeriod);

	if (biting()) {
		float before = bite_time;
		bite_time += elapsed;
		if (before < SlamDone && bite_time >= SlamDone) slammed = true;
		if (bite_time >= SlamDone + HoldTime + OpenTime) bite_time = -1.0f;
	}

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
	if (biting()) return;
	bite_time = 0.0f;
}

bool BigFish::biting() const {
	return bite_time >= 0.0f;
}

float BigFish::closed() const {
	float breathe = BreatheClosed * (0.5f - 0.5f * std::cos(time * 2.0f * float(M_PI) / BreathePeriod));
	if (!biting()) return breathe;

	float t = bite_time;
	float bite = 0.0f;
	if (t < CreepTime) {
		//starts slow and speeds up
		float u = t / CreepTime;
		bite = CreepClosed * u * u;
	} else if (t < SlamDone) {
		float u = (t - CreepTime) / SlamTime;
		bite = CreepClosed + (1.0f - CreepClosed) * u * u;
	} else if (t < SlamDone + HoldTime) {
		bite = 1.0f;
	} else {
		float u = (t - SlamDone - HoldTime) / OpenTime;
		bite = 1.0f - u * u * (3.0f - 2.0f * u);
	}
	return std::max(breathe, bite);
}

float BigFish::art_front() const {
	return ArtFront * size;
}

//inner edges of the jaws at distance x in front of position.
//measured with the jaws partway shut, so whatever is in there when they slam gets caught
static float mouth_top(float x, float size, float closed) {
	float c = std::min(closed, CreepClosed);
	return MouthTop * size + x * std::tan(glm::mix(UpperOpenAngle, UpperShutAngle, c));
}
static float mouth_bottom(float x, float size, float closed) {
	float c = std::min(closed, CreepClosed);
	return -MouthBottom * size + x * std::tan(glm::mix(LowerOpenAngle, LowerShutAngle, c));
}

bool BigFish::between_jaws(glm::vec2 const &p) const {
	if (state == Waiting) return false;
	glm::vec2 d = p - position;
	if (d.x < MouthBack * size || d.x > MouthFront * size) return false;
	return d.y < mouth_top(d.x, size, closed()) && d.y > mouth_bottom(d.x, size, closed());
}

bool BigFish::hits(glm::vec2 const &p, float radius) const {
	if (state == Waiting) return false;
	glm::vec2 d = p - position;
	if (d.x - radius > MouthFront * size) return false;
	//behind the mouth it's all head
	if (d.x + radius < MouthBack * size) return true;
	return d.y + radius > mouth_top(d.x, size, closed()) || d.y - radius < mouth_bottom(d.x, size, closed());
}

glm::vec2 BigFish::shake() const {
	if (!biting() || bite_time < SlamDone) return glm::vec2(0.0f);
	float left = 1.0f - (bite_time - SlamDone) / ShakeTime;
	if (left <= 0.0f) return glm::vec2(0.0f);
	//two sines at unrelated rates make an uneven wobble
	return ShakeSize * left * glm::vec2(std::sin(time * 73.0f), std::cos(time * 59.0f));
}

void BigFish::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	if (state == Waiting) return;

	//draw it darker while it's stopped
	glm::u8vec4 tint = (state == Normal ? glm::u8vec4(0xff) : glm::u8vec4(0x77, 0x77, 0x77, 0xff));
	float h = size * float(upper_jaw.pixel_size.y) / float(upper_jaw.pixel_size.x);
	float c = closed();

	//each jaw turns around position, so its center swings around that point with it
	auto draw_jaw = [&](SpriteTexture const &jaw, glm::vec2 const &offset, float angle) {
		float co = std::cos(angle);
		float si = std::sin(angle);
		glm::vec2 center = position + glm::vec2(co * offset.x - si * offset.y, si * offset.x + co * offset.y);
		sprites.draw(jaw, world_to_clip, center, glm::vec2(size, h), angle, false, tint);
	};
	//the upper jaw is drawn last so it covers the lower jaw where they overlap
	draw_jaw(lower_jaw, glm::vec2(0.5f * size, -0.5f * h + LowerLift * size), glm::mix(LowerOpenAngle, LowerShutAngle, c));
	draw_jaw(upper_jaw, glm::vec2(0.5f * size, 0.5f * h), glm::mix(UpperOpenAngle, UpperShutAngle, c));
}
