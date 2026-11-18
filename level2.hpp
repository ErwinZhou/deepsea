#pragma once

#include "Camera2D.hpp"
#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "Water.hpp"

#include <cstddef>
#include <array>
#include <optional>
#include <vector>

struct Level2 {
	Level2();
	std::optional<size_t> setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body,
		std::optional<Body2D> const &belly_contents);
	void update(float elapsed, UnderwaterPhysics const &physics, size_t player_body);
	void push_flashlight(UnderwaterPhysics &physics, size_t player_body, glm::vec2 const &movement, float elapsed);
	bool restart_requested() const;
	glm::vec3 prepare_water(Water::Look &look, UnderwaterPhysics const &physics, size_t player_body,
		std::optional<size_t> carried_body) const;
	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics) const;
	float fade_amount() const;

private:
	static constexpr float level_length = 170.0f;
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
	static constexpr float key_end_inset = 1.5f;
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
	SpriteTexture tunnel_texture;
	std::vector<unsigned char> tunnel_alpha; //opacity for light blocking, rows run downward
	SpriteTexture key_texture;
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
	void push_flashlight(Flashlight &flashlight, UnderwaterPhysics &physics, Body2D &player,
		glm::vec2 const &movement, float elapsed);
	glm::vec2 key_position{0.0f};
	glm::vec2 tunnel_center{0.0f};
	glm::vec2 tunnel_size{0.0f};
	void setup_tunnel(UnderwaterPhysics &physics);
	float clock = 0.0f;
	float death_time = 0.0f;
};
