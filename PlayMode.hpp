#pragma once

#include "Mode.hpp"
#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "Camera2D.hpp"
#include "level1.hpp"
#include "level2.hpp"
#include "CaveBackground.hpp"
#include "Water.hpp"

#include <cstddef>
#include <optional>
#include <string>

struct PlayMode : Mode {
	enum class FishState { Normal, Kirby, Fat };
	enum class LevelId { One = 1, Two = 2 };

	explicit PlayMode(LevelId start_level = LevelId::One, std::optional<Body2D> const &belly_contents = std::nullopt,
		std::string const &checkpoint_file = "");
	~PlayMode() override = default;
	bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	void update(float elapsed) override;
	void draw(glm::uvec2 const &drawable_size) override;

	static constexpr size_t fish_body = 0;
	static constexpr float swim_force = 9.0f;
	//larger swallowed rocks make the fish accelerate more slowly
	static constexpr float carried_inertia_per_radius = 6.0f;
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
	UnderwaterPhysics physics;
	Camera2D camera;
	glm::vec2 facing{1.0f, 0.0f};
	FishState state = FishState::Normal;
	std::optional<size_t> carried_body;
	LevelId const level_id;
	std::optional<Level1> level1;
	std::optional<Level2> level2;

private:
	void update_interaction();
	void spit();
	std::optional<Body2D> carried_contents() const;
	//empty for explicit level previews so they cannot overwrite normal progress
	std::string checkpoint_file;
	//retry restores the entrance checkpoint rather than the player's later inventory
	std::optional<Body2D> entry_belly_contents;

	struct {
		bool up = false;
		bool down = false;
		bool left = false;
		bool right = false;
		bool space = false;
		bool space_pressed = false;
		bool suppress_suction = false;
	} input;
	//smooth visual turns without delaying movement
	static constexpr float fish_turn_response = 12.0f;
	float fish_visual_angle = 0.0f;
	double accumulated_time = 0.0;
	//pause simulation briefly after an impact
	float hit_stop = 0.0f;
	static constexpr float transition_fade_time = 1.5f;
	//negative until the exit starts the fade to the next level
	float transition_time = -1.0f;
	static constexpr float listener_depth = 16.0f;
	DrawSprites sprites;
	Water water;
	Water::Look water_look;
	SpriteTexture fish_texture;
	SpriteTexture kirby_texture;
	SpriteTexture fat_texture;
	SpriteTexture fat_glow_texture;
	SpriteTexture stone_texture;
	CaveBackground background;
};
