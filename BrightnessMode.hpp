#pragma once

#include "DrawSprites.hpp"
#include "Mode.hpp"

#include <functional>
#include <optional>
#include <string>

struct BrightnessMode : Mode {
	static constexpr int default_brightness = 50;
	static float display_gain(int brightness);
	static std::optional<int> load(std::string const &filename);

	BrightnessMode(std::string const &filename, std::optional<int> brightness, std::function<void(int)> resume);
	bool handle_event(SDL_Event const &event, glm::uvec2 const &window_size) override;
	void draw(glm::uvec2 const &drawable_size) override;

private:
	std::string filename;
	std::optional<int> previous_brightness;
	std::function<void(int)> resume;
	int brightness;
	bool dragging = false;
	DrawSprites sprites;
	Water water;
	SpriteTexture sample_texture;
	SpriteTexture fish_texture;
};
