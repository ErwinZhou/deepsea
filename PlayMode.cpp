#include "PlayMode.hpp"

#include "data_path.hpp"
#include "DrawLines.hpp"
#include "gl_errors.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

PlayMode::PlayMode()
	: cave_texture(data_path("cave_rock.png")),
	  fish_texture(data_path("normal.png")),
	  kirby_texture(data_path("kirby.png")),
	  fat_texture(data_path("fat.png")),
	  stone_texture(data_path("small_rock.png")) {
	physics.bounds_min = glm::vec2(-0.5f * level_length, -0.5f * corridor_height);
	physics.bounds_max = glm::vec2(0.5f * level_length, 0.5f * corridor_height);
	camera.center = camera.constrain(physics.bounds_min, physics.bounds_min, physics.bounds_max);
	physics.bodies = {
		{.position = {4.0f, 0.0f}, .radius = fish_radius, .drag = 1.8f, .current_scale = 0.0f, .kind = Body2D::Fish},
		{.position = {-1.5f, 1.8f}, .radius = 0.35f},
		{.position = {5.0f, -1.5f}, .inverse_mass = 0.625f, .radius = 0.43f},
		{.position = {10.0f, 3.4f}, .inverse_mass = 1.25f, .radius = 0.29f}
	};
	for (auto &body : physics.bodies) body.position += camera.center;
	cave.setup(physics);
	big_fish.reset_for_world(physics.bounds_min, physics.bounds_max);
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &) {
	if (evt.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
		input = {};
		if (state == FishState::Kirby) state = FishState::Normal;
		return true;
	}
	if (evt.type != SDL_EVENT_KEY_DOWN && evt.type != SDL_EVENT_KEY_UP) return false;
	if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.repeat) return false;
	bool pressed = evt.type == SDL_EVENT_KEY_DOWN;
	switch (evt.key.key) {
	case SDLK_W: input.up = pressed; break;
	case SDLK_S: input.down = pressed; break;
	case SDLK_A: input.left = pressed; break;
	case SDLK_D: input.right = pressed; break;
	case SDLK_SPACE:
		if (pressed && !input.space) input.space_pressed = true;
		input.space = pressed;
		if (!pressed) input.suppress_suction = false;
		break;
	case SDLK_ESCAPE:
		if (pressed) Mode::set_current(nullptr);
		break;
	default: return false;
	}
	return true;
}

void PlayMode::update(float elapsed) {
	if (!std::isfinite(elapsed) || elapsed <= 0.0f) return;
	// hit stop, based on https://uhiyama-lab.com/en/notes/unity/unity-game-feel-hit-feedback/
	if (hit_stop > 0.0f) {
		hit_stop -= elapsed;
		return;
	}
	glm::vec2 move(float(input.right) - float(input.left), float(input.up) - float(input.down));
	if (move != glm::vec2(0.0f)) {
		move = glm::normalize(move);
		// face the way the keys point, even while still drifting the other way
		facing = move;
	}
	clock += elapsed;
	rumble = std::max(0.0f, rumble - elapsed);
	cave.update(physics, big_fish, elapsed);
	{ // freeze briefly when a spat rock lands, longer when something breaks
		constexpr float HitStop = 0.06f;
		constexpr float BreakStop = 0.12f;
		constexpr float CrashRumble = 0.6f;
		if (cave.events.hit) hit_stop = HitStop;
		if (cave.events.broke) hit_stop = BreakStop;
		if (cave.events.crashed) rumble = CrashRumble;
	}

	// step physics at a fixed rate so it plays the same at any frame rate
	accumulated_time += std::min(elapsed, 0.1f);
	while (accumulated_time >= UnderwaterPhysics::fixed_step) {
		auto &fish = physics.bodies[fish_body];
		// suck first, so a rock at the mouth isn't bumped away by the collision
		update_interaction();
		fish.add_force(move * swim_force);
		physics.step();
		float speed = glm::length(fish.velocity);
		if (fish.active && speed > max_swim_speed) fish.velocity *= max_swim_speed / speed;
		accumulated_time -= UnderwaterPhysics::fixed_step;
	}

	camera.update(physics.bodies[fish_body].position, elapsed, physics.bounds_min, physics.bounds_max);
	float view_left = camera.center.x - 0.5f * camera.view_size.x;

	big_fish.update(elapsed);
	{ // breaking the first wall wakes the big fish
		if (cave.events.first_wall_broke) warning_time = 0.0f;
		if (big_fish.state == BigFish::Waiting && warning_time >= 0.0f) {
			warning_time += elapsed;
			cave.drop_ceiling_dust(view_left, view_left + camera.view_size.x, warning_amount(), elapsed);
			if (warning_time >= warning_length) big_fish.enter(view_left);
		}
	}
	{ // it can't fall further behind than just off the left of the view, unless the cave held it up
		float leash_x = view_left - big_fish.art_front();
		if (big_fish.state == BigFish::Stunned) leashed = false;
		if (big_fish.state == BigFish::Normal && !leashed && big_fish.position.x >= leash_x) leashed = true;
		if (big_fish.state == BigFish::Normal && leashed) big_fish.position.x = std::max(big_fish.position.x, leash_x);
	}

	{ // once the teeth reach the small fish it's in the mouth. still being there when the jaws slam is death
		auto &fish = physics.bodies[fish_body];
		if (fish.active && !escaped && big_fish.state != BigFish::Waiting && fish.position.x < big_fish.teeth_x()) {
			if (big_fish.state == BigFish::Normal) big_fish.bite();
			// dead for now. later this could send you into the big fish's stomach level
			if (big_fish.slammed) fish.active = false;
		}
	}

	if (physics.bodies[fish_body].active && physics.bodies[fish_body].position.x > cave.exit_x) escaped = true;
	if (!physics.bodies[fish_body].active || escaped) end_time += elapsed;

	// start over a moment after being eaten. escaping ends the game, so it never starts over
	constexpr float RestartDelay = 2.5f;
	if (!physics.bodies[fish_body].active && end_time > RestartDelay) {
		// keep this mode alive until update returns, since replacing it frees it.
		// based on https://en.cppreference.com/w/cpp/memory/enable_shared_from_this
		std::shared_ptr< Mode > keep = shared_from_this();
		Mode::set_current(std::make_shared< PlayMode >());
	}
}

