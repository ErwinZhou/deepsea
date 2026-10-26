#pragma once

#include "Mode.hpp"
#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "BigFish.hpp"
#include "Camera2D.hpp"
#include "level1.hpp"

#include <optional>

struct PlayMode : Mode {
	enum class FishState { Normal, Kirby, Fat };

	PlayMode();
	bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	void update(float elapsed) override;
	void draw(glm::uvec2 const &drawable_size) override;

	static constexpr size_t fish_body = 0;
	static constexpr float swim_force = 9.0f;
	static constexpr float max_swim_speed = 3.5f;
	// drawn height of the small fish png and the size of its body for bumping into things
	static constexpr float fish_height = 1.6f;
	static constexpr float fish_radius = 0.55f;
	static constexpr float mouth_offset = 0.64f; // for enlarging the fish, subject to change
	static constexpr float suction_range = 3.6f;
	static constexpr float suction_half_angle = 35.0f;
	static constexpr float suction_force = 18.0f;
	static constexpr float capture_distance = 0.25f;
	static constexpr float spit_impulse = 14.0f;
	static constexpr float spit_clearance = 0.1f;
	UnderwaterPhysics physics;
	Camera2D camera;
	glm::vec2 facing{1.0f, 0.0f};
	FishState state = FishState::Normal; // initial state
	std::optional<size_t> carried_body;
	BigFish big_fish;
	Level1 level1;

private:
	void update_interaction();
	void spit();

	struct {
		bool up = false;
		bool down = false;
		bool left = false;
		bool right = false;
		bool space = false;
		bool space_pressed = false;
		bool suppress_suction = false;
	} input;
	double accumulated_time = 0.0;
	DrawSprites sprites;
	glm::vec2 background_size{0.0f};
	SpriteTexture background_texture;
	SpriteTexture fish_texture;
	SpriteTexture kirby_texture;
	SpriteTexture fat_texture;
	SpriteTexture stone_texture;
};
