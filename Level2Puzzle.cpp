#include "Level2Puzzle.hpp"
#include "data_path.hpp"
#include "Load.hpp"
#include "Sound.hpp"
#include <algorithm>
#include <cmath>

static constexpr float diagonal_pipe_lip = 0.66f;
static constexpr float big_pipe_lip = 0.43f;

static constexpr float ClamWarningTime = 0.8f;
static constexpr float ClamClosedTime = 1.5f;
static constexpr float PipeAngle = -0.7853982f;

static Load<Sound::Sample> arm_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_rock_hit.opus"));
});
static Load<Sound::Sample> capture_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("modified_chomp.opus"));
});
static Load<Sound::Sample> chest_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("chest_open.opus"));
});
static Load<Sound::Sample> scanner_sound(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("keycard_scan.opus"));
});

Level2Puzzle::Level2Puzzle()
	: pipe_texture(data_path("level2/pipe.png")), pearl_texture(data_path("level2/pearl.png")),
	  clam_open_texture(data_path("level2/shell_open.png")), clam_closed_texture(data_path("level2/shell_closed.png")),
	  coral_texture(data_path("coral.png")), trash_texture(data_path("level2/metal_trash.png")),
	  chest_texture(data_path("level2/chest.png")), keycard_texture(data_path("level2/keycard.png")),
	  big_pipe_texture(data_path("level2/big_pipe.png")), gear_texture(data_path("level2/gear_trash.png")),
	  sheet_texture(data_path("level2/sheet_trash.png")), scanner_locked_texture(data_path("level2/key_scanner_locked.png")),
	  scanner_unlocked_texture(data_path("level2/key_scanner_unlocked.png")) {
}

void Level2Puzzle::setup(UnderwaterPhysics &physics, glm::vec2 const &clam_position,
	glm::vec2 const &chest_position, glm::vec2 const &machine_position, size_t key_index) {
	float floor = physics.bounds_min.y;
	clam_center = clam_position;
	chest_center = chest_position;
	scanner_center = machine_position;
	key_body = key_index;
	chest = {glm::vec2(chest_center.x - 0.42f * chest_height, floor), chest_center + chest_height * glm::vec2(0.42f, 0.3f)};
	scanner = {glm::vec2(scanner_center.x - 0.26f * scanner_height, floor), scanner_center + scanner_height * glm::vec2(0.27f, 0.48f)};
	physics.obstacles.push_back(chest);
	physics.obstacles.push_back(scanner);
	//only the lower shell is solid; the open mouth must be reachable
	physics.obstacles.push_back({clam_center + clam_height * glm::vec2(-0.41f, -0.5f),
		clam_center + clam_height * glm::vec2(0.41f, -0.34f)});
	tentacles.setup(glm::vec2(chest_center.x, floor - 0.1f));
	pipe_body = physics.bodies.size();
	physics.bodies.push_back({.position = clam_center + glm::vec2(-8.0f, 0.0f),
		.radius = 0.8f, .current_scale = 0.0f, .kind = Body2D::Pipe});
	pearl_body = physics.bodies.size();
	physics.bodies.push_back({.position = clam_center + clam_height * glm::vec2(0.0f, -0.17f),
		.radius = 0.55f, .current_scale = 0.0f, .active = false, .kind = Body2D::Pearl});
	card_body = physics.bodies.size();
	physics.bodies.push_back({.position = chest_center + glm::vec2(0.0f, 0.66f * chest_height),
		.radius = 0.46f, .current_scale = 0.0f, .active = false, .kind = Body2D::Keycard});
	corals = {glm::vec2(clam_center.x - 9.5f, floor + 0.7f), glm::vec2(clam_center.x + 5.5f, floor + 0.7f),
		glm::vec2(chest_center.x - 12.0f, floor + 0.7f), glm::vec2(chest_center.x - 3.5f, floor + 0.7f),
		scanner_center + scanner_height * glm::vec2(-0.30f, 0.14f),
		glm::vec2(chest_center.x - 29.0f, floor + 0.7f),
		glm::vec2(glm::mix(chest_center.x, clam_center.x, 1.0f / 3.0f), floor + 0.7f),
		glm::vec2(glm::mix(chest_center.x, clam_center.x, 2.0f / 3.0f), floor + 0.7f),
		glm::vec2(glm::mix(clam_center.x, scanner_center.x, 1.0f / 3.0f), floor + 0.7f),
		glm::vec2(glm::mix(clam_center.x, scanner_center.x, 2.0f / 3.0f), floor + 0.7f)};

	// keep the three around the test scanner
	for (const float offset : {-12.0f, -6.0f, 9.0f}) {
		const glm::vec2 mouth(scanner_center.x + offset, floor + 22.0f);
		const float size = (physics.bounds_max.y + 3.5f - mouth.y) / (2.0f * diagonal_pipe_lip);
		ceiling_pipes.push_back({mouth, {0.0f, -1.0f}, glm::vec2(size), 0.0f, false});
	}

	//keep the body list fixed while the puzzle is running
	for (size_t &index : trash_bodies) {
		index = physics.bodies.size();
		physics.bodies.push_back({.radius = 0.55f, .drag = 1.2f, .current_scale = 0.0f,
			.active = false, .kind = Body2D::Trash});
	}
}

