#pragma once

#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "BigFish.hpp"
#include "Camera2D.hpp"
#include "Sound.hpp"

#include <cstddef>
#include <optional>
#include <vector>

//the first cave and its scripted chase
struct Level1 {
	Level1();
	~Level1();

	//a rock wall across the whole corridor. spitting enough rocks into it breaks it
	struct Wall {
		float x = 0.0f;
		int hits_left = 0;
		StaticBox2D box;
		//seconds left of the shudder after a hit
		float shudder = 0.0f;
	};
	//teaches spitting before the big fish shows up
	Wall first_wall;
	//stands between the small fish and the way out
	Wall last_wall;

	//the cracked rock hanging from the ceiling. enough hits and it drops
	struct HangingRock {
		enum State { Hanging, Falling, Fallen } state = Hanging;
		int hits_left = 0;
		glm::vec2 center = glm::vec2(0.0f);
		glm::vec2 velocity = glm::vec2(0.0f);
		float angle = 0.0f;
		//true once the big fish has been held up by it, so it only works once
		bool used = false;
		float shudder = 0.0f;
	} hanging_rock;

	//the exit light sits inside the trigger's artwork
	float exit_x = 0.0f;
	StaticBox2D exit_box{};

	//exit and coral light sources
	std::vector< glm::vec3 > light_spots;
	glm::vec2 coral_position = glm::vec2(0.0f);
	//keep the coral and its light attached to the pillar
	void update_coral();
	//use fewer boxes for shadows than for collisions
	std::vector< StaticBox2D > light_blockers;

	struct Target {
		std::vector< glm::vec4 > parts;
		std::vector< glm::vec2 > cracks;
		glm::vec2 texel_size = glm::vec2(0.0f);
		glm::vec2 entry = glm::vec2(1.0f, 0.5f);
	};
	Target wall_targets[3];
	Target hanging_targets[3];

	//impacts from the last obstacle update
	struct Events {
		bool hit = false;
		//a spat rock knocked against something but not on its crack
		bool missed = false;
		bool broke = false;
		bool first_wall_broke = false;
		bool crashed = false;
		//play impact sounds at the last collision
		glm::vec2 at = glm::vec2(0.0f);
	} events;

	//chips knocked off by hits, and dust shaken down from the ceiling
	struct Particle {
		glm::vec2 position = glm::vec2(0.0f);
		glm::vec2 velocity = glm::vec2(0.0f);
		float size = 0.0f;
		float angle = 0.0f;
		float spin = 0.0f;
		float life = 0.0f;
		float max_life = 0.0f;
		//how fast it falls. chips drop fast, dust drifts down slowly
		float gravity = 0.0f;
	};
	std::vector< Particle > particles;
	//throw chips away from an impact
	void throw_chips(glm::vec2 const &at, glm::vec2 const &away, int count);
	//drop ceiling dust in the visible part of the cave
	void drop_ceiling_dust(float left, float right, float amount, float elapsed);

