#include "PlayMode.hpp"

#include "data_path.hpp"
#include "gl_errors.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

PlayMode::PlayMode()
	: fish_texture(data_path("small-fish.png")), stone_texture(data_path("stone.png")) {
	physics.bodies = {
		{.position = {-8.0f, 0.0f}, .radius = 1.7f, .drag = 1.8f, .current_scale = 0.0f},
		{.position = {-1.5f, 1.8f}, .radius = 0.64f},
		{.position = {5.0f, -1.5f}, .inverse_mass = 0.625f, .radius = 0.8f},
		{.position = {10.0f, 3.4f}, .inverse_mass = 1.25f, .radius = 0.576f}
	};
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &) {
	if (evt.type != SDL_EVENT_KEY_DOWN || evt.key.repeat) return false;
	if (evt.key.key == SDLK_ESCAPE) {
		Mode::set_current(nullptr);
		return true;
	}
	return false;
}

void PlayMode::update(float elapsed) {
	if (!std::isfinite(elapsed) || elapsed <= 0.0f) return;
	accumulated_time += std::min(elapsed, 0.1f);
	while (accumulated_time >= UnderwaterPhysics::fixed_step) {
		physics.step();
		accumulated_time -= UnderwaterPhysics::fixed_step;
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	if (drawable_size.x == 0 || drawable_size.y == 0) return;

	// fit the same 32 x 18 world into any window, with centered letterboxing
	glViewport(0, 0, drawable_size.x, drawable_size.y);
	glClearColor(0.015f, 0.025f, 0.03f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	float scale = std::min(float(drawable_size.x) / 32.0f, float(drawable_size.y) / 18.0f);
	GLsizei width = std::max(1, int(32.0f * scale));
	GLsizei height = std::max(1, int(18.0f * scale));
	glViewport((int(drawable_size.x) - width) / 2, (int(drawable_size.y) - height) / 2, width, height);
	glm::mat4 world_to_clip = glm::ortho(-16.0f, 16.0f, -9.0f, 9.0f);

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE); // a horizontal flip reverses the quad's winding
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// use the image's aspect ratio instead of stretching future replacement art
	auto sprite_size = [](SpriteTexture const &texture, float height) {
		return glm::vec2(height * float(texture.pixel_size.x) / float(texture.pixel_size.y), height);
	};
	float const stone_heights[] = {2.0f, 2.5f, 1.8f};
	for (size_t i = 0; i < 3; ++i) {
		auto const &body = physics.bodies[i + 1];
		if (body.active) sprites.draw(stone_texture, world_to_clip, body.position, sprite_size(stone_texture, stone_heights[i]));
	}
	// turns the fish to face right
	auto const &fish = physics.bodies[fish_body];
	if (fish.active) sprites.draw(fish_texture, world_to_clip, fish.position, sprite_size(fish_texture, 4.0f), 0.0f, true);

	glDisable(GL_BLEND);
	glViewport(0, 0, drawable_size.x, drawable_size.y);
	GL_ERRORS();
}