void Level2Puzzle::decorate(UnderwaterPhysics const &physics, std::mt19937 random, glm::vec2 tunnel_edges,
	float fish_group_x, float door_x) {
	/**
	 * Add trash and pipe decorations
	 */
	constexpr glm::vec2 pipe_spacing(21.0f, 32.0f);
	constexpr glm::vec2 scrap_spacing(11.0f, 18.0f);
	constexpr glm::vec2 scrap_sizes(1.8f, 4.0f);

	constexpr float max_angle = 60.0f;
	constexpr float mouth_spacing = 14.0f;
	const float ceiling = physics.bounds_max.y;
	const float floor = physics.bounds_min.y;
	const auto between = [&random](float low, float high) {
		return std::uniform_real_distribution<float>(low, high)(random);
	};

	// hang the largest pipe above the octopus
	trash_launch_speed = 9.0f;
	ceiling_pipes.clear();
	ceiling_pipes.push_back({{chest_center.x, ceiling - 16.0f}, {0.0f, -1.0f}, {13.0f, 26.0f}, 0.0f, true});

	for (float x = physics.bounds_min.x + 10.0f; x < physics.bounds_max.x - 6.0f; x += between(pipe_spacing.x, pipe_spacing.y)) {
		// add angles to each pipe and bury its upper end, [-60, 60]
		const float angle = glm::radians(between(-max_angle, max_angle));
		const glm::vec2 direction(std::sin(angle), -std::cos(angle));
		const float length = between(14.0f, 20.0f);
		const glm::vec2 top(x, ceiling + 3.5f);
		const glm::vec2 mouth = top + length * direction;

		// leave room for the cave and fish group
		const float left = std::min(x, mouth.x) - 2.5f;
		const float right = std::max(x, mouth.x) + 2.5f;
		if ((left <= tunnel_edges.y && right >= tunnel_edges.x) ||
			(left <= fish_group_x + 10.0f && right >= fish_group_x - 10.0f) ||
			(left <= chest_center.x + 11.0f && right >= chest_center.x - 11.0f) ||
			left < physics.bounds_min.x || right > physics.bounds_max.x) {
			continue;
		}

		// don't let each get too crowded
		bool crowded = false;
		for (const CeilingPipe &other : ceiling_pipes) {
			if (std::abs(other.mouth.x - mouth.x) < mouth_spacing) {
				crowded = true;
			}
		}
		if (!crowded) {
			ceiling_pipes.push_back({mouth, direction, glm::vec2(length / (2.0f * diagonal_pipe_lip)), angle, false});
		}
	}

	// scatter trash but not on puzzles
	floor_trash.clear();
	for (float x = physics.bounds_min.x + 5.0f; x < physics.bounds_max.x - 4.0f; x += between(scrap_spacing.x, scrap_spacing.y)) {
		const float size = between(scrap_sizes.x, scrap_sizes.y);
		if (std::abs(x - clam_center.x) < 8.0f || std::abs(x - chest_center.x) < 13.0f ||
			std::abs(x - scanner_center.x) < 7.0f || std::abs(x - door_x) < 10.0f) {
			continue;
		}

		const glm::vec2 position(x, floor + 0.32f * size);
		floor_trash.push_back({position, size, between(-0.55f, 0.55f),
			std::uniform_int_distribution<unsigned>(0, 2)(random)});
	}

	// keep choosing sizes from the same sequence
	trash_random = random;
}

