#include "BigFish.hpp"

#include "data_path.hpp"

#include <algorithm>
#include <cmath>

//measured off the jaw pngs, as fractions of the png width. if the art changes, these need to be measured again.
//LowerLift raises the lower jaw so the pngs meet at the back of the head. MouthTop and MouthBottom are the inner edge of each jaw
static constexpr float LowerLift = 0.045f;
static constexpr float MouthFront = 0.6f;
static constexpr float MouthTop = 0.125f;
static constexpr float MouthBottom = 0.04f;
static constexpr float ArtFront = 0.66f;

//counterclockwise turn of each jaw with the mouth open and shut
static constexpr float Degrees = float(M_PI) / 180.0f;
static constexpr float UpperOpenAngle = -6.0f * Degrees;
static constexpr float UpperShutAngle = -11.0f * Degrees;
static constexpr float LowerOpenAngle = -9.0f * Degrees;
static constexpr float LowerShutAngle = 10.0f * Degrees;

//the charge. it starts when the small fish is RevealRange in front of its teeth,
//keeps normal speed for CreepTime seconds, then gains ChargeAccel units per second every second
static constexpr float RevealRange = 10.0f;
static constexpr float CreepTime = 2.0f;
static constexpr float ChargeAccel = 1.5f;

//the jaws fade in once the lure has gone up, then open wider until OpenTime seconds into the charge.
//for the closed values, 0 is as wide as they go and 1 is shut
static constexpr float RevealDelay = 0.8f;
static constexpr float RevealTime = 1.5f;
static constexpr float FadeTime = 2.0f;
static constexpr float RevealClosed = 0.4f;
static constexpr float WidestClosed = 0.0f;
static constexpr float OpenTime = 7.0f;
static constexpr float JawEase = 0.3f;

//the slam happens once the small fish is SnapDepth inside the mouth. HoldTime outlasts the fade to black after being eaten
static constexpr float SnapDepth = 3.0f;
static constexpr float SlamTime = 0.18f;
static constexpr float HoldTime = 2.0f;
static constexpr float CatchFront = 0.6f;

//the mouth slowly opens and closes, and the lure dims along with it
static constexpr float BreatheClosed = 0.06f;
static constexpr float BreathePeriod = 4.0f;
static constexpr float LureDim = 0.2f;

//up and down drift, two sine waves added together so it doesn't look regular
static constexpr float BobHeight = 0.5f;
static constexpr float BobPeriod = 6.0f;
static constexpr float BobSecondPeriod = 2.3f;
static constexpr float BobSecondShare = 0.35f;

//its speed while stalking rises and falls on two sine waves, like swimming in strokes.
//the two sizes add up to 1, so it can slow to a stop but never goes backward
static constexpr float SwimPeriod = 2.7f;
static constexpr float SwimSize = 0.75f;
static constexpr float SwimSecondPeriod = 4.3f;
static constexpr float SwimSecondSize = 0.25f;

//how fast the mouth lines up with the small fish, and how close to the top and bottom of the corridor it can go
static constexpr float TrackSpeed = 3.0f;
static constexpr float ChargeTrackSpeed = 6.0f;
static constexpr float MouthMargin = 3.0f;

//the lure in the art is far above the view at this size, so the light you see is placed separately.
//LureOutOfSight is how far off the view its glow stops reaching the screen
static constexpr float LureAhead = 6.0f;
static constexpr float LureOutOfSight = 12.0f;
static constexpr float LureMargin = 3.0f;
static constexpr float LureGone = 8.0f;
static constexpr float LureRiseSpeed = 5.0f;
static constexpr float LureReturnSpeed = 2.0f;
static constexpr float LureSize = 1.2f;

//screen shake when the jaws slam, and the tremble that builds during a charge
static constexpr float ShakeSize = 0.5f;
static constexpr float ShakeTime = 0.5f;
static constexpr float ChargeShake = 0.12f;

//based on https://en.wikipedia.org/wiki/Smoothstep
static float smooth(float u) {
	u = std::clamp(u, 0.0f, 1.0f);
	return u * u * (3.0f - 2.0f * u);
}