float PlayMode::warning_amount() const {
	if (warning_time < 0.0f) return 0.0f;
	return std::clamp((warning_time - warning_quiet) / (warning_length - warning_quiet), 0.0f, 1.0f);
}

void PlayMode::update_interaction() {
	// use up the press so the next physics step can't act on it again
	bool pressed = input.space_pressed;
	input.space_pressed = false;
	auto const &fish = physics.bodies[fish_body];
	if (!fish.active) return;
	if (state == FishState::Fat) {
		// spit only on a new press of space
		if (pressed) spit();
		return;
	}
	// after a spit, don't suck again until space is let go
	state = input.space && !input.suppress_suction ? FishState::Kirby : FishState::Normal;
	if (state != FishState::Kirby) return;

	glm::vec2 mouth = fish.position + facing * mouth_offset;
	float cone_cos = std::cos(glm::radians(suction_half_angle));
	float nearest_distance = suction_range;
	std::optional<size_t> target;
	// find the closest movable body in the cone in front of the mouth
	for (size_t i = 0; i < physics.bodies.size(); ++i) {
		auto const &body = physics.bodies[i];
		if (i == fish_body || !body.active || body.inverse_mass <= 0.0f) continue;
		glm::vec2 offset = body.position - mouth;
		float distance = glm::length(offset);
		// dot(offset, facing) is distance times the cosine of the angle to the body
		bool in_cone = glm::dot(offset, facing) >= distance * cone_cos;
		if (distance > nearest_distance || (!in_cone && distance > suction_any_side)) continue;
		nearest_distance = distance;
		target = i;
	}
	if (!target) return;
	auto &body = physics.bodies[*target];
	if (nearest_distance <= body.radius + capture_distance) {
		// switch it off but keep it in the list, so spitting brings back the same rock
		body.active = false;
		body.velocity = glm::vec2(0.0f);
		body.force = glm::vec2(0.0f);
		carried_body = target;
		state = FishState::Fat;
	} else {
		// pull with the same strength at any distance
		body.add_force((mouth - body.position) * (suction_force / nearest_distance));
	}
}