void Level2Puzzle::draw_decorations(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	// align each opening with its falling trash
	for (const CeilingPipe &pipe : ceiling_pipes) {
		const float lip = pipe.large ? big_pipe_lip * pipe.size.y : diagonal_pipe_lip * pipe.size.x;
		const glm::vec2 center = pipe.mouth - lip * pipe.direction;
		sprites.draw(pipe.large ? big_pipe_texture : pipe_texture, world_to_clip, center, pipe.size,
			pipe.angle + (pipe.large ? 0.0f : PipeAngle), false, glm::u8vec4(255), 0.0f, DrawSprites::LightOnly);
	}

	// keep loose transh behind the swimming fish
	const SpriteTexture *shapes[] = {&trash_texture, &gear_texture, &sheet_texture};
	for (const FloorTrash &piece : floor_trash) {
		sprites.draw(*shapes[piece.shape], world_to_clip, piece.position, glm::vec2(piece.size), piece.angle,
			false, glm::u8vec4(255), 0.0f, DrawSprites::LightOnly);
	}
}

void Level2Puzzle::check_puzzle_hits(UnderwaterPhysics &physics, Checkpoint *progress) {
	auto hits = [](Body2D const &body, StaticBox2D const &target) {
		if (!body.active || !body.player_spat) return false;
		glm::vec2 offset = body.position - glm::clamp(body.position, target.min, target.max);
		glm::vec2 contact;
		return glm::dot(offset, offset) <= body.radius * body.radius ||
			std::isfinite(hit_time(body, target, float(UnderwaterPhysics::fixed_step), &contact));
	};
	Body2D &pipe = physics.bodies[pipe_body];
	Body2D &pearl = physics.bodies[pearl_body];
	Body2D &card = physics.bodies[card_body];
	Body2D &key = physics.bodies[key_body];
	//a pipe hitting either shell props it open, even during a snap
	StaticBox2D shell{clam_center + clam_height * glm::vec2(-0.47f, -0.5f),
		clam_center + clam_height * glm::vec2(0.47f, 0.43f)};
	//either end of the pipe can hit the shell
	shell.min.x -= pipe.half_segment;
	shell.max.x += pipe.half_segment;
	if (!clam_wedged && hits(pipe, shell)) {
		pipe.active = false;
		pipe.player_spat = false;
		clam_wedged = true;
		clam_time = -1.0f;
		pearl.active = true;

		if (progress) {
			progress->finish_puzzle(Level2Checkpoint::Clam);
		}
	}
	if (!tentacles_distracted && pearl.active && pearl.player_spat &&
		tentacles.touches(pearl.position, pearl.radius, clock)) {
		pearl.active = false;
		pearl.player_spat = false;
		tentacles_distracted = true;
		held_pearl = pearl.position;
		tentacles.distract(held_pearl);

		if (progress) {
			progress->finish_puzzle(Level2Checkpoint::Octopus);
		}
	}
	// take the key out alive before opening the chest
	const bool key_recovered = !progress || (progress->puzzle_flags & puzzle_flag(Level2Checkpoint::Tunnel)) != 0;
	if (!chest_open && tentacles_distracted && key_recovered && hits(key, chest)) {
		key.active = false;
		key.player_spat = false;
		chest_open = true;
		card.active = true;

		if (progress) {
			progress->finish_puzzle(Level2Checkpoint::Chest);
		}
		Sound::play_3D(*chest_sound, 0.3f, glm::vec3(chest_center, 0.0f), 18.0f);
	}
	if (machine_time < 0.0f && hits(card, scanner)) {
		card.active = false;
		card.player_spat = false;
		machine_time = 0.0f;

		if (progress) {
			progress->finish_puzzle(Level2Checkpoint::Machine);
		}
		Sound::play_3D(*scanner_sound, 0.3f, glm::vec3(scanner_center, 0.0f), 18.0f);
	}
	for (size_t index : trash_bodies) {
		Body2D &trash = physics.bodies[index];
		if (trash.active && !trash.player_spat) trash.add_force(glm::vec2(0.0f, -6.0f));
	}
}

