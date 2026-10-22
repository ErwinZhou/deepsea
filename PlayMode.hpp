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
	UnderwaterPhysics physics;

private:
	double accumulated_time = 0.0;
	DrawSprites sprites;
	SpriteTexture fish_texture;
	SpriteTexture stone_texture;
};
