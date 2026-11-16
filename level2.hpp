#pragma once

#include "Camera2D.hpp"
#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "Water.hpp"

#include <cstddef>
#include <array>
#include <optional>

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
	bool restart_requested() const;
	glm::vec3 prepare_water(Water::Look &look, UnderwaterPhysics const &physics, size_t player_body,
		std::optional<size_t> carried_body) const;
	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics) const;
	float fade_amount() const;

	void react_to_suction(glm::vec2 const &mouth, glm::vec2 const &facing, float range, float cone_cos, float any_side);

private:
	static constexpr float level_length = 170.0f;
	//the corridor scrolls vertically inside the tighter view
	static constexpr float corridor_height = 32.0f;
	static constexpr float camera_height = 24.0f;
	//distance from the cave's left end to the player's starting position
	static constexpr float entrance_distance = 12.0f;
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
};