void Level2Puzzle::update(float elapsed, UnderwaterPhysics &physics, size_t player_body, Camera2D const &camera) {
	clock += elapsed;
	Body2D &fish = physics.bodies[player_body];
	StaticBox2D mouth{clam_center + clam_height * glm::vec2(-0.36f, -0.27f),
		clam_center + clam_height * glm::vec2(0.36f, 0.0f)};
	bool in_mouth = glm::distance(fish.position, glm::clamp(fish.position, mouth.min, mouth.max)) < fish.radius;
	if (!clam_wedged) {
		if (clam_time < 0.0f && fish.active && in_mouth) clam_time = 0.0f;
		if (clam_time >= 0.0f) {
			clam_time += elapsed;
			if (clam_time >= ClamWarningTime && in_mouth) fish.active = false;
			if (clam_time > ClamWarningTime + ClamClosedTime) clam_time = -1.0f;
		}
	}
	if (clam_wedged) wedge_time += elapsed;
	//let the player see the chest and guarding arms before an attack can start
	glm::vec2 half_view = 0.5f * camera.view_size;
	bool in_view = chest_center.x > camera.center.x - half_view.x + 5.0f &&
		chest_center.x < camera.center.x + half_view.x - 5.0f;
	if (tentacles.update(elapsed, fish.position, fish.active && !chest_open && in_view)) {
		glm::vec2 source = glm::mix(fish.position, chest_center, 0.5f);
		Sound::play_3D(*arm_sound, 0.3f, glm::vec3(source, 0.0f), 18.0f);
	}
	tentacles.push_fish(fish, clock);
	if (!fish.active && death_time == 0.0f) {
		Sound::play_3D(*capture_sound, 0.3f, glm::vec3(fish.position, 0.0f), 18.0f);
	}
	if (tentacles.holding_fish()) fish.position = tentacles.grip_position();
	if (machine_time >= 0.0f && fish.active) {
		machine_time += elapsed;
		if (machine_time >= next_drop) {
			constexpr float drop_interval = 0.9f;
			next_drop = machine_time + drop_interval;
			drop_nearby_trash(physics, fish.position);
		}
	}
	if (!fish.active) death_time += elapsed;
}

void Level2Puzzle::drop_nearby_trash(UnderwaterPhysics &physics, glm::vec2 fish_position) {
	// only pipes above the fish can drop trash
	constexpr float nearby_distance = 40.0f; // [-40, 40] -> surrounding of dropping zones
	for (size_t step = 0; step < ceiling_pipes.size(); ++step) {
		const size_t pipe_index = (next_pipe + step) % ceiling_pipes.size();
		const CeilingPipe &pipe = ceiling_pipes[pipe_index];
		if (fish_position.y > pipe.mouth.y || std::abs(fish_position.x - pipe.mouth.x) > nearby_distance) {
			continue;
		}

		for (size_t index : trash_bodies) {
			Body2D &trash = physics.bodies[index];
			const bool unused = drops_spawned < trash_bodies.size() && index == trash_bodies[drops_spawned];
			// never an item the player has swallowed
			const bool settled = drops_spawned >= trash_bodies.size() && trash.active &&
				trash.position.y < physics.bounds_min.y + trash.radius + 0.95f;
			if (!unused && !settled) {
				continue;
			}

			// shoot along the pipe and then falls down
			trash.radius = pipe.large ? big_gear_radius : std::uniform_real_distribution<float>(0.4f, 0.95f)(trash_random);
			trash.position = pipe.mouth + pipe.direction * trash.radius;
			trash.velocity = pipe.direction * trash_launch_speed;
			next_pipe = (pipe_index + 1) % ceiling_pipes.size();
			trash.force = glm::vec2(0.0f);
			trash.active = true;
			trash.player_spat = false;
			++drops_spawned;
			return;
		}

		// wait for a free piece of trash
		return;
	}
}

