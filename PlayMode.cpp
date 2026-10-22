#include "PlayMode.hpp"

#include "data_path.hpp"
#include "gl_errors.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

PlayMode::PlayMode()
	: fish_texture(data_path("small-fish.png")), stone_texture(data_path("stone.png")) {
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &) {
	if (evt.type != SDL_EVENT_KEY_DOWN || evt.key.repeat) return false;
	if (evt.key.key == SDLK_ESCAPE) {
		Mode::set_current(nullptr);
		return true;
	}
	return false;
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
	for (size_t i = 0; i < stone_positions.size(); ++i) {
		sprites.draw(stone_texture, world_to_clip, stone_positions[i], sprite_size(stone_texture, stone_heights[i]));
	}
	// the supplied fish faces left, flip it so the initial mouth points right
	sprites.draw(fish_texture, world_to_clip, fish_position, sprite_size(fish_texture, 4.0f), 0.0f, true);

	glDisable(GL_BLEND);
	glViewport(0, 0, drawable_size.x, drawable_size.y);
	GL_ERRORS();
}
