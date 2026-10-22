#pragma once

#include "Mode.hpp"

// Starting point for the fish movement sandbox; see docs/small-fish-movement-plan.md.
struct PlayMode : Mode {
	bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	void draw(glm::uvec2 const &drawable_size) override;
};