void Level2Puzzle::restore(UnderwaterPhysics &physics, Body2D &fish, Checkpoint const &entry,
	std::optional<size_t> carried, glm::vec2 door_center) {
	using Point = Level2Checkpoint;

	// check which puzzles the player already solved
	const auto done = [&entry](Point point) {
		return (entry.puzzle_flags & puzzle_flag(point)) != 0; // using the bitmask style puzzle_flag
	};

	// leave the pipe where it was used
	clam_wedged = done(Point::Clam);
	Body2D &pipe = physics.bodies[pipe_body];
	pipe.active = done(Point::Door) && !clam_wedged && carried != pipe_body;
	pipe.position = door_center - glm::vec2(4.0f, 0.0f);

	// leave the shell fully propped open
	if (clam_wedged) {
		wedge_time = 0.35f;
	}

	// keep one pearl in its current place
	tentacles_distracted = done(Point::Octopus);
	physics.bodies[pearl_body].active = clam_wedged && !tentacles_distracted && carried != pearl_body;

	// put the pearl back in the arms
	if (tentacles_distracted) {
		held_pearl = chest_center + glm::vec2(-5.0f, 8.0f);
		tentacles.distract(held_pearl, true);
	}

	// the chest uses up the tunnel key
	chest_open = done(Point::Chest);
	physics.bodies[key_body].active = !chest_open && carried != key_body;
	physics.bodies[card_body].active = chest_open && !done(Point::Machine) && carried != card_body;

	// leave trash below the running machine
	if (done(Point::Machine)) {
		machine_time = 0.0f;

		// use another piece if one was swallowed
		drops_spawned = carried == trash_bodies.front() ? 1 : 0;
		Body2D &trash = physics.bodies[trash_bodies[drops_spawned]];
		trash.position = glm::vec2(scanner_center.x - 6.0f, physics.bounds_min.y + trash.radius);
		trash.active = true;
		++drops_spawned;
	}

	// leave clear space around the player so they won't get killed again
	switch (entry.checkpoint) {
	case Point::Clam:
		fish.position = clam_center + glm::vec2(7.0f, 0.0f);
		break;

	case Point::Octopus:
		fish.position = chest_center + glm::vec2(16.0f, 6.0f);
		break;

	case Point::Chest:
		fish.position = chest_center + glm::vec2(5.0f, 3.0f);
		break;

	case Point::Machine:
		fish.position = scanner_center - glm::vec2(5.0f, 0.0f);
		break;

	default:
		break;
	}
}

void Level2Puzzle::draw_backdrop(glm::mat4 const &world_to_clip) const {
	tentacles.draw_backdrop(world_to_clip);
}

bool Level2Puzzle::show_dead_fish() const {
	return tentacles.holding_fish() && death_time < death_hold_time + death_fade_time;
}

void Level2Puzzle::add_lights(Water::Look &look) const {
	for (size_t i = 0; i < corals.size(); ++i) {
		//small patches along the floor lead toward the larger puzzle lights
		float strength = i >= 5 ? 0.3f : i == 3 ? 0.6f : 0.9f;
		glm::vec2 outward = i == 4 ? glm::vec2(-1.0f, 0.0f) : glm::vec2(0.0f, 1.0f);
		look.glows.emplace_back(corals[i] + outward, strength);
	}
	glm::vec2 center = 0.5f * (look.view_min + look.view_max);
	std::stable_sort(look.glows.begin(), look.glows.end(), [&](glm::vec3 const &a, glm::vec3 const &b) {
		return glm::distance(glm::vec2(a), center) < glm::distance(glm::vec2(b), center);
	});
	if (look.glows.size() > Water::max_glows) look.glows.resize(Water::max_glows);
}

