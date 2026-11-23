#pragma once

#include "Camera2D.hpp"
#include "BigFish.hpp"
#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "Water.hpp"
#include "Level2Puzzle.hpp"
#include "Sound.hpp"
#include "Tentacles.hpp"
#include "level1.hpp"

#include <cstddef>
#include <array>
#include <deque>
#include <optional>
#include <random>
#include <vector>

struct Level2 {
	// share one seed for this run of level two
	inline static const std::mt19937::result_type random_seed = std::random_device{}();

	Level2(SpriteTexture const &normal_texture, float fish_height, float max_swim_speed);
	~Level2();
	std::optional<size_t> setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body,
		std::optional<Body2D> const &belly_contents);
	glm::vec3 prepare_water(Water::Look &look, UnderwaterPhysics const &physics, size_t player_body,
		std::optional<size_t> carried_body) const;
	void check_puzzle_hits(UnderwaterPhysics &physics);
	float surface_y() const { return seafloor_y; }
	float camera_floor(float x) const;
	float pit_reveal(glm::vec2 const &position) const;
	glm::vec2 pit_focus() const { return pit_center + glm::vec2(0.0f, 10.0f - chest_depth); }
	StaticBox2D pit_bounds() const {
		return {pit_center - glm::vec2(pit_half_width, pit_depth), pit_center + glm::vec2(pit_half_width, 0.0f)};
	}
	bool show_dead_fish() const { return puzzle.show_dead_fish(); }
	void draw_grip(glm::mat4 const &world_to_clip) const { puzzle.draw_grip(world_to_clip); }
	const Checkpoint &puzzle_progress() const { return progress; }
	bool tunnel_finished() const { return (progress.puzzle_flags & puzzle_flag(Level2Checkpoint::Tunnel)) != 0; }
	void check_tunnel_exit(Body2D const &player, glm::vec2 previous_position, std::optional<size_t> carried_body);
	bool has_jelly() const { return jelly_body.has_value(); }
	bool inside_tunnel(glm::vec2 position) const;
	void place_jelly_nearby(UnderwaterPhysics &physics, size_t player_body);
	void resume_checkpoint(UnderwaterPhysics &physics, size_t player_body, Checkpoint const &entry,
		std::optional<size_t> carried_body);
	void push_door_wheel(UnderwaterPhysics &physics, size_t player_body, glm::vec2 const &movement, float elapsed);
	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics) const;
	float fade_amount() const;

	// main player update logic
	void update(float elapsed, UnderwaterPhysics &physics, Camera2D const &camera, size_t player_body,
		glm::vec2 const &player_movement, float player_angle);

	// draw worm update moving
	void update_worm(UnderwaterPhysics &physics, size_t player_body, glm::vec2 previous_position, float elapsed);
	// push flashlight to another angle
	void push_flashlight(UnderwaterPhysics &physics, size_t player_body, glm::vec2 const &movement, float elapsed);
	bool restart_requested() const;
	// true once the player swims into the held-open mouth in the finale
	bool exit_reached() const { return escaped; }
	// for arrival of worm, could be reused for big fish and final combat
	glm::vec2 arrival_shake() const;

	void react_to_suction(glm::vec2 const &mouth, glm::vec2 const &facing, float range, float cone_cos, float any_side);

