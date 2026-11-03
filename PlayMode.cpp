#include "PlayMode.hpp"

#include "data_path.hpp"
#include "DrawLines.hpp"
#include "gl_errors.hpp"
#include "Load.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>

Load<Sound::Sample> fish_roar(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_big_fish_roar.opus"));
});

Load<Sound::Sample> fish_roar_near(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_big_fish_roar1.opus"));
});

Load<Sound::Sample> charge_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_big_fish_roar_charge.opus"));
});

Load<Sound::Sample> chomp_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_chomp.opus"));
});

Load<Sound::Sample> rumble_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_rumble.opus"));
});

Load<Sound::Sample> rock_hit_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_rock_hit.opus"));
});

Load<Sound::Sample> ambient_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("ambient_deep.opus"));
});

Load<Sound::Sample> presence_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("big_fish_presence.opus"));
});

PlayMode::PlayMode()
	: cave_texture(data_path("cavern_background.png")),
	  fish_texture(data_path("normal.png")),
	  kirby_texture(data_path("kirby.png")),
	  fat_texture(data_path("fat.png")),
	  stone_texture(data_path("small_rock.png")),
	  cavern_texture(data_path("cavern_rock.png")),
	  background_texture(data_path("cavern_background.png")),
	  jellyfish_texture(data_path("jellyfish.png")) {
	camera.view_size = glm::vec2(corridor_height * (16.0f / 9.0f), corridor_height);
	camera.dead_zone = glm::vec2(5.0f, 2.5f);
	physics.bounds_min = glm::vec2(-0.5f * level_length, -0.5f * corridor_height);
	physics.bounds_max = glm::vec2(0.5f * level_length, 0.5f * corridor_height);
	camera.center = camera.constrain(physics.bounds_min, physics.bounds_min, physics.bounds_max);
	physics.bodies = {
		{.position = {4.0f, 0.0f}, .radius = fish_radius, .drag = 1.8f, .current_scale = 0.0f, .kind = Body2D::Fish},
		{.position = {-1.5f, 1.8f}, .radius = 0.35f},
		{.position = {5.0f, -1.5f}, .inverse_mass = 0.625f, .radius = 0.43f}
	};
	for (auto &body : physics.bodies) body.position += camera.center;
	cave.setup(physics);
	big_fish.reset_for_world(physics.bounds_min, physics.bounds_max);
	// starts silent and fades in with the picture
	ambient = Sound::loop(*ambient_sound, 0.0f);
	ambient->set_volume(1.0f, fade_in_time);
}

