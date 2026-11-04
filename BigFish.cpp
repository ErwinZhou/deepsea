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

//its speed is ChaseSlowest at ChargeRange behind the small fish, and gains ChaseGain for every unit further back.
//that matches the small fish's top speed about 29 units back, a little past the left of the view
static constexpr float ChaseSlowest = 1.0f;
static constexpr float ChaseGain = 0.11f;
static constexpr float ChaseFastest = 7.0f;

//the charge. it starts when the small fish is ChargeRange in front of its teeth, then gains ChargeAccel units per second every second
static constexpr float ChargeRange = 6.0f;
static constexpr float ChargeAccel = 1.5f;

//the jaws open wider until OpenTime seconds into the charge.
//for the closed values, 0 is as wide as they go and 1 is shut
static constexpr float RestClosed = 0.4f;
static constexpr float WidestClosed = 0.0f;
static constexpr float OpenTime = 7.0f;
static constexpr float JawEase = 0.3f;

//the slam happens once the small fish is SnapDepth inside the mouth. HoldTime outlasts the fade to black after being eaten
static constexpr float SnapDepth = 3.0f;
static constexpr float SlamTime = 0.18f;
static constexpr float HoldTime = 2.0f;
static constexpr float CatchFront = 0.6f;

//the mouth slowly opens and closes
static constexpr float BreatheClosed = 0.06f;
static constexpr float BreathePeriod = 4.0f;

//up and down drift, two sine waves added together so it doesn't look regular
static constexpr float BobHeight = 0.5f;
static constexpr float BobPeriod = 6.0f;
static constexpr float BobSecondPeriod = 2.3f;
static constexpr float BobSecondShare = 0.35f;

//how fast the mouth lines up with the small fish, and how close to the top and bottom of the corridor it can go
static constexpr float TrackSpeed = 3.0f;
static constexpr float ChargeTrackSpeed = 6.0f;
static constexpr float MouthMargin = 3.0f;

//how far off the left of the view its teeth start
static constexpr float OffView = 4.0f;

//its shape far behind the cave. FarBody is how far its body runs behind the png, then it fades out in FarTailSteps steps of FarTailStep long
static constexpr float FarSize = 80.0f;
static constexpr float FarAngle = -6.0f * Degrees;
static constexpr float FarBody = 20.0f;
static constexpr int FarTailSteps = 8;
static constexpr float FarTailStep = 2.0f;

//screen shake when the jaws slam, and the tremble that builds during a charge
static constexpr float ShakeSize = 0.5f;
static constexpr float ShakeTime = 0.5f;
static constexpr float ChargeShake = 0.12f;

//based on https://en.wikipedia.org/wiki/Smoothstep
static float smooth(float u) {
	u = std::clamp(u, 0.0f, 1.0f);
	return u * u * (3.0f - 2.0f * u);
}

BigFish::BigFish() : upper_jaw(data_path("upper_jaw.png")), lower_jaw(data_path("lower_jaw.png")) {
}

void BigFish::reset(glm::vec2 const &start) {
	state = Waiting;
	position = start;
	base_y = start.y;
	prey = start;
	time = 0.0f;
	slammed = false;
	jaw = RestClosed;
	shake_left = 0.0f;
}

void BigFish::reset_for_world(glm::vec2 const &bounds_min, glm::vec2 const &bounds_max) {
	lowest_mouth = bounds_min.y + MouthMargin;
	highest_mouth = bounds_max.y - MouthMargin;
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

	float breathe = BreatheClosed * (0.5f - 0.5f * std::cos(time * 2.0f * float(M_PI) / BreathePeriod));
	float jaw_target = RestClosed + breathe;

	if (state == Normal) {
		position.x += chase_speed() * elapsed;
	} else if (state == Charging) {
		charge_time += elapsed;
		position.x += charge_speed() * elapsed;
		jaw_target = glm::mix(RestClosed + breathe, WidestClosed, smooth(charge_time / OpenTime));
		if (prey.x < teeth_x() - SnapDepth) {
			state = Slamming;
			timer = SlamTime + HoldTime;
			slam_from = jaw;
		}
	} else if (state == Slamming) {
		if (timer > HoldTime) position.x += charge_speed() * elapsed;
		float before = timer;
		timer -= elapsed;
		if (before > HoldTime && timer <= HoldTime) {
			slammed = true;
			shake_left = ShakeTime;
		}
		if (timer <= 0.0f) state = Normal;
	} else {
		timer -= elapsed;
		if (timer <= 0.0f) state = Normal;
	}

	if (state == Slamming) {
		float u = std::min(1.0f, (SlamTime + HoldTime - timer) / SlamTime);
		jaw = glm::mix(slam_from, 1.0f, u * u);
	} else {
		jaw += (jaw_target - jaw) * (1.0f - std::exp(-elapsed / JawEase));
	}
}

