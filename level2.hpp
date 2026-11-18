#pragma once

#include "Camera2D.hpp"
#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "Water.hpp"

#include <cstddef>
#include <array>
#include <deque>
#include <optional>
#include <vector>

// small scary fish random appearing effects
struct SmallScaryFish {
	enum class State { Waiting, Arriving, Mimicking, Fleeing, Gone, Dormant, Turning, Chasing };
	State state = State::Waiting; // default to wait there first
	glm::vec2 position{0.0f};
	glm::vec2 velocity{0.0f};
	glm::vec2 arrival_offset{0.0f};
	float angle = 0.0f;
	float radius = 0.0f;
	float scale = 1.0f; //size relative to the player
};

struct Level2 {
	Level2(SpriteTexture const &normal_texture, float fish_height, float max_swim_speed);
	std::optional<size_t> setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body,
		std::optional<Body2D> const &belly_contents);
	void update(float elapsed, UnderwaterPhysics &physics, Camera2D const &camera, size_t player_body,
		glm::vec2 const &player_movement, float player_angle);
	void update_worm(UnderwaterPhysics &physics, size_t player_body, glm::vec2 previous_position, float elapsed);
	void push_flashlight(UnderwaterPhysics &physics, size_t player_body, glm::vec2 const &movement, float elapsed);
	bool restart_requested() const;
	glm::vec3 prepare_water(Water::Look &look, UnderwaterPhysics const &physics, size_t player_body,
		std::optional<size_t> carried_body) const;
	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics) const;
	float fade_amount() const;
	glm::vec2 arrival_shake() const;

	void react_to_suction(glm::vec2 const &mouth, glm::vec2 const &facing, float range, float cone_cos, float any_side);