	//places everything in the corridor and adds the loose rocks to physics
	void setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body);
	//obstacle hits must be checked before physics resolves their contacts
	float before_physics(UnderwaterPhysics &physics, float elapsed);
	void check_keycard(UnderwaterPhysics &physics);
	void after_physics(UnderwaterPhysics &physics, Camera2D const &camera, std::optional<size_t> carried_body, float elapsed);
	bool restart_requested(UnderwaterPhysics const &physics) const;
	bool exit_reached() const;
	void stop_sounds(float fade);
	glm::vec2 shaken_center(glm::vec2 const &center, UnderwaterPhysics const &physics) const;
	glm::vec3 prepare_water(Water::Look &look, UnderwaterPhysics const &physics, std::optional<size_t> carried_body,
		glm::vec2 const &view_center, glm::vec2 const &view_size) const;
	void draw_distant(DrawSprites const &sprites, glm::mat4 const &world_to_clip, glm::vec2 const &view_center) const;
	void draw_overlay(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics,
		glm::vec2 const &view_center, glm::vec2 const &view_size) const;

	BigFish big_fish;
	//tutorial progress survives retries
	static inline bool has_swum = false;
	static inline bool has_sucked = false;
	static inline bool has_spat = false;
	//walls, the hanging rock and the way out
	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip, UnderwaterPhysics const &physics) const;
	//the pillar draws over the big fish
	void draw_foreground(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;
	glm::vec2 shake() const;
	int wall_stage(Wall const &wall) const;
	glm::vec2 wall_crack_position(Wall const &wall) const;

	glm::vec2 bounds_min = glm::vec2(0.0f);
	glm::vec2 bounds_max = glm::vec2(0.0f);
	//seconds before each body can count as hitting something again, so one bounce counts once
	std::vector< float > hit_cooldown;

	SpriteTexture wall_textures[3];
	SpriteTexture hanging_textures[3];
	SpriteTexture exit_texture;
	SpriteTexture coral_texture;
	SpriteTexture chip_texture;

private:
	void update_obstacles(UnderwaterPhysics &physics, float elapsed);
	size_t fish_body = 0;
	size_t keycard_body = 0;
	StaticBox2D machine;
	bool machine_on = false;
	SpriteTexture square{glm::u8vec4(255)};
	static constexpr float level_length = 170.0f;
	//the corridor scrolls vertically inside the tighter view
	static constexpr float corridor_height = 32.0f;
	static constexpr float camera_height = 24.0f;
	//seconds since the small fish was eaten
	float end_time = 0.0f;
	bool escaped = false;
	//rockfall shake duration and animation time
	float rumble = 0.0f;
	float clock = 0.0f;
	float scene_brightness = 1.0f;
	//seconds to fade in from black at the start, and out to black after being eaten
	static constexpr float fade_in_time = 1.5f;
	static constexpr float death_fade_time = 1.5f;
	static constexpr float suck_hint_range = 5.0f;
	static constexpr float hint_fade_time = 0.4f;
	static constexpr float hint_height = 0.6f;
	static constexpr float hint_above_fish = 1.3f;
	char const *wanted_hint(UnderwaterPhysics const &physics, std::optional<size_t> carried_body) const;
	char const *shown_hint = nullptr;
	float hint_amount = 0.0f;
	//carrying the jellyfish lights the way but leaves no room for a rock
	//design based on https://frictionalgames.com/2013-12-the-five-foundational-design-pillars-of-soma/
	//the light fades from 1 to 0 over jelly_fade_time once the big fish reaches a jellyfish left behind
	size_t jelly_body = 0;
	float jelly_light = 1.0f;
	static constexpr float jelly_fade_time = 0.5f;
	static constexpr float jelly_radius = 0.5f;
	static constexpr float jelly_drag = 3.0f;
	static constexpr float jelly_glow = 1.2f;
	static constexpr float jelly_glow_pulse = 0.12f;
	bool jelly_in_belly(std::optional<size_t> carried_body) const;
	glm::vec2 jelly_position(UnderwaterPhysics const &physics) const;
	//the big fish wakes once the view's left edge is dark_past_wall past the first wall
	//the near roar follows the distant pass after a quiet gap
	//warning_time is below 0 before it wakes
	float warning_time = -1.0f;
	static constexpr float dark_past_wall = -1.0f;
	static constexpr float warning_quiet = 0.5f;
	float warning_length = 0.0f;
	float far_roar_end = 0.0f;
	float near_roar_end = 0.0f;
	//time to read the cave after the distant pass and roar end
	static constexpr float near_roar_gap = 6.0f;
	//only part of the distant fish fits in the view as it passes
	static constexpr float far_begin = 1.0f;
	static constexpr float far_speed = 8.0f;
	static constexpr float far_start = 68.0f;
	static constexpr float far_end = -54.0f;
	static constexpr float far_height = -22.0f;
	//0 before and during the quiet moment, rising to 1 as the big fish arrives
	float warning_amount() const;
	float roar_amount(UnderwaterPhysics const &physics) const;
	//true while it passes far behind the cave
	bool far_passing() const;
	glm::vec2 far_hinge(glm::vec2 const &view_center) const;

	//sounds that continue between frames
	std::shared_ptr< Sound::PlayingSample > ambient;
	std::shared_ptr< Sound::PlayingSample > presence;
	std::shared_ptr< Sound::PlayingSample > far_roar;
	std::shared_ptr< Sound::PlayingSample > near_roar;
	std::shared_ptr< Sound::PlayingSample > charge_roar;
	std::vector<std::shared_ptr<Sound::PlayingSample>> effects;
	void stop_roars(float fade);
	//quiet the ambience during the chase and restore it while the big fish is pinned
	static constexpr float ambient_quiet_volume = 0.15f;
	static constexpr float ambient_fade_down = 0.3f;
	static constexpr float ambient_fade_up = 8.0f;
	bool was_held_up = false;
	//place the distant roar behind the cave so it sounds quieter and muffled
	static constexpr float far_roar_depth = 40.0f;
	static constexpr float far_roar_volume = 0.75f;
	static constexpr float far_roar_reach = 24.0f;
	static constexpr float near_roar_volume = 1.2f;
	static constexpr float near_roar_reach = 90.0f;
	static constexpr float charge_roar_start_volume = 0.45f;
	static constexpr float charge_roar_fade_in = 3.0f;
	static constexpr float charge_roar_fade_out = 0.5f;
	//distance at which each sound is half as loud
	static constexpr float roar_reach = 60.0f;
	static constexpr float rumble_reach = 15.0f;
	static constexpr float presence_reach = 20.0f;
	static constexpr float rock_reach = 10.0f;
	static constexpr float rock_miss_volume = 0.5f;
	SpriteTexture fade_texture;
	SpriteTexture jellyfish_texture;
};
