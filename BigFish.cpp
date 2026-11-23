#include "BigFish.hpp"

#include "data_path.hpp"
#include "Load.hpp"

#include <algorithm>
#include <cmath>

//align the jaw artwork and collision checks using measurements from the pngs
//raise the lower jaw so both pieces meet at the back of the head
static constexpr float LowerLift = 0.045f;
static constexpr float MouthFront = 0.6f;
static constexpr float MouthTop = 0.19f;
static constexpr float MouthBottom = 0.07f;
static constexpr float ArtFront = 0.625f;

//counterclockwise turn of each jaw with the mouth open and shut
static constexpr float Degrees = float(M_PI) / 180.0f;
static constexpr float UpperOpenAngle = 8.0f * Degrees;
static constexpr float UpperShutAngle = -11.0f * Degrees;
static constexpr float LowerOpenAngle = -20.0f * Degrees;
static constexpr float LowerShutAngle = 10.0f * Degrees;

//turn the whole head with enough inertia to avoid snapping after the player
static constexpr float TurnLimit = 25.0f * Degrees;
static constexpr float TurnSpeed = 4.0f * Degrees;
static constexpr float TurnAccel = 6.0f * Degrees;
static constexpr float TurnResponse = 1.5f;
//hold the heading while the player has room between the jaws
static constexpr float TurnSlack = 4.0f;

//steady approach speed before charging
static constexpr float SwimSpeed = 0.9f;

//start charging when the player gets close to the teeth
static constexpr float ChargeRange = 6.0f;
static constexpr float ChargeAccel = 0.75f;
static constexpr float CreepTime = 2.0f;

//the jaws open wider until OpenTime seconds into the charge.
//for the closed values, 0 is as wide as they go and 1 is shut
static constexpr float RestClosed = 0.4f;
static constexpr float WidestClosed = 0.35f;
static constexpr float OpenTime = 7.0f;
static constexpr float JawEase = 0.3f;

//snap shut once the player is inside the mouth, then hold through the death fade
static constexpr float SnapDepth = 3.0f;
static constexpr float SlamTime = 0.18f;
static constexpr float HoldTime = 2.0f;
static constexpr float CatchFront = 0.6f;
//the opening near the hinge, measured against the jaw artwork
static constexpr float PassageX = 0.08f;
static constexpr float PassageY = -0.008f;

//the mouth slowly opens and closes
static constexpr float BreatheClosed = 0.02f;
static constexpr float BreathePeriod = 4.0f;

//slow vertical drift while swimming
static constexpr float BobHeight = 0.2f;
static constexpr float BobPeriod = 12.0f;
static constexpr float BobSecondPeriod = 7.0f;
static constexpr float BobSecondShare = 0.35f;

//how far off the left of the view its head starts
static constexpr float OffView = 4.0f;

//the distant body stays solid behind the head, then fades into the water
static constexpr float FarSize = 80.0f;
static constexpr float FarAngle = -6.0f * Degrees;
static constexpr float FarBody = 20.0f;
static constexpr float FarTailLength = 16.0f;

//screen shake when the jaws slam, and the tremble that builds during a charge
static constexpr float ShakeSize = 0.5f;
static constexpr float ShakeTime = 0.5f;
static constexpr float ChargeShake = 0.12f;

//based on https://en.wikipedia.org/wiki/Smoothstep
static float smooth(float u) {
	u = std::clamp(u, 0.0f, 1.0f);
	return u * u * (3.0f - 2.0f * u);
}

static Load<Sound::Sample> fish_roar_near(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_big_fish_roar1.opus"));
});

static Load<Sound::Sample> presence_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("big_fish_presence.opus"));
});

static Load<Sound::Sample> charge_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_big_fish_roar_charge.opus"));
});

std::shared_ptr<Sound::PlayingSample> BigFish::play_presence(glm::vec3 const &mouth) {
	return Sound::loop_3D(*presence_sound, 1.0f, mouth, presence_reach);
}

std::shared_ptr<Sound::PlayingSample> BigFish::play_charge_roar(glm::vec3 const &mouth) {
	//grow louder as the fish speeds up
	const std::shared_ptr<Sound::PlayingSample> roar = Sound::play_3D(*charge_sound, charge_start_volume, mouth, arrival_reach);
	roar->set_volume(arrival_volume, charge_fade_in);
	return roar;
}

Sound::Sample const &BigFish::arrival_sound() {
	return *fish_roar_near;
}

BigFish::BigFish() : upper_jaw(data_path("upper_jaw.png")), lower_jaw(data_path("lower_jaw.png")), passage_light(glm::u8vec4(255)) {
}

void BigFish::reset(glm::vec2 const &start) {
	state = Waiting;
	direction = 1.0f;
	swim_speed_scale = 1.0f;
	position = start;
	base_y = start.y;
	prey = start;
	time = 0.0f;
	slammed = false;
	jaw = RestClosed;
	shake_left = 0.0f;
	angle = 0.0f;
	turn_speed = 0.0f;
}

