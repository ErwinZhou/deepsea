#pragma once

#include "Camera2D.hpp"
#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "Water.hpp"

#include <cstddef>
#include <optional>

struct SmallScaryFish {
	enum class State { Waiting, Arriving, Mimicking, Fleeing, Gone };
	State state = State::Waiting;
	glm::vec2 position{0.0f};
	glm::vec2 velocity{0.0f};
	glm::vec2 arrival_offset{0.0f};
	float angle = 0.0f;
	float radius = 0.0f;
};

struct Level2 {
	Level2(SpriteTexture const &normal_texture, float fish_height, float max_swim_speed);
	std::optional<size_t> setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body,
		std::optional<Body2D> const &belly_contents);
	void update(float elapsed, UnderwaterPhysics const &physics, Camera2D const &camera, size_t player_body,
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
	float clock = 0.0f;
	float death_time = 0.0f;

	static constexpr float mimic_start_delay = 3.0f;
	static constexpr float mimic_screen_fraction = 1.0f / 3.0f;
	static constexpr float mimic_speed_multiplier = 3.0f;
	static constexpr float mimic_approach_distance = 4.0f;
	static constexpr float mimic_approach_cos = 0.6f;
	static constexpr float mimic_offscreen_margin = 1.0f;
	SpriteTexture const &small_fish_texture;
	const float small_fish_height;
	const float small_fish_fast_speed;
	SmallScaryFish small_fish;
	glm::vec2 previous_player_position{0.0f};
	bool small_fish_path_clear(glm::vec2 const &from, glm::vec2 const &to, UnderwaterPhysics const &physics) const;
	void update_small_fish(float elapsed, UnderwaterPhysics const &physics, Camera2D const &camera,
		Body2D const &player, glm::vec2 const &player_step, glm::vec2 const &player_movement, float player_angle);
};
