#include "PlayMode.hpp"

#include "data_path.hpp"
#include "gl_errors.hpp"
#include "Sound.hpp"
#include "Checkpoint.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>

PlayMode::PlayMode(LevelId start_level, std::optional<Body2D> const &belly_contents, std::string const &checkpoint_file,
	float display_gain, bool developer_mode, int entry_checkpoint)
	: level_id(start_level),
	  display_gain(display_gain),
	  developer_mode(developer_mode && start_level == LevelId::Two),
	  checkpoint_file(checkpoint_file),
	  entry_belly_contents(belly_contents),
	  entry_checkpoint(entry_checkpoint),
	  checkpoint_saved(entry_checkpoint != 0),
	  fish_texture(data_path("normal.png")),
	  kirby_texture(data_path("kirby.png")),
	  fat_texture(data_path("fat.png")),
	  fat_glow_texture(data_path("fat_with_glow.png")),
	  stone_texture(data_path("small_rock.png")) {
	physics.bodies.push_back({.radius = fish_radius, .drag = 1.8f, .current_scale = 0.0f, .kind = Body2D::Fish});
	if (level_id == LevelId::One) {
		level1.emplace();
		level1->setup(physics, camera, fish_body);
	} else if (level_id == LevelId::Test) {
		test_level.emplace();
		test_level->setup(physics, camera, fish_body);
	} else {
		assert(level_id == LevelId::Two && "play mode requires a supported level");
		level2.emplace(fish_texture, fish_height, max_swim_speed);
		carried_body = level2->setup(physics, camera, fish_body, belly_contents);
		if (entry_checkpoint == 1) {
			carried_body = level2->resume_checkpoint(physics, fish_body);
			camera.center = camera.constrain(physics.bodies[fish_body].position, physics.bounds_min, physics.bounds_max);
		}
		if (carried_body) {
			state = FishState::Fat;
		}
	}
	if (level2) camera.center = camera.constrain(camera_target(), camera_bounds_min(), physics.bounds_max);
	if (this->developer_mode) {
		camera.view_size = glm::vec2(40.0f * (16.0f / 9.0f), 40.0f);
		camera.center = camera.constrain(camera_target(), camera_bounds_min(), physics.bounds_max);
	}
}

glm::vec2 PlayMode::camera_target() const {
	glm::vec2 position = physics.bodies[fish_body].position;
	if (level2) {
		//look slightly lower near the floor so puzzle objects stay in the shot
		float near_floor = 1.0f - glm::smoothstep(12.0f, 24.0f, position.y - physics.bounds_min.y);
		position.y -= 3.0f * near_floor;
	}
	return position;
}

glm::vec2 PlayMode::camera_bounds_min() const {
	return physics.bounds_min - glm::vec2(0.0f, level2 ? 3.0f : 0.0f);
}

std::optional<Body2D> PlayMode::carried_contents() const {
	if (carried_body) {
		return physics.bodies[*carried_body];
	}
	return std::nullopt;
}

