//the key handling here is based on the PlayMode.cpp that ships with the base code.
#include "BigFishMode.hpp"

#include "DrawLines.hpp"
#include "GL.hpp"
#include "gl_errors.hpp"

#include <algorithm>
#include <cmath>

BigFishMode::BigFishMode() {
	restart();
}

BigFishMode::~BigFishMode() {
}

void BigFishMode::restart() {
	//put it just off the left side of the screen
	float left_edge = -0.5f * ViewHeight * aspect;
	big_fish.reset(glm::vec2(left_edge - 0.5f * big_fish.size, 0.0f));
	ball = Ball();
}

bool BigFishMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	if (evt.type == SDL_EVENT_KEY_DOWN) {
		if (evt.key.key == SDLK_1) {
			big_fish.rock();
			return true;
		} else if (evt.key.key == SDLK_2) {
			big_fish.stun();
			return true;
		} else if (evt.key.key == SDLK_R) {
			restart();
			return true;
		}
	}
	return false;
}

void BigFishMode::update(float elapsed) {
	big_fish.update(elapsed);

	if (!ball.eaten) {
		if (big_fish.can_suck(ball.position)) ball.caught = true;
		//keep pulling the ball in once it's caught, even if the mouth closes
		if (ball.caught) {
			constexpr float SuckSpeed = 6.0f;
			glm::vec2 to_mouth = big_fish.position - ball.position;
			float dist = glm::length(to_mouth);
			if (dist <= ball.radius) {
				ball.eaten = true;
			} else {
				ball.position += to_mouth / dist * std::min(dist, SuckSpeed * elapsed);
			}
		}
	}
}

void BigFishMode::draw(glm::uvec2 const &drawable_size) {
	aspect = float(drawable_size.x) / float(drawable_size.y);

	glClearColor(0.08f, 0.08f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_DEPTH_TEST);

	float sy = 2.0f / ViewHeight;
	glm::mat4 world_to_clip = glm::mat4(
		sy / aspect, 0.0f, 0.0f, 0.0f,
		0.0f, sy, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f
	);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	big_fish.draw(sprites, world_to_clip);
	glDisable(GL_BLEND);

	{ //draw the ball as a circle
		DrawLines lines(world_to_clip);
		if (!ball.eaten) {
			constexpr int Segments = 24;
			for (int i = 0; i < Segments; ++i) {
				float a0 = float(i) / Segments * 2.0f * float(M_PI);
				float a1 = float(i + 1) / Segments * 2.0f * float(M_PI);
				lines.draw(
					glm::vec3(ball.position + ball.radius * glm::vec2(std::cos(a0), std::sin(a0)), 0.0f),
					glm::vec3(ball.position + ball.radius * glm::vec2(std::cos(a1), std::sin(a1)), 0.0f),
					glm::u8vec4(0xff, 0xff, 0xff, 0xff)
				);
			}
		}
	}
	GL_ERRORS();
}
