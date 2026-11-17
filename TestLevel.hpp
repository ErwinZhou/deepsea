#pragma once

#include "Camera2D.hpp"
#include "DrawSprites.hpp"
#include "Tentacles.hpp"
#include "UnderwaterPhysics.hpp"
#include "Water.hpp"

#include <cstddef>
#include <array>
#include <vector>

struct TestLevel {
	TestLevel();
	void setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body);
	void update(float elapsed, UnderwaterPhysics const &physics, size_t player_body);
	void push_flashlight(UnderwaterPhysics &physics, size_t player_body, glm::vec2 const &movement, float elapsed);
	void check_puzzle_hits(UnderwaterPhysics &physics);
	bool restart_requested() const;
	void prepare_water(Water::Look &look) const;
	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics) const;
	float fade_amount() const;

private:
	static constexpr float camera_height = 24.0f;
	static constexpr float tunnel_width = 31.0f;
	static constexpr float key_height = 1.6f;

	static constexpr float flashlight_height = 4.8f;
	static constexpr float inside_flashlight_height = 2.0f;
	static constexpr float inside_flashlight_range = 35.0f;
	static constexpr float flashlight_burial_depth = 0.3f; //pivot depth below floor
	static constexpr float flashlight_initial_tilt = 20.0f; //degrees left from upward
	static constexpr glm::vec2 flashlight_tilt_limits{-35.0f, 45.0f}; //right and left tilt limits
	static constexpr float flashlight_turn_speed = 55.0f; //degrees per second when pushing near the lens
	static constexpr float flashlight_beam_range = 65.0f; //maximum forward reach before wall clipping
	static constexpr float flashlight_beam_half_width = 0.75f;
	static constexpr float flashlight_spill_radius = 4.0f; //extra lateral reach of soft light
	static constexpr float flashlight_beam_strength = 2.4f;
	static constexpr glm::vec2 flashlight_base_size{2.7f, 1.3f}; //drawn rock base width and height
	static constexpr glm::vec2 flashlight_base_collision_size{1.8f, 0.35f}; //collision width and height above floor

	static constexpr float scene_brightness = 1.0f;
	//seconds to fade in from black at the start
	static constexpr float fade_in_time = 1.5f;
	//seconds to fade to black and retry after the player dies
	static constexpr float death_fade_time = 1.5f;
	static constexpr float restart_delay = 3.0f;
	SpriteTexture tunnel_texture;
	SpriteTexture key_texture;
	SpriteTexture flashlight_texture;
	SpriteTexture flashlight_base_texture;
	SpriteTexture chest_texture;
	SpriteTexture keycard_texture;
	SpriteTexture scanner_locked_texture;
	SpriteTexture scanner_unlocked_texture;
	SpriteTexture rectangle{glm::u8vec4(255)};
	Tentacles tentacles;
	StaticBox2D chest;
	StaticBox2D scanner;
	glm::vec2 chest_center{0.0f};
	glm::vec2 scanner_center{0.0f};
	size_t key_body = 0, card_body = 0;
	bool chest_open = false;
	float ejection_time = -1.0f;

	void setup_puzzle(UnderwaterPhysics &physics);
	std::vector<unsigned char> tunnel_alpha; //opacity for light blocking, rows run downward
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
	glm::vec2 tunnel_center{0.0f};
	glm::vec2 tunnel_size{0.0f};
	void setup_tunnel(UnderwaterPhysics &physics);
	float clock = 0.0f;
	float death_time = 0.0f;
};
