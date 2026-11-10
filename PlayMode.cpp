#include "PlayMode.hpp"

#include "data_path.hpp"
#include "DrawLines.hpp"
#include "gl_errors.hpp"
#include "Load.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
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
	  fat_glow_texture(data_path("fat_with_glow.png")),
	  stone_texture(data_path("small_rock.png")),
	  cavern_texture(data_path("cavern_rock.png")),
	  background_texture(data_path("cavern_background.png")),
	  jellyfish_texture(data_path("jellyfish.png")) {
	float far_roar_length = float(fish_roar->data.size()) / 48000.0f;
	float passing_time = (far_start - far_end) / far_speed;
	warning_length = far_begin + std::max(far_roar_length, passing_time) + near_roar_gap;
	far_roar_end = far_begin + far_roar_length;
	near_roar_end = warning_length + float(fish_roar_near->data.size()) / 48000.0f;
	camera.view_size = glm::vec2(camera_height * (16.0f / 9.0f), camera_height);
	camera.dead_zone = glm::vec2(5.0f, 2.5f);
	camera.follow_speed = 4.0f;
	physics.bounds_min = glm::vec2(-0.5f * level_length, -0.5f * corridor_height);
	physics.bounds_max = glm::vec2(0.5f * level_length, 0.5f * corridor_height);
	camera.center = camera.constrain(glm::vec2(physics.bounds_min.x, 0.0f), physics.bounds_min, physics.bounds_max);
	physics.bodies = {
		{.radius = fish_radius, .drag = 1.8f, .current_scale = 0.0f, .kind = Body2D::Fish},
		{.radius = 0.35f},
		{.inverse_mass = 0.625f, .radius = 0.43f}
	};
	cave.setup(physics);
	//place stones relative to the wall so changing the camera cannot move them into it
	physics.bodies[1].position = glm::vec2(cave.first_wall.x - 6.0f, 6.5f);
	physics.bodies[2].position = glm::vec2(cave.first_wall.x - 11.0f, -4.0f);
	jelly_body = physics.bodies.size();
	glm::vec2 jelly_start = cave.wall_crack_position(cave.first_wall) - glm::vec2(4.0f, 0.0f);
	physics.bodies.push_back({.position = jelly_start, .radius = jelly_radius, .drag = jelly_drag, .current_scale = 0.0f, .kind = Body2D::Jelly});
	//start beside the jellyfish with the crack in view
	physics.bodies[fish_body].position = jelly_start - glm::vec2(2.4f, -0.2f);
	const glm::vec2 extra_rock_offset(-4.0f, 2.0f);
	physics.bodies.push_back({.position = physics.bodies[fish_body].position + extra_rock_offset, .radius = 0.35f});
	camera.center = camera.constrain(physics.bodies[fish_body].position, physics.bounds_min, physics.bounds_max);
	big_fish.reset_for_world(physics.bounds_min, physics.bounds_max);
	// starts silent and fades in with the picture
	ambient = Sound::loop(*ambient_sound, 0.0f);
	ambient->set_volume(1.0f, fade_in_time);
}

PlayMode::~PlayMode() {
	//stop this run's sounds before restarting
	if (ambient) ambient->stop();
	if (presence) presence->stop();
	stop_roars(0.0f);
}

