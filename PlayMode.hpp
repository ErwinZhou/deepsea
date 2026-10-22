#pragma once

#include "Mode.hpp"
#include "DrawSprites.hpp"

#include <array>

struct PlayMode : Mode {
	PlayMode();
	bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	void draw(glm::uvec2 const &drawable_size) override;

	// world coordinates stay independent of the window and artwork resolution
	glm::vec2 fish_position = glm::vec2(-8.0f, 0.0f);
	std::array<glm::vec2, 3> stone_positions = {{
		glm::vec2(-1.5f, 1.8f), glm::vec2(5.0f, -1.5f), glm::vec2(10.0f, 3.4f)
	}};

private:
	DrawSprites sprites;
	SpriteTexture fish_texture;
	SpriteTexture stone_texture;
};