void BigFish::enter(float x) {
	state = Normal;
	position.x = x;
}

float BigFish::out_of_sight_x(float view_left) const {
	return view_left - OffView - MouthFront * size;
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
	return state == Normal && point.x - teeth_x() < ChargeRange;
}

bool BigFish::in_mouth(glm::vec2 const &point) const {
	return point.x < teeth_x() + CatchFront;
}

float BigFish::closed() const {
	return jaw;
}

float BigFish::chase_speed() const {
	return std::clamp(ChaseSlowest + ChaseGain * (prey.x - teeth_x() - ChargeRange), ChaseSlowest, ChaseFastest);
}

float BigFish::charge_speed() const {
	return chase_speed() + ChargeAccel * charge_time;
}

float BigFish::mouth_offset(float shut) const {
	float front = MouthFront * size;
	float top = MouthTop * size + front * std::tan(glm::mix(UpperOpenAngle, UpperShutAngle, shut));
	float bottom = -MouthBottom * size + front * std::tan(glm::mix(LowerOpenAngle, LowerShutAngle, shut));
	return 0.5f * (top + bottom);
}

float BigFish::art_front() const {
	return ArtFront * size;
}

float BigFish::teeth_x() const {
	return position.x + MouthFront * size;
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
	auto draw_jaw = [&](SpriteTexture const &jaw_texture, glm::vec2 const &offset, float angle) {
		float co = std::cos(angle);
		float si = std::sin(angle);
		glm::vec2 center = position + glm::vec2(co * offset.x - si * offset.y, si * offset.x + co * offset.y);
		sprites.draw(jaw_texture, world_to_clip, center, glm::vec2(size, h), angle, false, glm::u8vec4(255), 0.0f, DrawSprites::Predator);
	};
	//upper jaw last so it covers the lower jaw where they overlap
	draw_jaw(lower_jaw, glm::vec2(0.5f * size, -0.5f * h + LowerLift * size), glm::mix(LowerOpenAngle, LowerShutAngle, c));
	draw_jaw(upper_jaw, glm::vec2(0.5f * size, 0.5f * h), glm::mix(UpperOpenAngle, UpperShutAngle, c));
}

//turns an offset from the hinge by FarAngle, then mirrors it so the head faces left.
//based on https://en.wikipedia.org/wiki/Rotation_matrix
static glm::vec2 far_offset(glm::vec2 const &offset) {
	float co = std::cos(FarAngle);
	float si = std::sin(FarAngle);
	return glm::vec2(-(co * offset.x - si * offset.y), si * offset.x + co * offset.y);
}

glm::vec2 BigFish::far_mouth(glm::vec2 const &hinge) const {
	return hinge + far_offset(glm::vec2(MouthFront * FarSize, 0.0f));
}

void BigFish::draw_far(DrawSprites const &sprites, glm::mat4 const &world_to_clip, glm::vec2 const &hinge) const {
	float h = FarSize * float(upper_jaw.pixel_size.y) / float(upper_jaw.pixel_size.x);
	constexpr glm::u8vec4 tint = glm::u8vec4(0, 0, 0, 255);
	//the body behind the head is the png's last column of pixels, stretched
	constexpr glm::vec4 LastColumn = glm::vec4(0.001f, 0.0f, 0.001f, 1.0f);
	auto draw_part = [&](SpriteTexture const &texture, glm::vec2 const &offset) {
		sprites.draw(texture, world_to_clip, hinge + far_offset(offset), glm::vec2(FarSize, h), -FarAngle, true, tint);
		sprites.draw(texture, world_to_clip, hinge + far_offset(glm::vec2(-0.5f * FarBody, offset.y)), glm::vec2(FarBody, h), -FarAngle, true, tint,
			0.0f, DrawSprites::Surface, LastColumn);
		for (int i = 0; i < FarTailSteps; ++i) {
			float back = FarBody + (float(i) + 0.5f) * FarTailStep;
			float solid = 1.0f - (float(i) + 1.0f) / float(FarTailSteps + 1);
			sprites.draw(texture, world_to_clip, hinge + far_offset(glm::vec2(-back, offset.y)), glm::vec2(FarTailStep, h), -FarAngle, true,
				glm::u8vec4(0, 0, 0, uint8_t(255.0f * solid)), 0.0f, DrawSprites::Surface, LastColumn);
		}
	};
	draw_part(lower_jaw, glm::vec2(0.5f * FarSize, -0.5f * h + LowerLift * FarSize));
	draw_part(upper_jaw, glm::vec2(0.5f * FarSize, 0.5f * h));
}
