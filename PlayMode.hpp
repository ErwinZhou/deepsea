#pragma once

#include "Mode.hpp"
#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "BigFish.hpp"
#include "Camera2D.hpp"
#include "Cave.hpp"
#include "Water.hpp"
#include "Sound.hpp"

#include <array>
#include <optional>

struct PlayMode : Mode {
	enum class FishState { Normal, Kirby, Fat };

	PlayMode();
	~PlayMode() override;
	bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	void update(float elapsed) override;
	void draw(glm::uvec2 const &drawable_size) override;

	static constexpr size_t fish_body = 0;
	static constexpr float swim_force = 9.0f;
	// tunes added inertia per unit of rock radius, 6.0 makes current rocks take about 3 to 4 times longer to reach the same top speed
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
	static constexpr float level_length = 120.0f;
	// the 36-unit corridor takes two 18-unit views to explore from floor to ceiling
	static constexpr float corridor_height = 36.0f;
	static constexpr float camera_height = 18.0f;
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
	// 12 completes about 95 percent of a turn in 0.25 seconds, 6 takes 0.5 seconds
	static constexpr float fish_turn_response = 12.0f;
	// current drawn heading in radians, 0 faces right, 1.57 faces up, 3.14 faces left
	float fish_visual_angle = 0.0f;
	double accumulated_time = 0.0;
	// seconds since the small fish was eaten or escaped
	float end_time = 0.0f;
	bool escaped = false;
	// seconds left of the rumble when the hanging rock comes down, and a clock to wobble it by
	float rumble = 0.0f;
	float clock = 0.0f;
	float scene_brightness = 1.0f;
	// seconds to fade in from black at the start, and out to black after being eaten
	static constexpr float fade_in_time = 1.5f;
	static constexpr float death_fade_time = 1.5f;
	// frozen time when a spat rock hits something
	float hit_stop = 0.0f;
	// 1 is fully lit, 0 is dark. a jellyfish goes dark over jelly_fade_time seconds when the big fish reaches it
	std::array< float, 4 > jelly_light = {1.0f, 1.0f, 1.0f, 1.0f};
	static constexpr float jelly_fade_time = 0.5f;
	glm::vec2 jelly_position(size_t i) const;
	// false after the big fish gets pinned, until it swims back up to the edge of the view
	bool leashed = true;
	// the big fish wakes once the view's left edge is dark_past_wall past the first wall, when the first jellyfish's light has faded.
	// then a quiet moment, the roar, and it arrives. warning_time is below 0 before it wakes
	float warning_time = -1.0f;
	static constexpr float dark_past_wall = -1.0f;
	static constexpr float warning_quiet = 0.5f;
	// 10 seconds from the warning starting to the fish entering
	static constexpr float warning_length = 10.0f;
	// 0 before and during the quiet moment, rising to 1 as the big fish arrives
	float warning_amount() const;

	// the deep water sound, the big fish's breathing, and the roar during a charge
	std::shared_ptr< Sound::PlayingSample > ambient;
	std::shared_ptr< Sound::PlayingSample > presence;
	std::shared_ptr< Sound::PlayingSample > charge_roar;
	// the deep water sound drops while the big fish wakes, so the roar stands out
	static constexpr float ambient_quiet_volume = 0.25f;
	static constexpr float ambient_fade_down = 0.3f;
	static constexpr float ambient_fade_up = 8.0f;
	// the first roar leans left, where -1 is the left ear only. the charge roar starts quiet and gets louder
	static constexpr float far_roar_pan = -0.5f;
	static constexpr float charge_roar_start_volume = 0.3f;
	static constexpr float charge_roar_fade_in = 3.0f;
	static constexpr float charge_roar_fade_out = 0.5f;
	// distance at which each sound is half as loud. the listener sits listener_depth in front of the screen
	static constexpr float listener_depth = 16.0f;
	static constexpr float roar_reach = 60.0f;
	static constexpr float rumble_reach = 15.0f;
	static constexpr float presence_reach = 10.0f;
	static constexpr float rock_reach = 10.0f;
	static constexpr float rock_miss_volume = 0.5f;
	DrawSprites sprites;
	Water water;
	Water::Look water_look;
	SpriteTexture cave_texture;
	SpriteTexture fish_texture;
	SpriteTexture kirby_texture;
	SpriteTexture fat_texture;
	SpriteTexture stone_texture;
	SpriteTexture cavern_texture;
	SpriteTexture background_texture;
	SpriteTexture jellyfish_texture;
};