void Level2Puzzle::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics) const {
	for (size_t i = 0; i < corals.size(); ++i) {
		sprites.draw(coral_texture, world_to_clip, corals[i], glm::vec2(i >= 5 ? 1.6f : 2.3f),
			i == 4 ? glm::radians(90.0f) : 0.0f, false, glm::u8vec4(150, 150, 150, 255), 0.0f, DrawSprites::Emissive);
	}
	for (size_t i = 0; i < trash_bodies.size(); ++i) {
		Body2D const &trash = physics.bodies[trash_bodies[i]];
		if (trash.active) {
			// the saved radius also identifies the large gear
			const bool gear = trash.radius == big_gear_radius;
			const float size = trash.radius * (gear ? 2.2f : 2.9f);
			sprites.draw(gear ? gear_texture : trash_texture, world_to_clip, trash.position, glm::vec2(size),
				0.25f * std::sin(clock + float(i)), i % 2 != 0, glm::u8vec4(255), 0.0f, DrawSprites::LightOnly);
		}
	}
	bool closed = !clam_wedged && clam_time >= ClamWarningTime;
	float warning = !clam_wedged && clam_time >= 0.0f ? std::min(clam_time / ClamWarningTime, 1.0f) : 0.0f;
	float height = closed ? clam_height : clam_height - 0.6f * warning;
	if (clam_wedged) {
		//the upper shell settles onto the pipe while the lower shell stays put
		constexpr float seam = 0.46f;
		float settle = 0.2f * glm::smoothstep(0.0f, 0.35f, wedge_time);
		sprites.draw(clam_open_texture, world_to_clip, clam_center + glm::vec2(0.0f, 0.5f * seam * clam_height - settle),
			glm::vec2(clam_height, (1.0f - seam) * clam_height), 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone,
			glm::vec4(0.0f, seam, 1.0f, 1.0f));
		sprites.draw(clam_open_texture, world_to_clip, clam_center + glm::vec2(0.0f, (seam - 1.0f) * 0.5f * clam_height),
			glm::vec2(clam_height, seam * clam_height), 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone,
			glm::vec4(0.0f, 0.0f, 1.0f, seam));
	} else {
		sprites.draw(closed ? clam_closed_texture : clam_open_texture, world_to_clip,
			clam_center + glm::vec2(0.0f, 0.5f * (height - clam_height)), glm::vec2(clam_height, height),
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
	Body2D const &pipe = physics.bodies[pipe_body];
	if (pipe.active || clam_wedged) {
		sprites.draw(pipe_texture, world_to_clip, clam_wedged ? clam_center + clam_height * glm::vec2(0.23f, -0.03f) : pipe.position,
			glm::vec2(pipe.half_segment > 0.0f ? door_pipe_size : pipe_height),
			clam_wedged || pipe.half_segment == 0.0f ? PipeAngle : glm::radians(45.0f), false, glm::u8vec4(255), 0.0f, DrawSprites::LightOnly);
		if (clam_wedged) {
			//the front lip hides the lower end of the prop between the shells
			constexpr float lip = 0.30f;
			sprites.draw(clam_open_texture, world_to_clip, clam_center + glm::vec2(0.0f, (lip - 1.0f) * 0.5f * clam_height),
				glm::vec2(clam_height, lip * clam_height), 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone,
				glm::vec4(0.0f, 0.0f, 1.0f, lip));
		}
	}
	Body2D const &pearl = physics.bodies[pearl_body];
	if (pearl.active || (!clam_wedged && !closed)) {
		sprites.draw(pearl_texture, world_to_clip, pearl.position, glm::vec2(1.6f),
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
	tentacles.draw(world_to_clip, clock);
	if (chest_open) {
		//lift the lid using the top half of the chest art
		constexpr float seam = 0.45f;
		sprites.draw(chest_texture, world_to_clip, chest_center + glm::vec2(0.0f, (seam - 1.0f) * 0.5f * chest_height),
			glm::vec2(chest_height, seam * chest_height), 0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone,
			glm::vec4(0.0f, 0.0f, 1.0f, seam));
		sprites.draw(chest_texture, world_to_clip, chest_center + glm::vec2(-0.55f, 0.5f * seam * chest_height + 0.85f),
			glm::vec2(chest_height, (1.0f - seam) * chest_height), 0.23f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone,
			glm::vec4(0.0f, seam, 1.0f, 1.0f));
	} else {
		sprites.draw(chest_texture, world_to_clip, chest_center, glm::vec2(chest_height),
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
	if (tentacles_distracted) {
		sprites.draw(pearl_texture, world_to_clip, held_pearl, glm::vec2(1.6f),
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
	Body2D const &card = physics.bodies[card_body];
	if (card.active) {
		sprites.draw(keycard_texture, world_to_clip, card.position, glm::vec2(1.85f),
			0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
	}
	bool unlocked = machine_time >= 0.0f;
	sprites.draw(unlocked ? scanner_unlocked_texture : scanner_locked_texture,
		world_to_clip, scanner_center, glm::vec2(scanner_height),
		0.0f, false, glm::u8vec4(255), 0.0f, DrawSprites::Stone);
}

float Level2Puzzle::fade_amount() const {
	return glm::clamp((death_time - death_hold_time) / death_fade_time, 0.0f, 1.0f);
}

void Level2Puzzle::hide_pipe(UnderwaterPhysics &physics, glm::vec2 const &position) {
	Body2D &pipe = physics.bodies[pipe_body];
	pipe.radius = door_pipe_size * 0.06f; //rounded end
	pipe.half_segment = door_pipe_size * 0.63f; //straight section on either side
	pipe.position = position;
	pipe.velocity = glm::vec2(0.0f);
	pipe.force = glm::vec2(0.0f);
	pipe.active = false;
	pipe.player_spat = false;
}

void Level2Puzzle::launch_pipe(UnderwaterPhysics &physics) {
	Body2D &pipe = physics.bodies[pipe_body];
	pipe.active = true;
	pipe.apply_impulse(glm::vec2(-pipe_launch_impulse, 0.0f));
}