void PlayMode::clear_input() {
	input = {};
	if (state == FishState::Kirby) state = FishState::Normal;
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &) {
	if (evt.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
		clear_input();
		return true;
	}
	if (evt.type != SDL_EVENT_KEY_DOWN && evt.type != SDL_EVENT_KEY_UP) return false;
	if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.repeat) return false;
	if (transition_time >= 0.0f && evt.key.key != SDLK_ESCAPE) {
		return true;
	}
	bool pressed = evt.type == SDL_EVENT_KEY_DOWN;
	switch (evt.key.key) {
	case SDLK_LSHIFT:
	case SDLK_RSHIFT:
		if (!developer_mode) return false;
		input.fast = pressed;
		break;
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
	if (transition_time >= 0.0f) {
		transition_time += elapsed;
		if (transition_time >= transition_fade_time) {
			//keep this mode alive until update returns after replacing it
			std::shared_ptr<Mode> const keep = shared_from_this();
			Mode::set_current(std::make_shared<PlayMode>(LevelId::Two, carried_contents(), checkpoint_file, display_gain));
		}
		return;
	}
	// hit stop, based on https://uhiyama-lab.com/en/notes/unity/unity-game-feel-hit-feedback/
	if (hit_stop > 0.0f) {
		hit_stop -= elapsed;
		return;
	}
	glm::vec2 move(float(input.right) - float(input.left), float(input.up) - float(input.down));
	if (move != glm::vec2(0.0f)) {
		move = glm::normalize(move);
		if (level1) {
			level1->has_swum = true;
		}
		// face the way the keys point, even while still drifting the other way
		facing = move;
	}
	{
		const float full_turn = glm::radians(360.0f);
		const float target_angle = std::atan2(facing.y, facing.x);
		//turn through the shortest angle when crossing left
		const float turn = std::remainder(target_angle - fish_visual_angle, full_turn);
		fish_visual_angle = std::remainder(fish_visual_angle + turn * (1.0f - std::exp(-fish_turn_response * elapsed)), full_turn);
		//keep suction and spit aligned with the visible mouth
		//control responsiveness based on https://www.gamedeveloper.com/design/principles-of-virtual-sensation
		if (input.space) fish_visual_angle = target_angle;
	}
	if (level1) {
		hit_stop = level1->before_physics(physics, elapsed);
	}

	// step physics at a fixed rate so it plays the same at any frame rate
	accumulated_time += std::min(elapsed, 0.1f);
	while (accumulated_time >= UnderwaterPhysics::fixed_step) {
		auto &fish = physics.bodies[fish_body];
		// suck first, so a rock at the mouth isn't bumped away by the collision
		update_interaction();
		glm::vec2 swim_thrust = move * swim_force;
		if (carried_body && (physics.bodies[*carried_body].kind == Body2D::Rock
			|| physics.bodies[*carried_body].kind == Body2D::Key) && move != glm::vec2(0.0f)) {
			const float acceleration_scale = 1.0f / (1.0f + carried_inertia_per_radius * physics.bodies[*carried_body].radius);
			// reduce net acceleration while keeping enough thrust to overcome drag at full speed
			swim_thrust = swim_thrust * acceleration_scale + fish.velocity * fish.drag * (1.0f - acceleration_scale);
		}
		const glm::vec2 previous_position = fish.position;
		if (developer_mode && input.fast && fish.active) fish.velocity = move * 24.0f;
		fish.add_force(swim_thrust);
		if (test_level) test_level->check_puzzle_hits(physics);
		if (level2) level2->check_puzzle_hits(physics);
		physics.step();
		if (test_level) {
			test_level->push_flashlight(physics, fish_body, move, float(UnderwaterPhysics::fixed_step));
		} else if (level2) {
			level2->push_door(physics, fish_body, move, float(UnderwaterPhysics::fixed_step));
			level2->push_flashlight(physics, fish_body, move, float(UnderwaterPhysics::fixed_step));
			level2->update_worm(physics, fish_body, previous_position, float(UnderwaterPhysics::fixed_step));
		}
		float speed = glm::length(fish.velocity);
		float speed_limit = developer_mode && input.fast ? 24.0f : max_swim_speed;
		if (fish.active && speed > speed_limit) fish.velocity *= speed_limit / speed;
		accumulated_time -= UnderwaterPhysics::fixed_step;
	}

	if (test_level) {
		test_level->update(elapsed, physics, fish_body, camera);
	}
	camera.update(camera_target(), elapsed, camera_bounds_min(), physics.bounds_max);
	if (level2) {
		level2->update(elapsed, physics, camera, fish_body, move, fish_visual_angle);
		if (!checkpoint_saved && level2->checkpoint() == 1) {
			checkpoint_saved = true;
			if (!checkpoint_file.empty()) save_checkpoint(checkpoint_file, Checkpoint{.level = 2, .checkpoint = 1});
		}
	}
	Sound::listener.set_position_right(glm::vec3(camera.center, listener_depth), glm::vec3(1.0f, 0.0f, 0.0f), elapsed);

	bool restart = false;
	if (level1) {
		level1->after_physics(physics, camera, carried_body, elapsed);

		if (level1->exit_reached()) {
			transition_time = 0.0f;
			input = {};
			hit_stop = 0.0f;
			level1->stop_sounds(transition_fade_time);
			if (!checkpoint_file.empty()) {
				save_checkpoint(checkpoint_file, Checkpoint{.level = 2, .checkpoint = 0, .belly_contents = carried_contents()});
			}
		} else {
			restart = level1->restart_requested(physics);
		}
	} else if (test_level) {
		restart = test_level->restart_requested();
	} else {
		restart = level2->restart_requested();
	}
	if (restart) {
		//keep this mode alive until update returns after replacing it
		std::shared_ptr<Mode> const keep = shared_from_this();
		int checkpoint = level2 ? level2->checkpoint() : entry_checkpoint;
		Mode::set_current(std::make_shared<PlayMode>(level_id, entry_belly_contents, checkpoint_file,
			display_gain, developer_mode, checkpoint));
	}
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
	if (level2) {
		level2->react_to_suction(mouth, facing, suction_range, cone_cos, suction_any_side);
	}
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
		//hide the swallowed body until it is released
		body.active = false;
		body.velocity = glm::vec2(0.0f);
		body.force = glm::vec2(0.0f);
		body.player_spat = false;
		carried_body = target;
		state = FishState::Fat;
		if (level1) {
			level1->has_sucked = true;
		}
	} else {
		// pull with the same strength at any distance
		body.add_force((mouth - body.position) * (suction_force / nearest_distance));
	}
}