void PlayMode::spit() {
	if (!carried_body) return;
	auto const &fish = physics.bodies[fish_body];
	auto &body = physics.bodies[*carried_body];
	// start the rock just past the fish's mouth, kept inside the level
	body.position = fish.position + facing * (fish.radius + body.radius + spit_clearance);
	body.position = glm::clamp(body.position, physics.bounds_min + body.radius, physics.bounds_max - body.radius);
	body.velocity = fish.velocity;
	body.force = glm::vec2(0.0f);
	body.active = true;
	body.apply_impulse(facing * spit_impulse);
	carried_body.reset();
	state = FishState::Normal;
	// so holding space after a spit doesn't suck the rock straight back
	input.suppress_suction = input.space;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	if (drawable_size.x == 0 || drawable_size.y == 0) return;

	// same view of the world whatever shape the window is
	glViewport(0, 0, drawable_size.x, drawable_size.y);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	float scale = std::min(float(drawable_size.x) / camera.view_size.x, float(drawable_size.y) / camera.view_size.y);
	GLsizei width = std::max(1, int(camera.view_size.x * scale));
	GLsizei height = std::max(1, int(camera.view_size.y * scale));
	glViewport((int(drawable_size.x) - width) / 2, (int(drawable_size.y) - height) / 2, width, height);
	glm::vec2 half_view = 0.5f * camera.view_size;
	glm::vec2 view_center = camera.center - big_fish.shake();
	{ // a rumble when the hanging rock comes down, and a tremble that grows while the big fish is on its way
		constexpr float RumbleSize = 0.25f;
		// the rumble eases off over its last moments
		constexpr float RumbleFade = 0.3f;
		constexpr float TrembleSize = 0.08f;
		float amount = RumbleSize * std::min(1.0f, rumble / RumbleFade);
		if (big_fish.state == BigFish::Waiting) amount += TrembleSize * warning_amount();
		view_center += amount * glm::vec2(std::sin(clock * 67.0f), std::cos(clock * 53.0f));
	}
	glm::mat4 world_to_clip = glm::ortho(view_center.x - half_view.x, view_center.x + half_view.x,
		view_center.y - half_view.y, view_center.y + half_view.y);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE); // a horizontal flip reverses the quad winding
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	{ // cave ceiling and floor, wide and tall enough to cover the view past both ends of the level
		glm::vec2 size = glm::vec2(level_length, 0.0f) + camera.view_size;
		float middle_x = 0.5f * (physics.bounds_min.x + physics.bounds_max.x);
		sprites.draw(cave_texture, world_to_clip, glm::vec2(middle_x, physics.bounds_max.y + 0.5f * size.y), size);
		sprites.draw(cave_texture, world_to_clip, glm::vec2(middle_x, physics.bounds_min.y - 0.5f * size.y), size);
	}

	// size from a height, keeping the png's shape
	auto sprite_size = [](SpriteTexture const &texture, float height) {
		return glm::vec2(height * float(texture.pixel_size.x) / float(texture.pixel_size.y), height);
	};
	cave.draw(sprites, world_to_clip);

	// the rock art fills about 83 percent of its png, so this makes the drawn rock match its radius
	constexpr float RockArtFill = 0.83f;
	for (auto const &body : physics.bodies) {
		if (body.active && body.kind == Body2D::Rock) {
			sprites.draw(stone_texture, world_to_clip, body.position, sprite_size(stone_texture, 2.0f * body.radius / RockArtFill));
		}
	}
	auto const &fish = physics.bodies[fish_body];
	// the fish png faces right, so flip it when swimming left
	bool flip_x = facing.x < 0.0f;
	glm::vec2 rotation_direction = flip_x ? -facing : facing;
	float angle = std::atan2(rotation_direction.y, rotation_direction.x);
	SpriteTexture const &current_fish_texture = state == FishState::Fat ? fat_texture
		: state == FishState::Kirby ? kirby_texture : fish_texture;
	glm::vec2 fish_size = sprite_size(current_fish_texture, fish_height);
	if (fish.active) sprites.draw(current_fish_texture, world_to_clip, fish.position, fish_size, angle, flip_x);
	// drawn over the small fish so it disappears behind the jaws
	big_fish.draw(sprites, world_to_clip);
	cave.draw_over_big_fish(sprites, world_to_clip);

	if (escaped) { // fade to black once the small fish gets away, then say so in the middle of the screen
		constexpr float FadeTime = 2.0f;
		constexpr float TextFadeTime = 1.0f;
		constexpr float TextHeight = 1.5f;
		float fade = std::min(1.0f, end_time / FadeTime);
		float text_fade = std::clamp((end_time - FadeTime) / TextFadeTime, 0.0f, 1.0f);
		// the cave png tinted black covers the whole view
		sprites.draw(cave_texture, world_to_clip, view_center, camera.view_size, 0.0f, false, glm::u8vec4(0x00, 0x00, 0x00, uint8_t(255.0f * fade)));

		DrawLines lines(world_to_clip);
		glm::vec3 text_end;
		lines.draw_text("You Escaped", glm::vec3(0.0f), glm::vec3(TextHeight, 0.0f, 0.0f), glm::vec3(0.0f, TextHeight, 0.0f),
			glm::u8vec4(0xff, 0xff, 0xff, uint8_t(255.0f * text_fade)), &text_end);
		// it was drawn from 0, so move it back by half its size to center it on the view
		glm::vec3 shift = glm::vec3(view_center - 0.5f * glm::vec2(text_end.x, TextHeight), 0.0f);
		for (DrawLines::Vertex &vertex : lines.attribs) vertex.Position += shift;
	}

	glDisable(GL_BLEND);
	glViewport(0, 0, drawable_size.x, drawable_size.y);
	GL_ERRORS();
}
