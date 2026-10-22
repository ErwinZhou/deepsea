#include "PlayMode.hpp"

#include "DrawLines.hpp"
#include "GL.hpp"
#include "gl_errors.hpp"

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &) {
	if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.key == SDLK_ESCAPE) {
		Mode::set_current(nullptr);
		return true;
	}
	return false;
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	if (drawable_size.x == 0 || drawable_size.y == 0) return;

	glClearColor(0.025f, 0.04f, 0.05f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	float aspect = float(drawable_size.x) / float(drawable_size.y);
	DrawLines lines(glm::mat4(
		1.0f / aspect, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	));
	float h = 0.09f;
	lines.draw_text("DeepSea", glm::vec3(-aspect + 0.1f, 0.7f, 0.0f),
		glm::vec3(h, 0.0f, 0.0f), glm::vec3(0.0f, h, 0.0f));
	lines.draw_text("Movement sandbox setup - Escape to quit",
		glm::vec3(-aspect + 0.1f, -0.85f, 0.0f),
		glm::vec3(h * 0.5f, 0.0f, 0.0f), glm::vec3(0.0f, h * 0.5f, 0.0f));
	GL_ERRORS();
}