void PlayMode::stop_roars(float fade) {
	for (auto *roar : {&far_roar, &near_roar, &charge_roar}) {
		if (*roar) (*roar)->stop(fade);
		*roar = nullptr;
	}
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
		has_swum = true;
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
		if (carried_body && physics.bodies[*carried_body].kind == Body2D::Rock && move != glm::vec2(0.0f)) {
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
	{ // the big fish wakes once the first room is behind you
		if (warning_time < 0.0f && view_left > cave.first_wall.x + dark_past_wall) {
			warning_time = 0.0f;
			if (ambient) ambient->set_volume(ambient_quiet_volume, ambient_fade_down);
		}
		float before = warning_time;
		if (warning_time >= 0.0f) warning_time += elapsed;
		if (big_fish.state == BigFish::Waiting && warning_time >= 0.0f) {
			if (before < far_begin && warning_time >= far_begin) {
				glm::vec3 at = glm::vec3(big_fish.far_mouth(far_hinge(camera.center)), -far_roar_depth);
				far_roar = Sound::play_3D(*fish_roar, far_roar_volume, at, far_roar_reach);
				Sound::play_3D(*rumble_sound, far_roar_volume, at, far_roar_reach);
			}
			if (far_roar) far_roar->set_position(glm::vec3(big_fish.far_mouth(far_hinge(camera.center)), -far_roar_depth), elapsed);
			cave.drop_ceiling_dust(view_left, view_left + camera.view_size.x, roar_amount(), elapsed);
			if (warning_time >= warning_length) {
				big_fish.reset_for_world(physics.bounds_min, physics.bounds_max);
				big_fish.enter(big_fish.out_of_sight_x(view_left));
				glm::vec3 at = glm::vec3(big_fish.mouth_position(), 0.0f);
				near_roar = Sound::play_3D(*fish_roar_near, near_roar_volume, at, near_roar_reach);
				presence = Sound::loop_3D(*presence_sound, 1.0f, at, presence_reach);
			}
		}
		glm::vec3 at = glm::vec3(big_fish.mouth_position(), 0.0f);
		if (presence) presence->set_position(at, elapsed);
		if (near_roar) near_roar->set_position(at, elapsed);
		if (charge_roar) charge_roar->set_position(at, elapsed);
	}
	{ // the deep water comes back only while the big fish is held up
		bool held_up = big_fish.state == BigFish::Stunned;
		if (ambient && held_up != was_held_up) ambient->set_volume(held_up ? 1.0f : ambient_quiet_volume, ambient_fade_up);
		was_held_up = held_up;
	}
	// a jellyfish left behind goes dark once the big fish reaches it
	if (!jelly_in_belly() && physics.bodies[jelly_body].active && big_fish.state != BigFish::Waiting && big_fish.teeth_x() > jelly_position().x) {
		jelly_light = std::max(0.0f, jelly_light - elapsed / jelly_fade_time);
		if (jelly_light <= 0.0f) physics.bodies[jelly_body].active = false;
	}
	float target_brightness = 1.0f;
	if (big_fish.state != BigFish::Waiting) target_brightness = 0.65f;
	else if (warning_time >= 0.0f) target_brightness = 1.0f - 0.45f * warning_amount();
	scene_brightness += (target_brightness - scene_brightness) * (1.0f - std::exp(-elapsed / 1.2f));

	{ // charge when the small fish gets too close, kill it when the jaws slam
		auto &fish = physics.bodies[fish_body];
		glm::vec3 at = glm::vec3(big_fish.mouth_position(), 0.0f);
		if (fish.active && !escaped && big_fish.in_reach(fish.position)) {
			big_fish.charge();
			// starts quiet and gets louder
			charge_roar = Sound::play_3D(*charge_sound, charge_roar_start_volume, at, near_roar_reach);
			charge_roar->set_volume(near_roar_volume, charge_roar_fade_in);
		}
		// pinned mid charge
		if (charge_roar && big_fish.state == BigFish::Stunned) {
			charge_roar->stop(charge_roar_fade_out);
			charge_roar = nullptr;
		}
		if (big_fish.slammed) {
			// no bite is heard once you're out
			if (!escaped) Sound::play_3D(*chomp_sound, 1.0f, at, roar_reach);
			if (charge_roar) charge_roar->stop();
			charge_roar = nullptr;
			if (fish.active && !escaped && big_fish.in_mouth(fish.position)) {
				//being caught starts the death fade
				fish.active = false;
				if (ambient) ambient->set_volume(0.0f, death_fade_time);
				if (presence) presence->stop(death_fade_time);
				stop_roars(death_fade_time);
			}
		}
	}

	{ //fade between tutorial hints
		char const *wanted = wanted_hint();
		float step = elapsed / hint_fade_time;
		hint_amount = wanted == shown_hint ? std::min(1.0f, hint_amount + step) : std::max(0.0f, hint_amount - step);
		if (hint_amount <= 0.0f) shown_hint = wanted;
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

bool PlayMode::jelly_in_belly() const {
	return carried_body && *carried_body == jelly_body;
}

glm::vec2 PlayMode::jelly_position() const {
	//let the jellyfish drift around its position
	return physics.bodies[jelly_body].position + glm::vec2(0.18f * std::sin(clock * 0.3f), 0.25f * std::sin(clock * 0.65f));
}

char const *PlayMode::wanted_hint() const {
	auto const &fish = physics.bodies[fish_body];
	if (!fish.active || escaped) return nullptr;
	if (!has_swum) return "WASD to swim";
	if (carried_body) return has_spat ? nullptr : "Press Space to spit";
	if (has_sucked) return nullptr;
	for (size_t i = 0; i < physics.bodies.size(); ++i) {
		auto const &body = physics.bodies[i];
		if (i != fish_body && body.active && glm::length(body.position - fish.position) < suck_hint_range) return "Hold Space to suck";
	}
	return nullptr;
}

float PlayMode::warning_amount() const {
	if (warning_time < 0.0f) return 0.0f;
	return std::clamp((warning_time - warning_quiet) / (warning_length - warning_quiet), 0.0f, 1.0f);
}

float PlayMode::roar_amount() const {
	if (escaped || !physics.bodies[fish_body].active) return 0.0f;
	auto envelope = [&](float begin, float end) {
		if (warning_time <= begin || warning_time >= end) return 0.0f;
		return std::min(1.0f, std::min((warning_time - begin) / 0.25f, (end - warning_time) / 0.6f));
	};
	return std::max(envelope(far_begin, far_roar_end), envelope(warning_length, near_roar_end));
}

bool PlayMode::far_passing() const {
	float passing_time = (far_start - far_end) / far_speed;
	return warning_time >= far_begin && warning_time < far_begin + passing_time;
}

glm::vec2 PlayMode::far_hinge(glm::vec2 const &view_center) const {
	return glm::vec2(view_center.x + far_start - far_speed * (warning_time - far_begin), far_height);
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
		//hide the swallowed body until it is released
		body.active = false;
		body.velocity = glm::vec2(0.0f);
		body.force = glm::vec2(0.0f);
		body.player_spat = false;
		carried_body = target;
		state = FishState::Fat;
		has_sucked = true;
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
	body.player_spat = body.kind == Body2D::Rock;
	// the jellyfish floats where it is released
	if (body.kind == Body2D::Rock) body.apply_impulse(facing * spit_impulse);
	carried_body.reset();
	state = FishState::Normal;
	has_spat = true;
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
	{ //shake the camera during rockfalls and roars
		constexpr float RumbleSize = 0.25f;
		// the rumble eases off over its last moments
		constexpr float RumbleFade = 0.3f;
		constexpr float TrembleSize = 0.08f;
		float amount = RumbleSize * std::min(1.0f, rumble / RumbleFade);
		amount += TrembleSize * roar_amount();
		view_center += amount * glm::vec2(std::sin(clock * 67.0f), std::cos(clock * 53.0f));
	}
	//keep horizontal shake inside the entrance and exit
	view_center.x = camera.constrain(view_center, physics.bounds_min, physics.bounds_max).x;
	glm::mat4 world_to_clip = glm::ortho(view_center.x - half_view.x, view_center.x + half_view.x,
		view_center.y - half_view.y, view_center.y + half_view.y);

	//fade the player's lights after it is caught
	constexpr float EyeOutTime = 0.15f;
	float eye = physics.bodies[fish_body].active ? 1.0f : std::max(0.0f, 1.0f - end_time / EyeOutTime);
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
		glm::vec3 jelly_source(0.0f);
		if (jelly_in_belly() || physics.bodies[jelly_body].active) {
			glm::vec2 position = jelly_in_belly() ? physics.bodies[fish_body].position : jelly_position();
			float strength = jelly_light * (jelly_glow + jelly_glow_pulse * std::sin(clock * 1.3f));
			if (jelly_in_belly()) strength *= eye;
			jelly_source = glm::vec3(position, strength);
		}
		if (std::abs(jelly_source.x - view_center.x) < half_view.x + 3.0f * GlowRadius.x) look.glows.push_back(jelly_source);
		sprites.set_jelly_light(jelly_source);
		for (StaticBox2D const &box : cave.light_blockers) look.blockers.emplace_back(box.min, box.max);
		look.glow_radius = GlowRadius;
		look.brightness = scene_brightness;
		look.time = clock;
		look.bubble_columns = {
			glm::vec3(cave.first_wall.x - 17.0f, physics.bounds_min.y - 1.5f, physics.bounds_max.y + 2.0f),
			glm::vec3(cave.hanging_rock.center.x + 6.0f, physics.bounds_min.y - 1.5f, physics.bounds_max.y + 2.0f),
			glm::vec3(cave.last_wall.x - 4.0f, physics.bounds_min.y - 1.5f, physics.bounds_max.y + 2.0f)
		};
		sprites.set_look(look);
		water.begin(look);
	}
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE); // a horizontal flip reverses the quad winding
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	{ // two rock layers scroll at different speeds behind the fish
		float middle_y = 0.5f * (physics.bounds_min.y + physics.bounds_max.y);
		auto draw_layer = [&](SpriteTexture const &texture, float parallax, float height, float fade, uint8_t alpha) {
			height = std::max(height, corridor_height + 4.0f);
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
	if (far_passing()) big_fish.draw_far(sprites, world_to_clip, far_hinge(view_center));
	water.haze();
	if (physics.bodies[jelly_body].active) {
		float pulse = 1.0f + 0.035f * std::sin(clock * 1.3f);
		sprites.draw(jellyfish_texture, world_to_clip, jelly_position(), glm::vec2(1.8f * pulse, 2.15f / pulse),
			0.04f * std::sin(clock * 0.4f), false, glm::u8vec4(175, 185, 190, uint8_t(165.0f * jelly_light)), 0.0f, DrawSprites::Emissive);
	}

	//scale artwork to the requested height without stretching it
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
	const bool flip_x = std::abs(fish_visual_angle) > glm::radians(90.0f);
	const float angle = flip_x ? fish_visual_angle - std::copysign(glm::radians(180.0f), fish_visual_angle) : fish_visual_angle;
	SpriteTexture const &current_fish_texture = jelly_in_belly() ? fat_glow_texture : state == FishState::Fat ? fat_texture
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

	//center text on the requested position
	auto draw_centered_text = [&](char const *text, glm::vec2 const &center, float text_height, glm::u8vec4 const &color) {
		DrawLines lines(world_to_clip);
		glm::vec3 text_end;
		lines.draw_text(text, glm::vec3(0.0f), glm::vec3(text_height, 0.0f, 0.0f), glm::vec3(0.0f, text_height, 0.0f), color, &text_end);
		glm::vec3 shift = glm::vec3(center - 0.5f * glm::vec2(text_end.x, text_height), 0.0f);
		for (DrawLines::Vertex &vertex : lines.attribs) vertex.Position += shift;
	};

	if (shown_hint && hint_amount > 0.0f) { // over the small fish, or under it if the ceiling or the jellyfish is in the way
		constexpr float ViewMargin = 5.0f;
		constexpr glm::vec2 JellyClearance = glm::vec2(5.5f, 2.0f);
		float above = fish.position.y + hint_above_fish;
		glm::vec2 to_jelly = glm::abs(jelly_position() - glm::vec2(fish.position.x, above));
		bool jelly_in_way = physics.bodies[jelly_body].active && to_jelly.x < JellyClearance.x && to_jelly.y < JellyClearance.y;
		float y = above + hint_height < view_center.y + half_view.y && !jelly_in_way ? above : fish.position.y - hint_above_fish;
		float x = std::clamp(fish.position.x, view_center.x - half_view.x + ViewMargin, view_center.x + half_view.x - ViewMargin);
		draw_centered_text(shown_hint, glm::vec2(x, y), hint_height, glm::u8vec4(0xc8, 0xd2, 0xd6, uint8_t(255.0f * hint_amount)));
	}

	if (escaped) { // fade to black, then the text
		constexpr float FadeTime = 2.0f;
		constexpr float TextFadeTime = 1.0f;
		constexpr float TextHeight = 1.5f;
		float fade = std::min(1.0f, end_time / FadeTime);
		float text_fade = std::clamp((end_time - FadeTime) / TextFadeTime, 0.0f, 1.0f);
		// the cave png tinted black covers the whole view
		sprites.draw(cave_texture, world_to_clip, view_center, render_view, 0.0f, false, glm::u8vec4(0x00, 0x00, 0x00, uint8_t(255.0f * fade)));

		draw_centered_text("You Escaped", view_center, TextHeight, glm::u8vec4(0xff, 0xff, 0xff, uint8_t(255.0f * text_fade)));
	}

	glDisable(GL_BLEND);
	glViewport(0, 0, drawable_size.x, drawable_size.y);
	GL_ERRORS();
}