void BigFish::reset_for_world(glm::vec2 const &bounds_min, glm::vec2 const &bounds_max) {
	float middle = 0.5f * (bounds_min.y + bounds_max.y);
	reset(glm::vec2(bounds_min.x - art_front(), middle - mouth_offset(RestClosed)));
}

void BigFish::update(float elapsed) {
	slammed = false;
	time += elapsed;
	shake_left = std::max(0.0f, shake_left - elapsed);
	if (state == Waiting) return;
	if (state == Held) {
		timer += elapsed;
		float settle = 1.0f - std::exp(-1.5f * elapsed);
		angle = glm::mix(angle, 0.0f, settle);
		position.y = glm::mix(position.y, base_y, settle);
		jaw += (WidestClosed - jaw) * (1.0f - std::exp(-elapsed / JawEase));
		position.x += held_mouth_x - mouth_position().x;
		return;
	}

	position.y = base_y + BobHeight * ((1.0f - BobSecondShare) * std::sin(time * 2.0f * float(M_PI) / BobPeriod)
		+ BobSecondShare * std::sin(time * 2.0f * float(M_PI) / BobSecondPeriod + 1.0f));

	float breathe = BreatheClosed * (0.5f - 0.5f * std::cos(time * 2.0f * float(M_PI) / BreathePeriod));
	float jaw_target = RestClosed + breathe;
	if (swimming()) update_turn(elapsed);

	if (state == Normal) {
		position.x += direction * swim_speed_scale * SwimSpeed * elapsed;
	} else if (state == Charging) {
		charge_time += elapsed;
		position.x += direction * swim_speed_scale * charge_speed() * elapsed;
		jaw_target = glm::mix(RestClosed + breathe, WidestClosed, smooth(charge_time / OpenTime));
		if (local_point(prey).x < MouthFront * size - SnapDepth) {
			state = Slamming;
			timer = SlamTime + HoldTime;
			slam_from = jaw;
		}
	} else if (state == Slamming) {
		if (timer > HoldTime) {
			position.x += direction * swim_speed_scale * charge_speed() * elapsed;
		}
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

void BigFish::enter_from_right(float view_right, float mouth_y) {
	//start with the jaws just outside the view
	reset(glm::vec2(view_right, mouth_y - mouth_offset(RestClosed)));
	direction = -1.0f;
	swim_speed_scale = finale_speed_scale;
	enter(view_right + OffView + art_front());
}

void BigFish::update_turn(float elapsed) {
	glm::vec2 delta = prey - position;
	delta.x *= direction;
	float mouth_angle = std::atan2(mouth_offset(jaw), MouthFront * size);
	float target = std::clamp(std::atan2(delta.y, std::max(1.0f, delta.x)) - mouth_angle, -TurnLimit, TurnLimit);
	float difference = target - angle;
	float slack = std::atan2(TurnSlack, std::max(MouthFront * size, delta.x));
	difference = std::copysign(std::max(0.0f, std::abs(difference) - slack), difference);
	target = angle + difference;
	float speed = std::clamp(TurnResponse * difference, -TurnSpeed, TurnSpeed);
	turn_speed += std::clamp(speed - turn_speed, -TurnAccel * elapsed, TurnAccel * elapsed);
	float step = turn_speed * elapsed;
	if (step * difference > 0.0f && std::abs(step) > std::abs(difference)) {
		angle = target;
		turn_speed = 0.0f;
	} else {
		angle = std::clamp(angle + step, -TurnLimit, TurnLimit);
	}
}

glm::vec2 BigFish::local_point(glm::vec2 const &point) const {
	glm::vec2 offset = point - position;
	offset.x *= direction;
	float co = std::cos(angle);
	float si = std::sin(angle);
	return glm::vec2(co * offset.x + si * offset.y, -si * offset.x + co * offset.y);
}

float BigFish::out_of_sight_x(float view_left) const {
	return view_left - OffView - art_front();
}

void BigFish::stun(float seconds) {
	if (state == Waiting) return;
	state = Stunned;
	timer = seconds;
	turn_speed = 0.0f;
}

void BigFish::charge() {
	if (state != Normal) return;
	state = Charging;
	charge_time = 0.0f;
}

bool BigFish::swimming() const {
	return state == Normal || state == Charging;
}

void BigFish::hold_open() {
	held_mouth_x = mouth_position().x;
	base_y = position.y;
	timer = 0.0f;
	state = Held;
	turn_speed = 0.0f;
}

void BigFish::hold_open(float passage_y) {
	hold_open();
	base_y = passage_y - PassageY * size;
}

bool BigFish::in_reach(glm::vec2 const &point) const {
	return state == Normal && local_point(point).x - MouthFront * size < ChargeRange;
}

bool BigFish::in_mouth(glm::vec2 const &point) const {
	glm::vec2 local = local_point(point);
	glm::vec2 edges = mouth_edges(state == Slamming ? slam_from : jaw);
	return local.x < MouthFront * size + CatchFront && local.y >= edges.y && local.y <= edges.x;
}

glm::vec2 BigFish::escape_target() const {
	glm::vec2 local(PassageX * size, PassageY * size);
	float co = std::cos(angle), si = std::sin(angle);
	return position + glm::vec2(direction * (co * local.x - si * local.y), si * local.x + co * local.y);
}

float BigFish::closed() const {
	return jaw;
}

float BigFish::charge_speed() const {
	return SwimSpeed + ChargeAccel * std::max(0.0f, charge_time - CreepTime);
}

glm::vec2 BigFish::mouth_edges(float shut) const {
	float front = MouthFront * size;
	float top = MouthTop * size + front * std::tan(glm::mix(UpperOpenAngle, UpperShutAngle, shut));
	float bottom = -MouthBottom * size + front * std::tan(glm::mix(LowerOpenAngle, LowerShutAngle, shut));
	return glm::vec2(top, bottom);
}

float BigFish::mouth_offset(float shut) const {
	glm::vec2 edges = mouth_edges(shut);
	return 0.5f * (edges.x + edges.y);
}

float BigFish::art_front() const {
	return ArtFront * size;
}

float BigFish::teeth_x() const {
	return mouth_position().x;
}

glm::vec2 BigFish::mouth_position() const {
	float offset = mouth_offset(state == Slamming ? slam_from : jaw);
	float front = MouthFront * size;
	float co = std::cos(angle);
	float si = std::sin(angle);
	return position + glm::vec2(direction * (co * front - si * offset), si * front + co * offset);
}

glm::vec2 BigFish::tentacle_point(bool upper, float along) const {
	float x = along * MouthFront * size;
	float jaw_angle = upper ? glm::mix(UpperOpenAngle, UpperShutAngle, jaw) : glm::mix(LowerOpenAngle, LowerShutAngle, jaw);
	float y = (upper ? MouthTop : -MouthBottom) * size + x * std::tan(jaw_angle);
	float co = std::cos(angle), si = std::sin(angle);
	return position + glm::vec2(direction * (co * x - si * y), si * x + co * y);
}

glm::vec2 BigFish::shake() const {
	float amount = ShakeSize * shake_left / ShakeTime;
	if (state == Charging) amount += ChargeShake * smooth(charge_time / OpenTime);
	//shake the camera during a bite or charge
	return amount * glm::vec2(std::sin(time * 73.0f), std::cos(time * 59.0f));
}

void BigFish::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	if (state == Waiting) return;

	float h = size * float(upper_jaw.pixel_size.y) / float(upper_jaw.pixel_size.x);
	float c = closed();

	//rotate each jaw around the back of the mouth
	//based on https://en.wikipedia.org/wiki/Rotation_matrix
	auto draw_jaw = [&](SpriteTexture const &jaw_texture, glm::vec2 const &offset, float jaw_angle) {
		jaw_angle += angle;
		float co = std::cos(jaw_angle);
		float si = std::sin(jaw_angle);
		glm::vec2 center = position + glm::vec2(direction * (co * offset.x - si * offset.y), si * offset.x + co * offset.y);
		sprites.draw(jaw_texture, world_to_clip, center, glm::vec2(size, h), direction * jaw_angle, direction < 0.0f, glm::u8vec4(255), 0.0f, state == Held ? DrawSprites::HeldPredator : DrawSprites::Predator);
	};
	if (state == Held) {
		uint8_t light = uint8_t(190.0f * std::min(timer / 2.0f, 1.0f));
		//feather the light through the opening behind the jaws
		sprites.draw(passage_light, world_to_clip, escape_target(), glm::vec2(0.24f * size, 0.10f * size),
			direction * angle, false, glm::u8vec4(230, 230, 230, light), 0.0f, DrawSprites::SoftLight);
	}
	//upper jaw last so it covers the lower jaw where they overlap
	draw_jaw(lower_jaw, glm::vec2(0.5f * size, -0.5f * h + LowerLift * size), glm::mix(LowerOpenAngle, LowerShutAngle, c));
	draw_jaw(upper_jaw, glm::vec2(0.5f * size, 0.5f * h), glm::mix(UpperOpenAngle, UpperShutAngle, c));
}

//position the distant fish facing left
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
	//extend the back of the head into a body using the edge of the artwork
	constexpr glm::vec4 LastColumn = glm::vec4(0.001f, 0.0f, 0.001f, 1.0f);
	auto draw_part = [&](SpriteTexture const &texture, glm::vec2 const &offset) {
		sprites.draw(texture, world_to_clip, hinge + far_offset(offset), glm::vec2(FarSize, h), -FarAngle, true, tint, 0.0f, DrawSprites::Surface);
		sprites.draw(texture, world_to_clip, hinge + far_offset(glm::vec2(-0.5f * FarBody, offset.y)), glm::vec2(FarBody, h), -FarAngle, true, tint,
			0.0f, DrawSprites::Surface, LastColumn);
		float back = FarBody + 0.5f * FarTailLength;
		sprites.draw(texture, world_to_clip, hinge + far_offset(glm::vec2(-back, offset.y)), glm::vec2(FarTailLength, h), -FarAngle, true,
			tint, 0.0f, DrawSprites::FarTail, LastColumn);
	};
	draw_part(lower_jaw, glm::vec2(0.5f * FarSize, -0.5f * h + LowerLift * FarSize));
	draw_part(upper_jaw, glm::vec2(0.5f * FarSize, 0.5f * h));
}
