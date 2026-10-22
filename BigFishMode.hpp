#pragma once

#include "Mode.hpp"
#include "BigFish.hpp"

#include <glm/glm.hpp>

//test scene. the big fish swims in from the left toward a ball in the middle
struct BigFishMode : Mode {
	BigFishMode();
	virtual ~BigFishMode();

	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	void restart();

	//the screen is ViewHeight units tall with (0,0) in the middle
	static constexpr float ViewHeight = 18.0f;
	float aspect = 16.0f / 9.0f;

	DrawSprites sprites;
	BigFish big_fish;

	//placeholder for something the big fish can eat
	struct Ball {
		glm::vec2 position = glm::vec2(0.0f);
		float radius = 0.5f;
		bool caught = false;
		bool eaten = false;
	} ball;
};