PlayMode::~PlayMode() {
	// starting over makes a new PlayMode, which starts its own ambient
	if (ambient) ambient->stop();
	if (presence) presence->stop();
	if (charge_roar) charge_roar->stop();
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
	{ // every impact is heard from where it happened
		glm::vec3 at = glm::vec3(cave.events.at, 0.0f);
		if (cave.events.missed) Sound::play_3D(*rock_hit_sound, rock_miss_volume, at, rock_reach);
		if (cave.events.hit) Sound::play_3D(*rock_hit_sound, 1.0f, at, rock_reach);
		if (cave.events.broke || cave.events.crashed) Sound::play_3D(*rumble_sound, 1.0f, at, rumble_reach);
		if (cave.events.crashed) Sound::play_3D(*rock_hit_sound, 1.0f, at, rumble_reach);
	}

	// step physics at a fixed rate so it plays the same at any frame rate
	accumulated_time += std::min(elapsed, 0.1f);
	while (accumulated_time >= UnderwaterPhysics::fixed_step) {
		auto &fish = physics.bodies[fish_body];
		// suck first, so a rock at the mouth isn't bumped away by the collision
		update_interaction();
		glm::vec2 swim_thrust = move * swim_force;
		if (carried_body && move != glm::vec2(0.0f)) {
			const float acceleration_scale = 1.0f / (1.0f + carried_inertia_per_radius * physics.bodies[*carried_body].radius);
			// reduce net acceleration while keeping enough thrust to overcome drag at full speed
			swim_thrust = swim_thrust * acceleration_scale + fish.velocity * fish.drag * (1.0f - acceleration_scale);
		}
		fish.add_force(swim_thrust);
		physics.step();
		float speed = glm::length(fish.velocity);
		if (fish.active && speed > max_swim_speed) fish.velocity *= max_swim_speed / speed;
		accumulated_time -= UnderwaterPhysics::fixed_step;
	}

	camera.update(physics.bodies[fish_body].position, elapsed, physics.bounds_min, physics.bounds_max);
	float view_left = camera.center.x - 0.5f * camera.view_size.x;
	Sound::listener.set_position_right(glm::vec3(camera.center, listener_depth), glm::vec3(1.0f, 0.0f, 0.0f), elapsed);

	big_fish.prey = physics.bodies[fish_body].position;
	big_fish.update(elapsed);
	{ // the big fish wakes once the first jellyfish's light is behind you
		if (warning_time < 0.0f && view_left > cave.first_wall.x + dark_past_wall) {
			warning_time = 0.0f;
			if (ambient) ambient->set_volume(ambient_quiet_volume, ambient_fade_down);
		}
		if (big_fish.state == BigFish::Waiting && warning_time >= 0.0f) {
			float before = warning_time;
			warning_time += elapsed;
			if (before < warning_quiet && warning_time >= warning_quiet) {
				Sound::play(*fish_roar, 1.0f, far_roar_pan);
				Sound::play(*rumble_sound, 1.0f, far_roar_pan);
			}
			cave.drop_ceiling_dust(view_left, view_left + camera.view_size.x, warning_amount(), elapsed);
			if (warning_time >= warning_length) {
				big_fish.enter(big_fish.out_of_sight_x(view_left));
				glm::vec3 at = glm::vec3(big_fish.teeth_x(), camera.center.y, 0.0f);
				Sound::play_3D(*fish_roar_near, 1.0f, at, roar_reach);
				presence = Sound::loop_3D(*presence_sound, 1.0f, at, presence_reach);
				if (ambient) ambient->set_volume(1.0f, ambient_fade_up);
			}
		}
		glm::vec3 at = glm::vec3(big_fish.teeth_x(), camera.center.y, 0.0f);
		if (presence) presence->set_position(at, elapsed);
		if (charge_roar) charge_roar->set_position(at, elapsed);
	}
	{ // it can't fall further behind than just out of sight, unless the cave held it up
		float leash_x = big_fish.out_of_sight_x(view_left);
		if (big_fish.state == BigFish::Stunned) leashed = false;
		if (big_fish.swimming() && !leashed && big_fish.position.x >= leash_x) leashed = true;
		if (big_fish.swimming() && leashed) big_fish.position.x = std::max(big_fish.position.x, leash_x);
	}
	// a jellyfish goes dark once the big fish reaches it
	for (size_t i = 0; i < jelly_light.size(); ++i) {
		if (big_fish.state != BigFish::Waiting && big_fish.teeth_x() > jelly_position(i).x) {
			jelly_light[i] = std::max(0.0f, jelly_light[i] - elapsed / jelly_fade_time);
		}
	}
	float target_brightness = 1.0f;
	if (big_fish.state != BigFish::Waiting) target_brightness = 0.65f;
	else if (warning_time >= 0.0f) target_brightness = 1.0f - 0.45f * warning_amount();
	scene_brightness += (target_brightness - scene_brightness) * (1.0f - std::exp(-elapsed / 1.2f));

	{ // charge when the small fish gets too close, kill it when the jaws slam
		auto &fish = physics.bodies[fish_body];
		glm::vec3 at = glm::vec3(big_fish.teeth_x(), camera.center.y, 0.0f);
		if (fish.active && !escaped && big_fish.in_reach(fish.position)) {
			big_fish.charge();
			// starts quiet and gets louder
			charge_roar = Sound::play_3D(*charge_sound, charge_roar_start_volume, at, roar_reach);
			charge_roar->set_volume(1.0f, charge_roar_fade_in);
		}
		// pinned mid charge
		if (charge_roar && big_fish.state == BigFish::Stunned) {
			charge_roar->stop(charge_roar_fade_out);
			charge_roar = nullptr;
		}
		if (big_fish.slammed) {
			Sound::play_3D(*chomp_sound, 1.0f, at, roar_reach);
			if (charge_roar) charge_roar->stop();
			charge_roar = nullptr;
			if (fish.active && !escaped && big_fish.in_mouth(fish.position)) {
				// dead for now. later this could send you into the big fish's stomach level
				fish.active = false;
				if (ambient) ambient->set_volume(0.0f, death_fade_time);
				if (presence) presence->stop(death_fade_time);
			}
		}
	}

	if (physics.bodies[fish_body].active && physics.bodies[fish_body].position.x > cave.exit_x) escaped = true;
	if (!physics.bodies[fish_body].active || escaped) end_time += elapsed;

	// start over a moment after being eaten. escaping ends the game, so it never starts over
	constexpr float RestartDelay = 3.0f;
	if (!physics.bodies[fish_body].active && end_time > RestartDelay) {
		// keep this mode alive until update returns, since replacing it frees it.
		// based on https://en.cppreference.com/w/cpp/memory/enable_shared_from_this
		std::shared_ptr< Mode > keep = shared_from_this();
		Mode::set_current(std::make_shared< PlayMode >());
	}
}

