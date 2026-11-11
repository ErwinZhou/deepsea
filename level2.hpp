#pragma once

#include "Camera2D.hpp"
#include "UnderwaterPhysics.hpp"
#include "Water.hpp"

#include <cstddef>

struct Level2 {
	void setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body);
	void update(float elapsed);
	void prepare_water(Water::Look &look) const;

private:
	static constexpr float level_length = 170.0f;
	static constexpr float corridor_height = 32.0f;
	static constexpr float camera_height = 24.0f;
	static constexpr float entrance_distance = 12.0f;
	static constexpr float scene_brightness = 1.0f;
	float clock = 0.0f;
};