BigFish::BigFish() : upper_jaw(data_path("upper_jaw.png")), lower_jaw(data_path("lower_jaw.png")),
	lure_texture(data_path("lure_glow.png")) {
}

void BigFish::reset(glm::vec2 const &start) {
	state = Waiting;
	position = start;
	base_y = start.y;
	prey = start;
	time = 0.0f;
	slammed = false;
	jaw = RevealClosed;
	shown = 0.0f;
	shake_left = 0.0f;
}

void BigFish::reset_for_world(glm::vec2 const &bounds_min, glm::vec2 const &bounds_max) {
	lowest_mouth = bounds_min.y + MouthMargin;
	highest_mouth = bounds_max.y - MouthMargin;
	lure_top = bounds_max.y;
	lure_y = lure_top - LureMargin;
	reset(glm::vec2(bounds_min.x - art_front(), 0.0f));
}

void BigFish::update(float elapsed) {
	slammed = false;
	time += elapsed;
	shake_left = std::max(0.0f, shake_left - elapsed);
	if (state == Waiting) return;

	float mouth_target = std::clamp(prey.y, lowest_mouth, highest_mouth);
	float step = (state == Normal ? TrackSpeed : ChargeTrackSpeed) * elapsed;
	base_y += std::clamp(mouth_target - mouth_offset(jaw) - base_y, -step, step);
	position.y = base_y + BobHeight * ((1.0f - BobSecondShare) * std::sin(time * 2.0f * float(M_PI) / BobPeriod)
		+ BobSecondShare * std::sin(time * 2.0f * float(M_PI) / BobSecondPeriod + 1.0f));

	float lure_target = state == Normal ? lure_top - LureMargin : lure_top + LureGone;
	float lure_step = (state == Normal ? LureReturnSpeed : LureRiseSpeed) * elapsed;
	lure_y += std::clamp(lure_target - lure_y, -lure_step, lure_step);

	float breathe = BreatheClosed * (0.5f - 0.5f * std::cos(time * 2.0f * float(M_PI) / BreathePeriod));
	float jaw_target = RevealClosed + breathe;
	float shown_target = 0.0f;

	if (state == Normal) {
		position.x += speed * stroke() * elapsed;
	} else if (state == Charging) {
		charge_time += elapsed;
		position.x += charge_speed() * elapsed;
		jaw_target = glm::mix(RevealClosed + breathe, WidestClosed, smooth(charge_time / OpenTime));
		if (charge_time > RevealDelay) shown_target = 1.0f;
		if (prey.x < teeth_x() - SnapDepth) {
			state = Slamming;
			timer = SlamTime + HoldTime;
			slam_from = jaw;
		}
	} else if (state == Slamming) {
		if (timer > HoldTime) position.x += charge_speed() * elapsed;
		shown_target = 1.0f;
		float before = timer;
		timer -= elapsed;
		if (before > HoldTime && timer <= HoldTime) {
			slammed = true;
			shake_left = ShakeTime;
		}
		if (timer <= 0.0f) state = Normal;
	} else {
		shown_target = 1.0f;
		timer -= elapsed;
		if (timer <= 0.0f) state = Normal;
	}

	if (state == Slamming) {
		float u = std::min(1.0f, (SlamTime + HoldTime - timer) / SlamTime);
		jaw = glm::mix(slam_from, 1.0f, u * u);
	} else {
		jaw += (jaw_target - jaw) * (1.0f - std::exp(-elapsed / JawEase));
	}
	float shown_rate = elapsed / (shown_target > shown ? RevealTime : FadeTime);
	shown += std::clamp(shown_target - shown, -shown_rate, shown_rate);
}

void BigFish::enter(float x) {
	state = Normal;
	position.x = x;
}

float BigFish::out_of_sight_x(float view_left) const {
	return view_left - LureOutOfSight - LureAhead - MouthFront * size;
}

void BigFish::stun(float seconds) {
	if (state == Waiting) return;
	state = Stunned;
	timer = seconds;
}