glm::vec2 PlayMode::jelly_position(size_t i) const {
	std::array<glm::vec2, 3> const homes = {
		glm::vec2(cave.first_wall.x - 11.5f, 4.1f),
		glm::vec2(cave.hanging_rock.center.x - 7.0f, 3.0f),
		glm::vec2(cave.last_wall.x - 7.0f, 3.8f)
	};
	return homes[i] + glm::vec2(0.18f * std::sin(clock * 0.3f + float(i)), 0.25f * std::sin(clock * 0.65f + float(i) * 2.0f));
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
		body.player_spat = false;
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
	body.player_spat = true;
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
	glm::vec2 const &render_view = camera.view_size;
	glm::vec2 view_center = camera.center - big_fish.shake() + cave.shake();
	glm::vec2 half_view = 0.5f * render_view;
	{ // rumble when the hanging rock comes down, tremble while the big fish is on its way
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

	std::array<glm::vec2, 3> jelly_positions = {jelly_position(0), jelly_position(1), jelly_position(2)};
	{ // lights stay at their positions in the cave
		constexpr glm::vec2 GlowRadius = glm::vec2(5.5f, 4.0f);

		Water::Look &look = water_look;
		look.glows.clear();
		look.blockers.clear();
		look.view_min = view_center - half_view;
		look.view_max = view_center + half_view;
		for (glm::vec3 const &spot : cave.light_spots) {
			if (std::abs(spot.x - view_center.x) < half_view.x + 3.0f * GlowRadius.x) look.glows.push_back(spot);
		}
		for (size_t i = 0; i < jelly_positions.size(); ++i) {
			if (std::abs(jelly_positions[i].x - view_center.x) < half_view.x + 3.0f * GlowRadius.x) {
				look.glows.emplace_back(jelly_positions[i], jelly_light[i] * (1.2f + 0.12f * std::sin(clock * 1.3f + float(i))));
			}
		}
		if (big_fish.state != BigFish::Waiting) {
			// about as bright as a jellyfish
			constexpr float LureGlow = 1.3f;
			look.glows.emplace_back(big_fish.lure_position(), LureGlow * big_fish.lure_brightness());
		}
		for (StaticBox2D const &box : physics.obstacles) look.blockers.emplace_back(box.min, box.max);
		look.glow_radius = GlowRadius;
		look.brightness = scene_brightness;
		look.time = clock;
		look.bubble_columns = {
			glm::vec3(cave.first_wall.x - 17.0f, physics.bounds_min.y - 1.5f, physics.bounds_max.y + 2.0f),
			glm::vec3(cave.hanging_rock.center.x + 6.0f, physics.bounds_min.y - 1.5f, physics.bounds_max.y + 2.0f),
			glm::vec3(cave.last_wall.x - 4.0f, physics.bounds_min.y - 1.5f, physics.bounds_max.y + 2.0f)
		};
		sprites.set_look(look);
		// the small fish's eye goes out with it
		constexpr float EyeOutTime = 0.15f;
		float eye = physics.bodies[fish_body].active ? 1.0f : std::max(0.0f, 1.0f - end_time / EyeOutTime);
		sprites.set_predator_light(physics.bodies[fish_body].position, eye);
		water.begin(look);
	}
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE); // a horizontal flip reverses the quad winding
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	{ // two rock layers scroll at different speeds behind the fish
		float middle_y = 0.5f * (physics.bounds_min.y + physics.bounds_max.y);
		auto draw_layer = [&](SpriteTexture const &texture, float parallax, float height, float fade, uint8_t alpha) {
			height = std::max(height, 2.0f * (std::abs(view_center.y) + half_view.y) + 4.0f);
			float width = height * float(texture.pixel_size.x) / float(texture.pixel_size.y);
			glm::vec2 origin(view_center.x * (1.0f - parallax), middle_y);
			int first = int(std::floor((view_center.x - half_view.x - origin.x) / width));
			int last = int(std::floor((view_center.x + half_view.x - origin.x) / width));
			for (int i = first; i <= last; ++i) {
				sprites.draw(texture, world_to_clip, origin + glm::vec2((float(i) + 0.5f) * width, 0.0f),
					glm::vec2(width + 0.03f, height), 0.0f, i % 2 != 0,
					glm::u8vec4(255, 255, 255, alpha), fade, DrawSprites::Background);
			}
		};
		draw_layer(background_texture, 0.12f, 28.0f, 0.72f, 255);
		draw_layer(cavern_texture, 0.28f, 34.0f, 0.45f, 55);
	}
	water.haze();
	for (size_t i = 0; i < jelly_positions.size(); ++i) {
		if (jelly_light[i] <= 0.0f) continue;
		float pulse = 1.0f + 0.035f * std::sin(clock * 1.3f + float(i));
		sprites.draw(jellyfish_texture, world_to_clip, jelly_positions[i], glm::vec2(1.8f * pulse, 2.15f / pulse),
			0.04f * std::sin(clock * 0.4f + float(i)), false, glm::u8vec4(175, 185, 190, uint8_t(165.0f * jelly_light[i])), 0.0f, DrawSprites::Emissive);
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
			sprites.draw(stone_texture, world_to_clip, body.position, sprite_size(stone_texture, 2.0f * body.radius / RockArtFill),
				0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
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
	if (fish.active) sprites.draw(current_fish_texture, world_to_clip, fish.position, fish_size, angle, flip_x,
		glm::u8vec4(255), 0.0f, DrawSprites::Player);
	big_fish.draw(sprites, world_to_clip);
	cave.draw_over_big_fish(sprites, world_to_clip);
	water.finish();
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	{ // fades in from black at the start, and out to black after being eaten
		float black = 1.0f - std::min(1.0f, clock / fade_in_time);
		if (!fish.active) black = std::max(black, std::min(1.0f, end_time / death_fade_time));
		if (black > 0.0f) {
			sprites.draw(cave_texture, world_to_clip, view_center, render_view, 0.0f, false, glm::u8vec4(0x00, 0x00, 0x00, uint8_t(255.0f * black)));
		}
	}

	if (escaped) { // fade to black, then the text
		constexpr float FadeTime = 2.0f;
		constexpr float TextFadeTime = 1.0f;
		constexpr float TextHeight = 1.5f;
		float fade = std::min(1.0f, end_time / FadeTime);
		float text_fade = std::clamp((end_time - FadeTime) / TextFadeTime, 0.0f, 1.0f);
		// the cave png tinted black covers the whole view
		sprites.draw(cave_texture, world_to_clip, view_center, render_view, 0.0f, false, glm::u8vec4(0x00, 0x00, 0x00, uint8_t(255.0f * fade)));

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
