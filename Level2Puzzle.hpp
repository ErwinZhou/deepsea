#pragma once
#include "Camera2D.hpp"
#include "Checkpoint.hpp"
#include "DrawSprites.hpp"
#include "Tentacles.hpp"
#include "UnderwaterPhysics.hpp"
#include "Water.hpp"
#include <array>
#include <cstddef>
#include <random>
#include <vector>

struct Level2Puzzle {
	static constexpr float clam_height = 10.0f;
	static constexpr float chest_height = 8.0f;
	static constexpr float scanner_height = 11.0f;
	static constexpr float pipe_height = 4.2f;
	Level2Puzzle();
	void setup(UnderwaterPhysics &physics, glm::vec2 const &clam_position,
		glm::vec2 const &chest_position, glm::vec2 const &machine_position, size_t key_index, float pit_depth = 0.0f,
		size_t ground_start = 0);
	void decorate(UnderwaterPhysics const &physics, std::mt19937 random, glm::vec2 tunnel_edges,
		float fish_group_x, glm::vec2 door_center, std::array<StaticBox2D, 3> const &roofs);
	void draw_decorations(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;
	void check_puzzle_hits(UnderwaterPhysics &physics, Checkpoint *progress = nullptr);
	void update(float elapsed, UnderwaterPhysics &physics, size_t player_body, Camera2D const &camera, bool guard_active = true);
	void restore(UnderwaterPhysics &physics, Body2D &fish, Checkpoint const &entry,
		std::optional<size_t> carried);
	void add_lights(Water::Look &look) const;
	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics) const;
	void draw_backdrop(glm::mat4 const &world_to_clip) const;
	void draw_grip(glm::mat4 const &world_to_clip) const { tentacles.draw(world_to_clip, clock, true); }
	bool show_dead_fish() const;
	float fade_amount() const;
	bool machine_running() const { return machine_time >= 0.0f; }
	bool touches_arm(glm::vec2 const &position, float radius) const { return !tentacles_departed && tentacles.touches(position, radius, clock); }
	void hide_pipe(UnderwaterPhysics &physics, glm::vec2 const &position);
	void launch_pipe(UnderwaterPhysics &physics);
	//release the pearl when the arms leave the chest
	void lose_pearl();

private:
	void drop_nearby_trash(UnderwaterPhysics &physics, glm::vec2 fish_position);

	static constexpr float death_hold_time = 0.7f;
	static constexpr float capture_hold_time = 1.25f;
	static constexpr float death_fade_time = 1.5f;
	static constexpr glm::vec2 key_spawn_offset{-2.0f, 1.0f};
	static constexpr glm::vec2 pipe_spawn_offset{0.0f, 2.85f};
	static constexpr glm::vec2 pearl_or_card_spawn_offset{2.0f, 1.0f};
	static constexpr float door_pipe_size = 3.2f;
	static constexpr float pipe_launch_impulse = 10.0f;
	SpriteTexture pipe_texture, pearl_texture, clam_open_texture, clam_closed_texture;
	SpriteTexture coral_texture, trash_texture, chest_texture, keycard_texture;
	SpriteTexture big_pipe_texture, gear_texture, sheet_texture;
	SpriteTexture scanner_locked_texture, scanner_unlocked_texture;
	Tentacles tentacles;
	StaticBox2D chest, scanner;
	glm::vec2 chest_center{0.0f}, scanner_center{0.0f}, clam_center{0.0f}, held_pearl{0.0f};
	std::array<glm::vec2, 4> coral_positions{};
	unsigned coral_count = 1;

	//ceiling pipes and falling trash
	static constexpr glm::vec2 pipe_lengths{9.0f, 13.0f};
	static constexpr glm::vec2 large_pipe_size{7.5f, 17.0f};
	static constexpr float large_pipe_drop = 10.0f;
	struct CeilingPipe {
		glm::vec2 mouth{};
		glm::vec2 direction{};
		glm::vec2 size{};
		float angle = 0.0f;
		bool large = false;
	};
	struct FloorTrash {
		glm::vec2 position{};
		float size = 0.0f;
		float angle = 0.0f;
		unsigned shape = 0;
	};
	std::vector<CeilingPipe> ceiling_pipes;
	std::vector<FloorTrash> floor_trash;
	size_t next_pipe = 0;
	size_t ground_begin = 0, ground_end = 0;

	float trash_launch_speed = 4.0f;
	// small pipes drop pieces of different sizes
	std::mt19937 trash_random{};

	// the largest piece is always a gear
	static constexpr float big_gear_radius = 2.4f;
	std::array<size_t, 24> trash_bodies{};
	size_t pipe_body = 0, pearl_body = 0, card_body = 0, key_body = 0;
	unsigned drops_spawned = 0;
	float next_drop = 1.2f;
	bool clam_wedged = false, tentacles_distracted = false, chest_open = false;
	bool tentacles_departed = false;
	bool large_trash_dropped = false;
	float clam_time = -1.0f, wedge_time = 0.0f, machine_time = -1.0f;
	float clock = 0.0f, death_time = 0.0f;
};
