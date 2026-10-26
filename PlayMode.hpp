#pragma once

#include "Mode.hpp"
#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "BigFish.hpp"
#include "Camera2D.hpp"
#include "Cave.hpp"

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
	// how far in front of the small fish its mouth is
	static constexpr float mouth_offset = 0.64f;
	static constexpr float suction_range = 3.6f;
	static constexpr float suction_half_angle = 60.0f;
	// a rock this close to the mouth gets pulled in from any side
	static constexpr float suction_any_side = 1.8f;
	static constexpr float suction_force = 18.0f;
	static constexpr float capture_distance = 0.25f;
	static constexpr float spit_impulse = 14.0f;
	static constexpr float spit_clearance = 0.1f;
	// the level is a cave corridor no taller than the big fish's open mouth, so there is no getting around it
	static constexpr float level_length = 120.0f;
	static constexpr float corridor_height = 13.0f;
	UnderwaterPhysics physics;
	Camera2D camera;
	glm::vec2 facing{1.0f, 0.0f};
	FishState state = FishState::Normal;
	std::optional<size_t> carried_body;
	BigFish big_fish;
	Cave cave;

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
	// seconds since the small fish was eaten or escaped
	float end_time = 0.0f;
	bool escaped = false;
	// seconds left of the rumble when the hanging rock comes down, and a clock to wobble it by
	float rumble = 0.0f;
	float clock = 0.0f;
	// a moment of frozen time when a spat rock hits something, so the hit lands
	float hit_stop = 0.0f;
	// false after the big fish gets pinned, until it swims back up to the edge of the view
	bool leashed = true;
	// seconds since the first wall broke, below 0 before then. the big fish arrives after a warning
	float warning_time = -1.0f;
	// a quiet moment after the wall breaks, then the cave trembles and dust falls until the big fish arrives
	static constexpr float warning_quiet = 1.5f;
	static constexpr float warning_length = 4.5f;
	// 0 before and during the quiet moment, rising to 1 as the big fish arrives
	float warning_amount() const;
	DrawSprites sprites;
	SpriteTexture cave_texture;
	SpriteTexture fish_texture;
	SpriteTexture kirby_texture;
	SpriteTexture fat_texture;
	SpriteTexture stone_texture;
};