private:
	//width and height of the swimming area
	static constexpr float level_length = 320.0f;
	//the corridor scrolls vertically inside the tighter view
	static constexpr float corridor_height = 80.0f;
	static constexpr float seafloor_y = -0.5f * corridor_height;
	static constexpr float pit_depth = 112.0f;
	static constexpr float chest_depth = 36.0f;
	static constexpr float pit_half_width = 14.5f;
	glm::vec2 pit_center{0.0f};
	std::array<StaticBox2D, 2> ground{};
	SpriteTexture mound_texture;
	std::array<glm::vec2, 2> mound_centers{};
	glm::vec2 mound_size{18.0f, pit_depth + 6.0f};
	glm::vec2 ledge_center{0.0f};
	static constexpr glm::vec2 ledge_size{20.0f, 3.0f};
	void setup_octopus_pit(UnderwaterPhysics &physics);

	struct Seal {
		glm::vec2 center{0.0f};
		glm::vec2 size{1.0f};
		float angle = 0.0f;
		int hits_left = 3;
		float shudder = 0.0f;
	};
	Seal pit_seal;
	SpriteTexture column_textures[3];
	Level1::Target column_targets[3];
	size_t pit_obstacle_start = 0;
	void setup_pit_seal(UnderwaterPhysics &physics);
	void update_pit_seal_obstacles(UnderwaterPhysics &physics) const;
	void draw_pit_seal(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;
	struct Chip {
		glm::vec2 position{0.0f}, velocity{0.0f};
		float life = 0.0f;
	};
	std::vector<Chip> chips;
	static constexpr float low_ceiling_height = 22.0f;
	std::array<StaticBox2D, 3> ceiling{};
	static constexpr float camera_height = 24.0f;
	static constexpr float scene_brightness = 1.0f;

	//seconds to fade in from black at the start
	static constexpr float fade_in_time = 1.5f;
	//seconds to fade to black and retry after the player dies
	static constexpr float death_fade_time = 1.5f;
	static constexpr float restart_delay = 3.0f;
	float clock = 0.0f; //seconds since entering level 2
	float death_time = 0.0f; //seconds since the player died
	std::shared_ptr<Sound::PlayingSample> ambient;

	/**
	 * Jellyfish and its reborn
	 */

	//base light strength and how far it rises and falls during the slow pulse
	static constexpr float jelly_glow = 1.2f;
	static constexpr float jelly_glow_pulse = 0.12f;
	//let the jellyfish drift visually around its physics position
	glm::vec2 jelly_position(Body2D const &jelly) const;
	//only present if the player carried the jellyfish through the exit
	std::optional<size_t> jelly_body;
	std::optional<SpriteTexture> jellyfish_texture;

	/**
	 * Tunnel Puzzle
	 * tunnel basic, flashlight, keys, corals, and worms
	 */
	//distance from the cave's left end to the player's starting position
	static constexpr float entrance_distance = 12.0f;
	//place the tunnel's left edge at 1/3
	static constexpr float tunnel_start_fraction = 1.0f / 3.0f;
	//leave a return route beneath the tunnel
	static constexpr float tunnel_width = level_length / 3.0f;
	glm::vec2 tunnel_center{0.0f};
	glm::vec2 tunnel_size{0.0f};
	void setup_tunnel(UnderwaterPhysics &physics);
	SpriteTexture tunnel_texture;
	std::vector<unsigned char> tunnel_alpha; //opacity for light blocking, rows run downward

	//key
	std::optional<size_t> key_body;
	SpriteTexture key_texture;
	glm::vec2 key_position{0.0f};
	static constexpr float key_height = 1.6f; //draw key height and its distance below the tunnel's upper opening
	static constexpr float key_radius = 0.6f; //key collision radius
	static constexpr float key_wake_distance = 10.0f; //distance from spawn that wakes the worm
	static constexpr float key_end_inset = 3.2f;
	static constexpr float key_left_offset = 0.9f;

	//coral
	SpriteTexture coral_texture;
	struct Coral {
		glm::vec2 position{0.0f};
		float angle = 0.0f;
	};
	std::array<Coral, 2> corals{};
	static constexpr float coral_size = 2.4f; //guide coral size
	static constexpr float coral_base_inset = 0.08f; //bury the flat edge slightly
	static constexpr float coral_glow = 1.0f; //light reaches the key
	static constexpr float coral_light_offset = 0.4f; //light sits outside the rock

	//flashlight
	struct Flashlight {
		glm::vec2 anchor{0.0f};
		glm::vec2 center{0.0f};
		float height = flashlight_height;
		float range = flashlight_beam_range;
		float tilt = flashlight_initial_tilt;
		bool turned = false;
		Water::Beam light;
	};
	SpriteTexture flashlight_texture;
	SpriteTexture flashlight_base_texture; // for fixing it position
	std::array<Flashlight, 2> flashlights{};

	void setup_flashlight(UnderwaterPhysics &physics);
	void aim_flashlight(Flashlight &flashlight, UnderwaterPhysics const &physics);
	void push_flashlight(Flashlight &flashlight, UnderwaterPhysics &physics, Body2D &body,
		glm::vec2 const &movement, float elapsed);

	static constexpr float flashlight_height = 4.8f; //flashlight height
	static constexpr float inside_flashlight_height = 2.0f; //smaller lamp inside the tunnel
	static constexpr float inside_flashlight_range = 35.0f; //inside beam reach
	static constexpr float flashlight_entrance_offset = -5.0f; //horizontal offset from entrance, negative is on left
	static constexpr float flashlight_burial_depth = 0.3f; //pivot depth below floor
	static constexpr float flashlight_initial_tilt = 20.0f; //degrees left from upward, face right to make it harder, they have to push them
	static constexpr glm::vec2 flashlight_tilt_limits{-35.0f, 45.0f}; //right and left tilt limits
	static constexpr float flashlight_turn_speed = 55.0f; //degrees per second when pushing near the lens

	static constexpr float flashlight_beam_range = 65.0f; //maximum forward reach before wall clipping
	static constexpr float flashlight_beam_half_width = 0.75f; //bright core half-width
	static constexpr float flashlight_spill_radius = 4.0f; //extra lateral reach of soft light
	static constexpr float flashlight_beam_strength = 2.4f; //brightness multiplier
	static constexpr glm::vec2 flashlight_base_size{2.7f, 1.3f}; //drawn rock base width and height
	static constexpr glm::vec2 flashlight_base_collision_size{1.8f, 0.35f}; //collision width and height above floor

	//worm
	struct Worm {
		static constexpr float speed_fraction = 0.75f; //relative to full swim speed with the key
		SpriteTexture texture;
		float chase_speed;
		glm::vec2 head{};
		std::vector<glm::vec2> trail; //where the head has been
		std::deque<glm::vec2> tracks; //player positions still to reach

		bool awake = false;
		bool returning_to_entrance = false;
		bool stopped = false;
		static constexpr float size = 3.6f;

		glm::vec2 facing() const;
		void chase(glm::vec2 player, glm::vec2 entrance, float elapsed);
		void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;
	} worm;

	static constexpr float shake_duration = 12.373f; //length of the big fish arrival roar
	static constexpr float shake_strength = 0.08f;
	float shake_left = 0.0f;

	/**
	 * Small scary fish
	 */
	struct SmallScaryFish {
		enum class State { Waiting, Arriving, Mimicking, Fleeing, Gone, Dormant, Turning, Chasing };
		State state = State::Waiting; // default to wait there first
		glm::vec2 position{0.0f};
		glm::vec2 velocity{0.0f};
		glm::vec2 arrival_offset{0.0f};
		float angle = 0.0f;
		float radius = 0.0f;
		float trapped_time = 0.0f;
		bool leaving_arm = false;
		float scale = 1.0f; //size relative to the player, default to be the same size, the group has various sizes
	};
	SmallScaryFish small_fish;
	float next_mimic_time = 0.0f; //level time when the next fish may appear
	bool mimic_encounters_finished = false; //stop single fish for this run after nearing the seven fish
	glm::vec2 previous_player_position{0.0f};
	bool small_fish_path_clear(glm::vec2 const &from, glm::vec2 const &to,
		UnderwaterPhysics const &physics, float radius) const;
	void update_small_fish(float elapsed, UnderwaterPhysics const &physics, Camera2D const &camera,
		Body2D const &player, glm::vec2 const &player_step, glm::vec2 const &player_movement, float player_angle);

	static constexpr size_t fish_group_count = 7; // small fish appears in group of 7 because the octopus has 8 hands
	static constexpr float fish_group_depth_fraction = 0.25f; // the group appear in about 25% below the cave top
	static constexpr float fish_group_trigger_distance = 4.0f; // once the player appears near 4 units to any of the small fish, trigger turn around and chasing
	static constexpr float fish_group_turn_time = 0.15f; // turn time and transformation
	static constexpr float fish_group_speed_multiplier = 0.7f; // default to be 0.7 of the player speed
	static constexpr float fish_group_separation_distance = 1.4f; // if two fish <1.4 unit, seperate each other
	static constexpr float fish_group_separation_weight = 1.5f; // the force to control seperatance
	static constexpr glm::vec2 fish_group_scale_range{1.2f, 2.0f}; //min and max size relative to the player
	static constexpr float fish_group_position_offset = 0.9f; //max random shift on each axis, in units, position them randomly
	static constexpr glm::vec2 mimic_first_delay{5.0f, 10.0f}; // first encounter appears in 5-10 seconds
	static constexpr glm::vec2 mimic_repeat_delay{15.0f, 30.0f}; // second and all following will be in 15-30
	static constexpr float mimic_end_fraction = 4.0f / 5.0f; // because those small fish group will start chasing, no need for small fishs
	static constexpr float mimic_quiet_screen_fraction = 0.5f; //stop single fish half a screen before the seven-fish position
	static constexpr float mimic_screen_fraction = 1.0f / 3.0f;
	static constexpr float mimic_speed_multiplier = 3.0f; // appearing in 3x speed than the player
	static constexpr float mimic_approach_distance = 4.0f; // flee when the player is within 4 world units
	static constexpr float mimic_approach_cos = 0.6f; // movement input must point within about 53 degrees of the fish
	static constexpr float mimic_offscreen_margin = 1.0f;
	SpriteTexture const &small_fish_texture;
	const float small_fish_height;
	const float small_fish_fast_speed;

	std::array<SmallScaryFish, fish_group_count> fish_group{};
	SpriteTexture fish_eyes_texture;
	const float fish_group_speed;
	bool fish_group_triggered = false;
	float fish_group_turn_remaining = 0.0f;

	void update_fish_group(float elapsed, UnderwaterPhysics &physics, size_t player_body, glm::vec2 const &player_step);
	void draw_fish(DrawSprites const &sprites, glm::mat4 const &world_to_clip,
		SmallScaryFish const &fish, SpriteTexture const &texture) const;

	/**
	 * Door Puzzle
	 */
	SpriteTexture door_texture;
	SpriteTexture floor_texture;
	SpriteTexture wheel_mark_texture;
	Level2Puzzle puzzle;

	// wake the big fish when the machine starts
	BigFish big_fish;
	std::shared_ptr<Sound::PlayingSample> arrival_roar;
	std::shared_ptr<Sound::PlayingSample> presence;
	std::shared_ptr<Sound::PlayingSample> charge_roar;
	void stop_chase_sounds();

	float roar_length = 0.0f;
	void update_big_fish(float elapsed, Body2D &player, Camera2D const &camera);
	SpriteTexture door_wheel_texture;
	static constexpr float door_height = 30.0f;
	static constexpr float wheel_radius = door_height * 0.17f; //include the handles
	size_t wheel_body = 0;
	static constexpr float wheel_turn_speed = 2.5f; //radians per second at full push
	glm::vec2 door_center{0.0f};
	static constexpr float wheel_open_angle = glm::radians(180.0f); // clockwise turn needed to open the door
	bool door_open = false;
	float wheel_angle = 0.0f; // initial is 0

	// clockwise travel since the last wrong turn
	float clockwise_turn = 0.0f;

	Checkpoint progress{.level = 2};

	/*
	* Finale stuff
	*/
	static constexpr float finale_trigger_offset = 30.0f; // trigger line distance right of the octopus pit
	float finale_trigger_x = 0.0f;  // set in setup(): to the right of the octopus pit
	bool finale_started = false;
	bool escaped = false; // the player swam into the held-open mouth
	// separate from the pit octopus, these rise from the floor instead
	Tentacles arms;
	static constexpr float finale_root_depth = 1.0f; // roots start this far under the floor, out of sight
	std::array<glm::vec2, 8> finale_arm_targets() const;

	/**
	 * Finale rockfall: a couple seconds after the arms grab the jaws, big rocks fall left of the fish
	 * and crush the player unless they swim into the mouth
	 */
	SpriteTexture big_rock_texture;
	Level1::Target big_rock_target; // same collision boxes as level 1's fallen rock, at rock_size
	// copies of level 1's hanging rock values (HangingSize, FallGravity, FallSpin, RestHeight in level1.cpp)
	static constexpr float rock_size = 9.0f;
	static constexpr float rock_gravity = 25.0f;
	static constexpr float rock_spin = 0.6f;
	static constexpr float rock_rest_height = 0.32f;
	struct FallingRock {
		glm::vec2 position{0.0f};
		float velocity = 0.0f; // downward speed
		float angle = 0.0f;
		float rest_time = -1.0f; // negative while falling, then seconds since it landed
	};
	std::vector<FallingRock> falling_rocks;
	float grab_time = -1.0f; // seconds since every arm gripped, negative before that
	float next_rock_time = 0.0f;
	static constexpr float rock_delay = 2.0f; // wait to have the cave start collapsing
	static constexpr float rock_ramp_time = 6.0f; // rocks reach their fastest rate this long after they start
	static constexpr glm::vec2 rock_interval{0.9f, 0.35f}; // seconds between rocks, first and fastest
	static constexpr float rock_fade_time = 3.0f; // seconds a landed rock stays before it's gone
	float ceiling_at(float x, UnderwaterPhysics const &physics) const;
	void update_rockfall(float elapsed, UnderwaterPhysics const &physics, Body2D &player, Camera2D const &camera);
	void draw_rockfall(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;
};