void PlayMode::spit() {
	if (!carried_body) return;
	auto const &fish = physics.bodies[fish_body];
	auto &body = physics.bodies[*carried_body];
	// place it past the mouth, inside the level
	body.position = fish.position + facing * (fish.radius + body.radius + spit_clearance);
	body.position = glm::clamp(body.position, physics.bounds_min + body.radius, physics.bounds_max - body.radius);
	body.velocity = body.kind == Body2D::Jelly ? glm::vec2(0.0f) : fish.velocity;
	body.force = glm::vec2(0.0f);
	body.active = true;
	body.player_spat = body.kind == Body2D::Rock || body.kind == Body2D::Key || body.kind == Body2D::Keycard ||
		body.kind == Body2D::Pipe || body.kind == Body2D::Pearl || body.kind == Body2D::Trash;
	// the jellyfish floats where it is released
	if (body.player_spat) body.apply_impulse(facing * spit_impulse);
	carried_body.reset();
	state = FishState::Normal;
	if (level1) {
		level1->has_spat = true;
	}
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
	glm::vec2 const &render_view = camera.view_size;
	glm::vec2 view_center = camera.center;
	if (level1) view_center = level1->shaken_center(camera.center, physics);
	else if (level2) view_center = camera.constrain(camera.center + level2->arrival_shake(), camera_bounds_min(), physics.bounds_max);
	glm::vec2 half_view = 0.5f * render_view;
	//keep horizontal shake inside the entrance and exit
	view_center.x = camera.constrain(view_center, physics.bounds_min, physics.bounds_max).x;
	glm::mat4 world_to_clip = glm::ortho(view_center.x - half_view.x, view_center.x + half_view.x,
		view_center.y - half_view.y, view_center.y + half_view.y);

	water_look.view_min = view_center - half_view;
	water_look.view_max = view_center + half_view;
	glm::vec3 jelly_light(0.0f);
	if (level1) {
		jelly_light = level1->prepare_water(water_look, physics, carried_body, view_center, render_view);
	} else if (test_level) {
		test_level->prepare_water(water_look);
	} else {
		jelly_light = level2->prepare_water(water_look, physics, fish_body, carried_body);
	}
	sprites.set_jelly_light(jelly_light);
	sprites.set_look(water_look);
	water.begin(water_look);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE); // a horizontal flip reverses the quad winding
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	int level = 1;
	if (level1) level = 1;
	else if (level2) level = 2;

	background.draw(sprites, world_to_clip, view_center, render_view, physics.bounds_min, physics.bounds_max, level);
	if (test_level) test_level->draw_backdrop(world_to_clip);
	if (level2) level2->draw_backdrop(world_to_clip);
	if (level1) {
		level1->draw_distant(sprites, world_to_clip, view_center);
	}
	water.haze();
	if (level1) {
		level1->draw(sprites, world_to_clip, physics);
	} else if (test_level) {
		test_level->draw(sprites, world_to_clip, physics);
	} else {
		level2->draw(sprites, world_to_clip, physics);
	}

	//scale artwork to the requested height without stretching it
	auto sprite_size = [](SpriteTexture const &texture, float height) {
		return glm::vec2(height * float(texture.pixel_size.x) / float(texture.pixel_size.y), height);
	};

	// the rock art fills about 83 percent of its png, so this makes the drawn rock match its radius
	constexpr float RockArtFill = 0.83f;
	for (auto const &body : physics.bodies) {
		if (body.active && body.kind == Body2D::Rock) {
			sprites.draw(stone_texture, world_to_clip, body.position, sprite_size(stone_texture, 2.0f * body.radius / RockArtFill),
				0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
		}
	}
	auto const &fish = physics.bodies[fish_body];
	// the fish png faces right, so flip it when swimming left
	const bool flip_x = std::abs(fish_visual_angle) > glm::radians(90.0f);
	const float angle = flip_x ? fish_visual_angle - std::copysign(glm::radians(180.0f), fish_visual_angle) : fish_visual_angle;
	SpriteTexture const &current_fish_texture = (carried_body && physics.bodies[*carried_body].kind == Body2D::Jelly) ? fat_glow_texture : state == FishState::Fat ? fat_texture
		: state == FishState::Kirby ? kirby_texture : fish_texture;
	glm::vec2 fish_size = sprite_size(current_fish_texture, fish_height);
	if (fish.active || (test_level && test_level->show_dead_fish()) || (level2 && level2->show_dead_fish())) {
		sprites.draw(current_fish_texture, world_to_clip, fish.position, fish_size, angle, flip_x,
			glm::u8vec4(255), 0.0f, DrawSprites::Player);
	}
	if (level1) {
		level1->draw_foreground(sprites, world_to_clip);
	}
	water.finish(display_gain);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (level1) {
		level1->draw_overlay(sprites, world_to_clip, physics, view_center, render_view);
	}
	float fade = 0.0f;
	if (transition_time >= 0.0f) {
		fade = transition_time / transition_fade_time;
	} else if (level2) {
		fade = level2->fade_amount();
	} else if (test_level) {
		fade = test_level->fade_amount();
	}
	background.draw_fade(sprites, world_to_clip, view_center, render_view, fade, level);

	glDisable(GL_BLEND);
	glViewport(0, 0, drawable_size.x, drawable_size.y);
	GL_ERRORS();
}
