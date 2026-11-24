#pragma once

#include "Camera2D.hpp"
#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "Water.hpp"

#include <cstddef>

//the final passage after escaping the big fish
struct Level3 {
	Level3();
	void setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body);
	void update(float elapsed, UnderwaterPhysics const &physics, size_t player_body);
	void prepare_water(Water::Look &look) const;
	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;
	void draw_overlay(DrawSprites const &sprites, glm::mat4 const &world_to_clip,
		glm::vec2 const &view_center, glm::vec2 const &view_size) const;
	bool exit_reached() const { return escaped; }

private:
	static constexpr float level_length = 60.0f;
	static constexpr float corridor_height = 20.0f;
	static constexpr float scene_brightness = 1.0f;
	//entrance distance from the left wall
	static constexpr float entrance_distance = 6.0f;
	//same exit placement as level 1
	static constexpr float exit_width = 6.0f;
	static constexpr float exit_shift = 2.0f;
	static constexpr float exit_glow = 2.4f;

	static constexpr float fade_in_time = 1.5f;
	static constexpr float fade_out_time = 1.5f;
	//text fades in after the screen is black
	static constexpr float text_fade_time = 1.0f;
	static constexpr float text_height = 2.0f;

	SpriteTexture exit_texture;
	SpriteTexture fade_texture;
	StaticBox2D exit_box{};
	float clock = 0.0f;
	float end_time = 0.0f; //seconds since reaching the exit
	bool escaped = false;
};
