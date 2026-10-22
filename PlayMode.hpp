#pragma once

#include "Mode.hpp"
#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"

struct PlayMode : Mode {
	PlayMode();
	bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	void update(float elapsed) override;
	void draw(glm::uvec2 const &drawable_size) override;

	static constexpr size_t fish_body = 0;
	static constexpr float swim_force = 14.0f;
	static constexpr float max_swim_speed = 6.0f;
	UnderwaterPhysics physics;
	glm::vec2 facing{1.0f, 0.0f};

private:
	struct {
		bool up = false;
		bool down = false;
		bool left = false;
		bool right = false;
	} input;
	double accumulated_time = 0.0;
	DrawSprites sprites;
	SpriteTexture fish_texture;
	SpriteTexture stone_texture;
};
