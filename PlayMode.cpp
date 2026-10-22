#include "PlayMode.hpp"

#include "data_path.hpp"
#include "gl_errors.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

PlayMode::PlayMode()
	: fish_texture(data_path("small-fish.png")), stone_texture(data_path("stone.png")) {
	physics.bodies = {
		{.position = {-8.0f, 0.0f}, .radius = 1.7f, .drag = 1.8f, .current_scale = 0.0f},
		{.position = {-1.5f, 1.8f}, .radius = 0.64f},
		{.position = {5.0f, -1.5f}, .inverse_mass = 0.625f, .radius = 0.8f},
		{.position = {10.0f, 3.4f}, .inverse_mass = 1.25f, .radius = 0.576f}
	};
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
	glm::vec2 move(float(input.right) - float(input.left), float(input.up) - float(input.down));
	if (move != glm::vec2(0.0f)) {
		move = glm::normalize(move);
		// facing follows intent even while momentum carries the fish the other way
		facing = move;
	}
	// retain fractional time so acceleration and suction use the same steps at every frame rate
	accumulated_time += std::min(elapsed, 0.1f);
	while (accumulated_time >= UnderwaterPhysics::fixed_step) {
		auto &fish = physics.bodies[fish_body];
		// capture at the mouth before solid contacts push the target away
		update_interaction();
		fish.add_force(move * swim_force);
		physics.step();
		float speed = glm::length(fish.velocity);
		if (fish.active && speed > max_swim_speed) fish.velocity *= max_swim_speed / speed;
		accumulated_time -= UnderwaterPhysics::fixed_step;
	}
}

void PlayMode::update_interaction() {
	// consume the press once so later substeps cannot reuse it after a capture
	bool pressed = input.space_pressed;
	input.space_pressed = false;
	auto const &fish = physics.bodies[fish_body];
	if (!fish.active) return;
	if (state == FishState::Fat) {
		// holding space after capture keeps the cargo until a fresh press
		if (pressed) spit();
		return;
	}
	// a spit blocks suction until space is released, even though the fish is empty
	state = input.space && !input.suppress_suction ? FishState::Kirby : FishState::Normal;
	if (state != FishState::Kirby) return;

	glm::vec2 mouth = fish.position + facing * mouth_offset;
	float cone_cos = std::cos(glm::radians(suction_half_angle));
	float nearest_distance = suction_range;
	std::optional<size_t> target;
	// shrinking the search distance selects one nearest movable body inside the cone
	for (size_t i = 0; i < physics.bodies.size(); ++i) {
		auto const &body = physics.bodies[i];
		if (i == fish_body || !body.active || body.inverse_mass <= 0.0f) continue;
		glm::vec2 offset = body.position - mouth;
		float distance = glm::length(offset);
		// facing is unit length, so dot(offset, facing) equals distance times cos(angle)
		// comparing cosines rejects targets outside the cone without computing the angle
		if (distance > nearest_distance || glm::dot(offset, facing) < distance * cone_cos) continue;
		nearest_distance = distance;
		target = i;
	}
	if (!target) return;
	auto &body = physics.bodies[*target];
	if (nearest_distance <= body.radius + capture_distance) {
		// disable rather than erase so spitting restores the same body and its properties
		body.active = false;
		body.velocity = glm::vec2(0.0f);
		body.force = glm::vec2(0.0f);
		carried_body = target;
		state = FishState::Fat;
	} else {
		// divide by distance to normalize direction and keep force strength independent of range
		body.add_force((mouth - body.position) * (suction_force / nearest_distance));
	}
}

void PlayMode::spit() {
	if (!carried_body) return;
	auto const &fish = physics.bodies[fish_body];
	auto &body = physics.bodies[*carried_body];
	// both radii provide launch separation before the spawn is constrained to world bounds
	body.position = fish.position + facing * (fish.radius + body.radius + spit_clearance);
	body.position = glm::clamp(body.position, physics.bounds_min + body.radius, physics.bounds_max - body.radius);
	body.velocity = fish.velocity;
	body.force = glm::vec2(0.0f);
	body.active = true;
	body.apply_impulse(facing * spit_impulse);
	carried_body.reset();
	state = FishState::Normal;
	// the press that launches an object cannot also start pulling it back
	input.suppress_suction = input.space;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	if (drawable_size.x == 0 || drawable_size.y == 0) return;

	// fit the same 32 x 18 world into any window, with centered letterboxing
	glViewport(0, 0, drawable_size.x, drawable_size.y);
	glClearColor(0.015f, 0.025f, 0.03f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	float scale = std::min(float(drawable_size.x) / 32.0f, float(drawable_size.y) / 18.0f);
	GLsizei width = std::max(1, int(32.0f * scale));
	GLsizei height = std::max(1, int(18.0f * scale));
	glViewport((int(drawable_size.x) - width) / 2, (int(drawable_size.y) - height) / 2, width, height);
	glm::mat4 world_to_clip = glm::ortho(-16.0f, 16.0f, -9.0f, 9.0f);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE); // a horizontal flip reverses the quad winding
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// use aspect ratio
	auto sprite_size = [](SpriteTexture const &texture, float height) {
		return glm::vec2(height * float(texture.pixel_size.x) / float(texture.pixel_size.y), height);
	};
	float const stone_heights[] = {2.0f, 2.5f, 1.8f};
	for (size_t i = 0; i < 3; ++i) {
		auto const &body = physics.bodies[i + 1];
		if (body.active) sprites.draw(stone_texture, world_to_clip, body.position, sprite_size(stone_texture, stone_heights[i]));
	}
	auto const &fish = physics.bodies[fish_body];
	// switch the mirror direction so swimming left keeps the belly down
	bool flip_x = facing.x >= 0.0f;
	glm::vec2 rotation_direction = flip_x ? facing : -facing;
	float angle = std::atan2(rotation_direction.y, rotation_direction.x);
	glm::vec2 fish_size = sprite_size(fish_texture, 4.0f);
	if (state == FishState::Kirby) fish_size *= glm::vec2(1.05f, 1.15f);
	if (state == FishState::Fat) fish_size *= glm::vec2(1.1f, 1.45f);
	if (fish.active) sprites.draw(fish_texture, world_to_clip, fish.position, fish_size, angle, flip_x);

	glDisable(GL_BLEND);
	glViewport(0, 0, drawable_size.x, drawable_size.y);
	GL_ERRORS();
}
