#include "BrightnessMode.hpp"

#include "DrawLines.hpp"
#include "data_path.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

namespace {
constexpr glm::vec2 MenuSize(960.0f, 540.0f);
constexpr float SliderLeft = 364.0f;
constexpr float SliderRight = 596.0f;
constexpr float SliderY = 150.0f;

float menu_scale(glm::uvec2 size) {
	return std::min(float(size.x) / MenuSize.x, float(size.y) / MenuSize.y);
}
}

float BrightnessMode::display_gain(int brightness) {
	float amount = float(brightness - default_brightness) / float(default_brightness);
	return std::exp2(amount * (amount > 0.0f ? 2.0f : 1.0f));
}

std::optional<int> BrightnessMode::load(std::string const &filename) {
	if (filename.empty()) return std::nullopt;
	std::ifstream input(std::filesystem::path(std::u8string(filename.begin(), filename.end())));
	int value;
	if (input >> value && value >= 0 && value <= 100 && (input >> std::ws).eof() && !input.bad()) return value;
	return std::nullopt;
}

BrightnessMode::BrightnessMode(std::string const &filename, std::optional<int> brightness, std::function<void(int)> resume)
	: filename(filename), previous_brightness(brightness), resume(std::move(resume)),
	  brightness(brightness.value_or(default_brightness)),
	  sample_texture(data_path("brightness.png")), fish_texture(data_path("normal.png")) {
}

bool BrightnessMode::handle_event(SDL_Event const &event, glm::uvec2 const &window_size) {
	auto slider_value = [&](float x) {
		brightness = int(std::round(100.0f * std::clamp((x - SliderLeft) / (SliderRight - SliderLeft), 0.0f, 1.0f)));
	};
	if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
		(event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)) {
		dragging = false;
	}
	if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
		float scale = menu_scale(window_size);
		if (scale <= 0.0f) return true;
		glm::vec2 point(event.button.x, float(window_size.y) - event.button.y);
		point = (point - 0.5f * glm::vec2(window_size)) / scale + 0.5f * MenuSize;
		dragging = std::abs(point.y - SliderY) <= 16.0f && point.x >= SliderLeft - 12.0f && point.x <= SliderRight + 12.0f;
		if (dragging) slider_value(point.x);
		return true;
	}
	if (event.type == SDL_EVENT_MOUSE_MOTION && dragging) {
		float scale = menu_scale(window_size);
		if (scale > 0.0f) slider_value((event.motion.x - 0.5f * float(window_size.x)) / scale + 0.5f * MenuSize.x);
		return true;
	}
	if (event.type != SDL_EVENT_KEY_DOWN) return false;
	switch (event.key.key) {
	case SDLK_LEFT: brightness = std::max(0, brightness - 5); break;
	case SDLK_RIGHT: brightness = std::min(100, brightness + 5); break;
	case SDLK_R: brightness = default_brightness; break;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		if (!filename.empty()) {
			std::string temporary = filename + ".tmp";
			std::ofstream output(std::filesystem::path(std::u8string(temporary.begin(), temporary.end())));
			output << brightness << '\n';
			output.close();
			if (!output || !SDL_RenamePath(temporary.c_str(), filename.c_str())) {
				std::cerr << "Could not save brightness setting to '" << filename << "'\n";
				SDL_RemovePath(temporary.c_str());
			}
		}
		resume(brightness);
		break;
	case SDLK_ESCAPE:
		if (previous_brightness) resume(*previous_brightness);
		else Mode::set_current(nullptr);
		break;
	default: return false;
	}
	return true;
}

void BrightnessMode::draw(glm::uvec2 const &size) {
	float scale = menu_scale(size);
	if (scale <= 0.0f) return;
	glViewport(0, 0, size.x, size.y);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);

	glm::ivec2 origin(0.5f * (glm::vec2(size) - MenuSize * scale));
	glm::ivec2 extent = glm::max(glm::ivec2(1), glm::ivec2(MenuSize * scale));
	glViewport(origin.x, origin.y, extent.x, extent.y);
	glm::mat4 to_clip = glm::ortho(0.0f, MenuSize.x, 0.0f, MenuSize.y);

	//Only the three samples go through the same brightness curve as gameplay.
	water.begin(Water::Look{});
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glm::u8vec4 const shades[] = {{1, 1, 1, 24}, {1, 1, 1, 230}, {10, 10, 10, 255}};
	for (int i = 0; i < 3; ++i) {
		glm::vec2 center(290.0f + 190.0f * float(i), 312.0f);
		sprites.draw(sample_texture, to_clip, center, glm::vec2(136.0f), 0.0f, false, shades[i], 0.0f, DrawSprites::Emissive);
		sprites.draw(fish_texture, to_clip, center, glm::vec2(112.0f), 0.0f, false,
			glm::u8vec4(0, 0, 0, 255), 0.0f, DrawSprites::Emissive);
	}
	water.finish(display_gain(brightness), false);

	DrawLines lines(to_clip);
	glm::u8vec4 const white(230, 230, 230, 255);
	auto text = [&](char const *label, float x, float y, float height) {
		size_t first = lines.attribs.size();
		glm::vec3 end;
		lines.draw_text(label, glm::vec3(0.0f, y, 0.0f),
			glm::vec3(height, 0.0f, 0.0f), glm::vec3(0.0f, height, 0.0f), white, &end);
		for (size_t i = first; i < lines.attribs.size(); ++i) lines.attribs[i].Position.x += x - 0.5f * end.x;
	};
	text("Brightness", 480.0f, 458.0f, 28.0f);
	text("Not visible", 290.0f, 397.0f, 19.0f);
	text("Barely visible", 480.0f, 397.0f, 19.0f);
	text("Clearly visible", 670.0f, 397.0f, 19.0f);
	text("Adjust screen brightness", 480.0f, 194.0f, 22.0f);
	text("Press Enter to continue", 480.0f, 95.0f, 24.0f);

	float marker = glm::mix(SliderLeft, SliderRight, float(brightness) / 100.0f);
	lines.draw(glm::vec3(SliderLeft, SliderY, 0.0f), glm::vec3(SliderRight, SliderY, 0.0f), glm::u8vec4(96, 96, 96, 255));
	lines.draw(glm::vec3(marker, SliderY - 8.0f, 0.0f), glm::vec3(marker, SliderY + 8.0f, 0.0f), white);
}
