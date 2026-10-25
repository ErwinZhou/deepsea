#include "PlayMode.hpp"

#include "data_path.hpp"
#include "gl_errors.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

PlayMode::PlayMode()
	: background_texture(data_path("background.png")),
	  fish_texture(data_path("normal.png")),
	  kirby_texture(data_path("kirby.png")),
	  fat_texture(data_path("fat.png")),
	  stone_texture(data_path("stone.png")) {
	// two view heights reveal roughly a quarter of the background at a time
	background_size = glm::vec2(background_texture.pixel_size);
	background_size *= 2.0f * std::max(camera.view_size.x / background_size.x, camera.view_size.y / background_size.y);
	physics.bounds_min = -0.5f * background_size;
	physics.bounds_max = 0.5f * background_size;
	camera.center = camera.constrain(physics.bounds_min, physics.bounds_min, physics.bounds_max);
	physics.bodies = {
		{.position = {4.0f, 0.0f}, .radius = fish_radius, .drag = 1.8f, .current_scale = 0.0f, .kind = Body2D::Fish},
		{.position = {-1.5f, 1.8f}, .radius = 0.35f},
		{.position = {5.0f, -1.5f}, .inverse_mass = 0.625f, .radius = 0.43f},
		{.position = {10.0f, 3.4f}, .inverse_mass = 1.25f, .radius = 0.29f}
	};
	for (auto &body : physics.bodies) body.position += camera.center;
	// the big fish waits with its teeth just off the left side of the world.
	// its jaws meet a little above the middle so both rows of teeth stay on screen
	constexpr float BigFishY = 1.3f;
	big_fish.reset(glm::vec2(physics.bounds_min.x - big_fish.art_front(), camera.center.y + BigFishY));
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

	big_fish.update(elapsed);
	{ // swimming between the jaws makes them start to close. still being there when they slam is death
		auto &fish = physics.bodies[fish_body];
		if (fish.active && big_fish.between_jaws(fish.position)) {
			// only a swimming fish starts a bite, but a slam kills even if it got stunned partway
			if (big_fish.state == BigFish::Normal) big_fish.bite();
			// dead for now. later this could send you into the big fish's stomach level
			if (big_fish.slammed) fish.active = false;
		}
	}

	{ // a rock thrown or pushed into the big fish stuns it
		// slower than this and the rock is just drifting
		constexpr float RockHitSpeed = 3.0f;
		constexpr float RockStunTime = 5.0f;
		for (auto &body : physics.bodies) {
			if (!body.active || body.kind != Body2D::Rock) continue;
			if (big_fish.state != BigFish::Normal) break;
			if (glm::length(body.velocity) < RockHitSpeed) continue;
			if (!big_fish.hits(body.position, body.radius)) continue;
			big_fish.stun(RockStunTime);
			// the rock breaks so it can't stun again right away
			body.active = false;
		}
	}
	camera.update(physics.bodies[fish_body].position, elapsed, physics.bounds_min, physics.bounds_max);
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

	// keep the camera's view size fixed when the window changes shape
	glViewport(0, 0, drawable_size.x, drawable_size.y);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	float scale = std::min(float(drawable_size.x) / camera.view_size.x, float(drawable_size.y) / camera.view_size.y);
	GLsizei width = std::max(1, int(camera.view_size.x * scale));
	GLsizei height = std::max(1, int(camera.view_size.y * scale));
	glViewport((int(drawable_size.x) - width) / 2, (int(drawable_size.y) - height) / 2, width, height);
	glm::vec2 half_view = 0.5f * camera.view_size;
	// clamp screen shake too so it cannot reveal space outside the background
	glm::vec2 view_center = camera.constrain(camera.center - big_fish.shake(), physics.bounds_min, physics.bounds_max);
	glm::mat4 world_to_clip = glm::ortho(view_center.x - half_view.x, view_center.x + half_view.x,
		view_center.y - half_view.y, view_center.y + half_view.y);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE); // a horizontal flip reverses the quad winding
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	sprites.draw(background_texture, world_to_clip, glm::vec2(0.0f), background_size);

	// use aspect ratio
	auto sprite_size = [](SpriteTexture const &texture, float height) {
		return glm::vec2(height * float(texture.pixel_size.x) / float(texture.pixel_size.y), height);
	};
	float const stone_heights[] = {1.2f, 1.5f, 1.0f};
	for (size_t i = 0; i < 3; ++i) {
		auto const &body = physics.bodies[i + 1];
		if (body.active) sprites.draw(stone_texture, world_to_clip, body.position, sprite_size(stone_texture, stone_heights[i]));
	}
	auto const &fish = physics.bodies[fish_body];
	// the artwork faces right, so mirror leftward movement before rotating
	bool flip_x = facing.x < 0.0f;
	glm::vec2 rotation_direction = flip_x ? -facing : facing;
	float angle = std::atan2(rotation_direction.y, rotation_direction.x);
	SpriteTexture const &current_fish_texture = state == FishState::Fat ? fat_texture
		: state == FishState::Kirby ? kirby_texture : fish_texture;
	glm::vec2 fish_size = sprite_size(current_fish_texture, fish_height);
	if (fish.active) sprites.draw(current_fish_texture, world_to_clip, fish.position, fish_size, angle, flip_x);
	// drawn over the small fish so it disappears behind the jaws
	big_fish.draw(sprites, world_to_clip);

	glDisable(GL_BLEND);
	glViewport(0, 0, drawable_size.x, drawable_size.y);
	GL_ERRORS();
}