private:
	//place the upper tunnel opening about two thirds through the level
	static constexpr float level_length = 320.0f;
	//the corridor scrolls vertically inside the tighter view
	static constexpr float corridor_height = 80.0f;
	static constexpr float camera_height = 24.0f;
	//distance from the cave's left end to the player's starting position
	static constexpr float entrance_distance = 12.0f;
	//place the tunnel's left edge one third of the way across the cave
	static constexpr float tunnel_start_fraction = 1.0f / 3.0f;
	//leave room through the tunnel's narrow turns
	static constexpr float tunnel_width = 127.0f;
	//drawn key height and its distance below the tunnel's upper opening
	static constexpr float key_height = 1.6f;
	static constexpr float key_radius = 0.6f; //key collision radius
	static constexpr float key_end_inset = 3.2f;
	static constexpr float key_left_offset = 0.9f;
	static constexpr float coral_size = 2.4f; //guide coral size
	static constexpr float coral_base_inset = 0.08f; //bury the flat edge slightly
	static constexpr float coral_glow = 1.0f; //light reaches the key
	static constexpr float coral_light_offset = 0.4f; //light sits outside the rock

	static constexpr float flashlight_height = 4.8f; //flashlight height
	static constexpr float inside_flashlight_height = 2.0f; //smaller lamp inside the tunnel
	static constexpr float inside_flashlight_range = 35.0f; //inside beam reach
	static constexpr float flashlight_entrance_offset = -5.0f; //horizontal offset from entrance, negative is on left
	static constexpr float flashlight_burial_depth = 0.3f; //pivot depth below floor
	static constexpr float flashlight_initial_tilt = 20.0f; //degrees left from upward
	static constexpr glm::vec2 flashlight_tilt_limits{-35.0f, 45.0f}; //right and left tilt limits
	static constexpr float flashlight_turn_speed = 55.0f; //degrees per second when pushing near the lens
	static constexpr float flashlight_beam_range = 65.0f; //maximum forward reach before wall clipping
	static constexpr float flashlight_beam_half_width = 0.75f; //bright core half-width
	static constexpr float flashlight_spill_radius = 4.0f; //extra lateral reach of soft light
	static constexpr float flashlight_beam_strength = 2.4f; //brightness multiplier
	static constexpr glm::vec2 flashlight_base_size{2.7f, 1.3f}; //drawn rock base width and height
	static constexpr glm::vec2 flashlight_base_collision_size{1.8f, 0.35f}; //collision width and height above floor

	static constexpr float scene_brightness = 1.0f;
	//seconds to fade in from black at the start
	static constexpr float fade_in_time = 1.5f;
	//seconds to fade to black and retry after the player dies
	static constexpr float death_fade_time = 1.5f;
	static constexpr float restart_delay = 3.0f;
	//base light strength and how far it rises and falls during the slow pulse
	static constexpr float jelly_glow = 1.2f;
	static constexpr float jelly_glow_pulse = 0.12f;
	//let the jellyfish drift visually around its physics position
	glm::vec2 jelly_position(Body2D const &jelly) const;
	//only present if the player carried the jellyfish through the exit
	std::optional<size_t> jelly_body;
	std::optional<SpriteTexture> jellyfish_texture;
	float clock = 0.0f; //seconds since entering level 2
	float death_time = 0.0f; //seconds since the player died

	// small scary fish relevant constants
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
	SmallScaryFish small_fish;
	float next_mimic_time = 0.0f; //level time when the next fish may appear
	bool mimic_encounters_finished = false; //stop single fish for this run after nearing the seven fish
	glm::vec2 previous_player_position{0.0f};
	bool small_fish_path_clear(glm::vec2 const &from, glm::vec2 const &to,
		UnderwaterPhysics const &physics, float radius) const;
	void update_small_fish(float elapsed, UnderwaterPhysics const &physics, Camera2D const &camera,
		Body2D const &player, glm::vec2 const &player_step, glm::vec2 const &player_movement, float player_angle);
	
	// small scary fish group (with eyes)
	static constexpr size_t fish_group_count = 7; // small fish appears in group of 7 because the octopus has 8 hands
	static constexpr float fish_group_depth_fraction = 0.25f; // the group appear in about 25% below the cave top
	static constexpr float fish_group_trigger_distance = 4.0f; // once the player appears near 4 units to any of the small fish, trigger turn around and chasing
	static constexpr float fish_group_turn_time = 0.15f; // turn time and transformation
	static constexpr float fish_group_speed_multiplier = 0.7f; // default to be 0.7 of the player speed
	static constexpr float fish_group_separation_distance = 1.4f; // if two fish <1.4 unit, seperate each other
	static constexpr float fish_group_separation_weight = 1.5f; // the force to control seperatance
	static constexpr glm::vec2 fish_group_scale_range{1.2f, 2.0f}; //min and max size relative to the player
	static constexpr float fish_group_position_offset = 0.9f; //max random shift on each axis, in units, position them randomly
	std::array<SmallScaryFish, fish_group_count> fish_group{};
	SpriteTexture fish_eyes_texture;
	const float fish_group_speed;
	bool fish_group_triggered = false;
	float fish_group_turn_remaining = 0.0f;

	void update_fish_group(float elapsed, UnderwaterPhysics &physics, size_t player_body, glm::vec2 const &player_step);
	void draw_fish(DrawSprites const &sprites, glm::mat4 const &world_to_clip,
		SmallScaryFish const &fish, SpriteTexture const &texture) const;

	SpriteTexture tunnel_texture;
	std::vector<unsigned char> tunnel_alpha; //opacity for light blocking, rows run downward
	SpriteTexture key_texture;

	struct Worm {
		SpriteTexture texture;
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

	SpriteTexture coral_texture;
	struct Coral {
		glm::vec2 position{0.0f};
		float angle = 0.0f;
	};
	std::array<Coral, 2> corals{};
	std::optional<size_t> key_body;
	SpriteTexture flashlight_texture;
	SpriteTexture flashlight_base_texture;
	struct Flashlight {
		glm::vec2 anchor{0.0f};
		glm::vec2 center{0.0f};
		float height = flashlight_height;
		float range = flashlight_beam_range;
		float tilt = flashlight_initial_tilt;
		bool turned = false;
		Water::Beam light;
	};
	std::array<Flashlight, 2> flashlights{};
	void setup_flashlight(UnderwaterPhysics &physics);
	void aim_flashlight(Flashlight &flashlight, UnderwaterPhysics const &physics);
	void push_flashlight(Flashlight &flashlight, UnderwaterPhysics &physics, Body2D &body,
		glm::vec2 const &movement, float elapsed);
	glm::vec2 key_position{0.0f};
	glm::vec2 tunnel_center{0.0f};
	glm::vec2 tunnel_size{0.0f};
	void setup_tunnel(UnderwaterPhysics &physics);
	static constexpr float shake_duration = 12.373f; //length of the big fish arrival roar
	static constexpr float shake_strength = 0.08f;
	float shake_left = 0.0f;
};