void BigFish::charge() {
	if (state != Normal) return;
	state = Charging;
	charge_time = 0.0f;
}

bool BigFish::swimming() const {
	return state == Normal || state == Charging;
}

bool BigFish::in_reach(glm::vec2 const &point) const {
	return state == Normal && point.x - teeth_x() < RevealRange;
}

bool BigFish::in_mouth(glm::vec2 const &point) const {
	return point.x < teeth_x() + CatchFront;
}

float BigFish::closed() const {
	return jaw;
}

float BigFish::charge_speed() const {
	return speed + ChargeAccel * std::max(0.0f, charge_time - CreepTime);
}

float BigFish::mouth_offset(float shut) const {
	float front = MouthFront * size;
	float top = MouthTop * size + front * std::tan(glm::mix(UpperOpenAngle, UpperShutAngle, shut));
	float bottom = -MouthBottom * size + front * std::tan(glm::mix(LowerOpenAngle, LowerShutAngle, shut));
	return 0.5f * (top + bottom);
}

float BigFish::stroke() const {
	return 1.0f + SwimSize * std::sin(time * 2.0f * float(M_PI) / SwimPeriod)
		+ SwimSecondSize * std::sin(time * 2.0f * float(M_PI) / SwimSecondPeriod + 1.0f);
}

float BigFish::art_front() const {
	return ArtFront * size;
}

float BigFish::teeth_x() const {
	return position.x + MouthFront * size;
}

glm::vec2 BigFish::lure_position() const {
	return glm::vec2(teeth_x() + LureAhead, lure_y + position.y - base_y);
}

float BigFish::lure_brightness() const {
	return 1.0f - LureDim * (0.5f - 0.5f * std::cos(time * 2.0f * float(M_PI) / BreathePeriod));
}

float BigFish::visibility() const {
	return smooth(shown);
}

glm::vec2 BigFish::shake() const {
	float amount = ShakeSize * shake_left / ShakeTime;
	if (state == Charging) amount += ChargeShake * smooth(charge_time / OpenTime);
	//two sines at unrelated rates make an uneven wobble
	return amount * glm::vec2(std::sin(time * 73.0f), std::cos(time * 59.0f));
}

void BigFish::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	if (state == Waiting) return;

	float h = size * float(upper_jaw.pixel_size.y) / float(upper_jaw.pixel_size.x);
	float c = closed();

	//each jaw turns around position, so its center swings around that point with it.
	//based on https://en.wikipedia.org/wiki/Rotation_matrix
	glm::u8vec4 tint = glm::u8vec4(255, 255, 255, uint8_t(255.0f * visibility()));
	auto draw_jaw = [&](SpriteTexture const &jaw_texture, glm::vec2 const &offset, float angle) {
		float co = std::cos(angle);
		float si = std::sin(angle);
		glm::vec2 center = position + glm::vec2(co * offset.x - si * offset.y, si * offset.x + co * offset.y);
		sprites.draw(jaw_texture, world_to_clip, center, glm::vec2(size, h), angle, false, tint, 0.0f, DrawSprites::Predator);
	};
	//upper jaw last so it covers the lower jaw where they overlap
	if (tint.a > 0) {
		draw_jaw(lower_jaw, glm::vec2(0.5f * size, -0.5f * h + LowerLift * size), glm::mix(LowerOpenAngle, LowerShutAngle, c));
		draw_jaw(upper_jaw, glm::vec2(0.5f * size, 0.5f * h), glm::mix(UpperOpenAngle, UpperShutAngle, c));
	}

	//same pale color as the jellyfish
	constexpr glm::u8vec4 LureColor = glm::u8vec4(175, 185, 190, 200);
	float bright = std::min(1.0f, 0.6f * lure_brightness());
	glm::u8vec4 lure_color = glm::u8vec4(glm::vec4(LureColor) * glm::vec4(glm::vec3(bright), 1.0f));
	sprites.draw(lure_texture, world_to_clip, lure_position(), glm::vec2(LureSize), 0.0f, false, lure_color, 0.0f, DrawSprites::Emissive);
}
