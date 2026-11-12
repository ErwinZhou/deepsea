#include "Tentacles.hpp"

#include "ColorProgram.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace {
constexpr float StrikeRange = 7.0f;
constexpr float ExtendTime = 0.18f;
constexpr float HoldTime = 0.12f;
constexpr float ReturnTime = 0.65f;
constexpr float StrikeCycle = ExtendTime + HoldTime + ReturnTime + 0.9f;
constexpr int Segments = 20;
constexpr int StripSize = 2 * (Segments + 1);

struct Vertex {
	glm::vec2 position;
	glm::vec3 color;
};
}

Tentacles::Tentacles() {
	//based on the color buffer setup in DrawLines.cpp
	glGenBuffers(1, &buffer);
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glVertexAttribPointer(color_program->Position_vec4, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		reinterpret_cast<void const *>(offsetof(Vertex, position)));
	glEnableVertexAttribArray(color_program->Position_vec4);
	glVertexAttribPointer(color_program->Color_vec4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		reinterpret_cast<void const *>(offsetof(Vertex, color)));
	glEnableVertexAttribArray(color_program->Color_vec4);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

Tentacles::~Tentacles() {
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &buffer);
}

void Tentacles::setup(glm::vec2 const &spawn, float floor) {
	roots[0] = glm::vec2(spawn.x + 0.5f, floor - 0.2f);
	roots[1] = glm::vec2(spawn.x - 8.0f, floor - 0.2f);
	strike_time = StrikeCycle;
}

void Tentacles::update(float elapsed, glm::vec2 const &fish, bool active) {
	if (strike_time < StrikeCycle) {
		strike_time += elapsed;
	} else if (active && glm::distance(fish, roots[1]) < StrikeRange) {
		target = fish - roots[1];
		strike_time = 0.0f;
	}
}

void Tentacles::draw(glm::mat4 const &world_to_clip, float clock) const {
	float extend = std::clamp(strike_time / ExtendTime, 0.0f, 1.0f);
	float retract = std::clamp((strike_time - ExtendTime - HoldTime) / ReturnTime, 0.0f, 1.0f);
	extend *= 2.0f - extend;
	retract = retract * retract * (3.0f - 2.0f * retract);
	float strike = extend * (1.0f - retract);
	std::array<Vertex, 2 * StripSize> vertices;
	constexpr glm::vec3 ink(0.0015f);
	//reference for the wiggling silhouette: https://www.youtube.com/watch?v=qlfh_rv6khY
	//https://github.com/argonautcode/animal-proc-anim
	for (int i = 0; i < 2; ++i) {
		float lunge = i == 0 ? 0.0f : strike;
		glm::vec2 rest = i == 0 ? glm::vec2(0.8f, 6.5f) : glm::vec2(1.5f, 1.15f);
		glm::vec2 tip = glm::mix(rest, target, lunge);
		auto spine = [&](float along) {
			float sway = (1.0f - lunge) * std::sin(1.7f * clock - 6.5f * along + float(i));
			float curl = (1.0f - lunge) * 1.5f * along * (1.0f - along);
			float x = along * along * (tip.x + 0.85f * sway) - curl;
			return roots[i] + glm::vec2(x, along * tip.y);
		};
		for (int j = 0; j <= Segments; ++j) {
			float along = float(j) / float(Segments);
			glm::vec2 tangent = spine(std::min(1.0f, along + 0.01f)) - spine(std::max(0.0f, along - 0.01f));
			glm::vec2 side = glm::normalize(glm::vec2(-tangent.y, tangent.x));
			float edge = 1.0f + 0.11f * std::sin(17.0f * along + float(i));
			float radius = 0.52f * (1.0f - along) * edge;
			glm::vec2 center = spine(along);
			int index = i * StripSize + 2 * j;
			vertices[index] = {center + radius * side, ink};
			vertices[index + 1] = {center - radius * side, ink};
		}
	}

	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STREAM_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(color_program->program);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(world_to_clip));
	glBindVertexArray(vao);
	for (int strip = 0; strip < 2; ++strip) {
		glDrawArrays(GL_TRIANGLE_STRIP, strip * StripSize, StripSize);
	}
	glBindVertexArray(0);
	glUseProgram(0);
}
