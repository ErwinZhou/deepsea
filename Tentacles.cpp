#include "Tentacles.hpp"

#include "ColorProgram.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace {
constexpr float StrikeRange = 16.0f;
constexpr float WarningTime = 1.5f;
constexpr float ExtendTime = 0.4f;
constexpr float HoldTime = 0.2f;
constexpr float ReturnTime = 1.6f;
constexpr float StrikeCycle = WarningTime + ExtendTime + HoldTime + ReturnTime + 2.5f;
constexpr int Segments = 32;
constexpr int StripSize = 2 * (Segments + 1);

struct Vertex {
	glm::vec2 position;
	glm::vec4 color;
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
	glVertexAttribPointer(color_program->Color_vec4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
		reinterpret_cast<void const *>(offsetof(Vertex, color)));
	glEnableVertexAttribArray(color_program->Color_vec4);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

Tentacles::~Tentacles() {
	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &buffer);
}

void Tentacles::setup(glm::vec2 center) {
	for (int i = 0; i < 8; ++i) {
		roots[i] = center + glm::vec2(-9.5f + float(i) * (19.0f / 7.0f), 0.0f);
	}
	distracted = false;
	distraction = 0.0f;
	caught = false;
	caught_time = 0.0f;
	strike_time = StrikeCycle;
}

void Tentacles::distract(glm::vec2 const &position, bool already_holding) {
	// skip reaching if the arms already hold it
	if (already_holding) {
		distraction = 1.0f;
	}

	pearl = position;
	distracted = true;
}

bool Tentacles::update(float elapsed, glm::vec2 const &fish, bool active) {
	if (caught) {
		caught_time += elapsed;
		float pull = glm::smoothstep(0.0f, 0.7f, caught_time);
		glm::vec2 direction = glm::normalize(roots[strike_arm] - caught_position);
		target = caught_position + direction * (2.5f * pull);
		return false;
	}
	if (distracted) {
		distraction = std::min(1.0f, distraction + elapsed / 1.8f);
		return false;
	}
	if (strike_time < StrikeCycle) {
		strike_time += elapsed;
	} else if (active && glm::distance(fish, 0.5f * (roots[0] + roots[7]) + glm::vec2(0.0f, 8.0f)) < StrikeRange) {
		target = fish;
		strike_arm = 0;
		for (int i = 1; i < 8; ++i) {
			if (glm::distance(fish, roots[i]) < glm::distance(fish, roots[strike_arm])) strike_arm = i;
		}
		strike_time = 0.0f;
		return true;
	}
	return false;
}

glm::vec2 Tentacles::spine(int arm, float along, float clock) const {
	float extend = std::clamp((strike_time - WarningTime) / ExtendTime, 0.0f, 1.0f);
	float retract = std::clamp((strike_time - WarningTime - ExtendTime - HoldTime) / ReturnTime, 0.0f, 1.0f);
	extend *= 2.0f - extend;
	retract = retract * retract * (3.0f - 2.0f * retract);
	float strike = extend * (1.0f - retract);
	float reach = distraction * distraction * (3.0f - 2.0f * distraction);
	float lunge = arm == strike_arm ? strike : 0.0f;
	//fold the striking arm down into view before it lashes out
	float warning = arm == strike_arm && strike_time < WarningTime + ExtendTime + HoldTime ?
		std::clamp(strike_time / WarningTime, 0.0f, 1.0f) : 0.0f;
	glm::vec2 rest((arm < 4 ? 1.0f : -1.0f) * (3.0f + 0.4f * float(arm % 4)),
		15.0f + 1.2f * float(arm % 3) - 8.0f * warning);
	glm::vec2 tip = glm::mix(rest, target - roots[arm], lunge);
	float sway = (1.0f - lunge) * std::sin(0.55f * clock - 4.0f * along + float(arm));
	float curl = (1.0f - lunge) * 1.5f * along * (1.0f - along);
	float x = along * along * (tip.x + 0.85f * sway) - curl;
	glm::vec2 point = roots[arm] + glm::vec2(x, along * tip.y);
	if (!distracted) return point;
	//separate arcs keep the arms readable as they gather around the pearl
	float angle = float(arm) * 6.2831853f / 8.0f;
	glm::vec2 grip = pearl + 0.45f * glm::vec2(std::cos(angle), std::sin(angle));
	float side = pearl.x < roots[arm].x ? 1.0f : -1.0f;
	glm::vec2 first = roots[arm] + glm::vec2(0.0f, 5.0f + 0.6f * float(arm));
	glm::vec2 second = grip + glm::vec2(side * (1.8f + 0.3f * float(arm)), 2.0f + 0.3f * float(arm));
	float back = 1.0f - along;
	glm::vec2 curve = back * back * back * roots[arm] + 3.0f * back * back * along * first +
		3.0f * back * along * along * second + along * along * along * grip;
	return glm::mix(point, curve, reach);
}

float Tentacles::width(int arm, float along) const {
	return glm::mix(1.25f, 0.65f, distraction) * (1.0f - along) *
		(1.0f + 0.11f * std::sin(17.0f * along + float(arm)));
}

bool Tentacles::touches(glm::vec2 const &position, float radius, float clock) const {
	for (int i = 0; i < 8; ++i) {
		glm::vec2 start = spine(i, 0.0f, clock);
		for (int j = 1; j <= Segments; ++j) {
			glm::vec2 end = spine(i, float(j) / float(Segments), clock);
			glm::vec2 segment = end - start;
			float t = glm::clamp(glm::dot(position - start, segment) /
				std::max(0.00001f, glm::dot(segment, segment)), 0.0f, 1.0f);
			float at = (float(j - 1) + t) / float(Segments);
			if (glm::distance(position, start + t * segment) < radius + width(i, at)) return true;
			start = end;
		}
	}
	return false;
}

void Tentacles::push_fish(Body2D &fish, float clock) {
	if (!fish.active || distracted) return;
	//match collisions to the drawn arms
	for (int i = 0; i < 8; ++i) {
		glm::vec2 start = spine(i, 0.0f, clock);
		for (int j = 1; j <= Segments; ++j) {
			float along = float(j) / float(Segments);
			glm::vec2 end = spine(i, along, clock);
			glm::vec2 segment = end - start;
			float t = glm::clamp(glm::dot(fish.position - start, segment) /
				std::max(0.00001f, glm::dot(segment, segment)), 0.0f, 1.0f);
			glm::vec2 offset = fish.position - (start + t * segment);
			float distance = glm::length(offset);
			float at = (float(j - 1) + t) / float(Segments);
			float radius = fish.radius + width(i, at);
			if (distance < radius) {
				if (i == strike_arm && at > 0.35f && strike_time >= WarningTime &&
					strike_time < WarningTime + ExtendTime + HoldTime) {
					fish.active = false;
					caught = true;
					caught_time = 0.0f;
					caught_position = fish.position;
					target = fish.position;
					strike_time = WarningTime + ExtendTime;
					return;
				}
				glm::vec2 normal = distance > 0.001f ? offset / distance : glm::vec2(0.0f, 1.0f);
				fish.position += normal * (radius - distance);
				float inward = glm::dot(fish.velocity, normal);
				if (inward < 0.0f) fish.velocity -= inward * normal;
			}
			start = end;
		}
	}
}

void Tentacles::draw(glm::mat4 const &world_to_clip, float clock) const {
	std::array<Vertex, 8 * StripSize> vertices;
	constexpr glm::vec4 ink(0.0015f, 0.0015f, 0.0015f, 1.0f);
	//reference for the wiggling silhouette: https://www.youtube.com/watch?v=qlfh_rv6khY
	//https://github.com/argonautcode/animal-proc-anim
	for (int i = 0; i < 8; ++i) {
		for (int j = 0; j <= Segments; ++j) {
			float along = float(j) / float(Segments);
			glm::vec2 tangent = spine(i, std::min(1.0f, along + 0.01f), clock) - spine(i, std::max(0.0f, along - 0.01f), clock);
			glm::vec2 side = glm::vec2(-tangent.y, tangent.x) / std::max(0.00001f, glm::length(tangent));
			float radius = width(i, along);
			glm::vec2 center = spine(i, along, clock);
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
	for (int strip = 0; strip < 8; ++strip) {
		glDrawArrays(GL_TRIANGLE_STRIP, strip * StripSize, StripSize);
	}
	glBindVertexArray(0);
	glUseProgram(0);
}

void Tentacles::draw_backdrop(glm::mat4 const &world_to_clip) const {
	//a patch of lighter water separates the black arms from the cave pillars
	constexpr int Sides = 48;
	std::array<Vertex, Sides + 2> vertices;
	glm::vec2 center = 0.5f * (roots[0] + roots[7]) + glm::vec2(0.0f, 9.0f);
	vertices[0] = {center, glm::vec4(0.035f, 0.035f, 0.035f, 0.95f)};
	for (int i = 0; i <= Sides; ++i) {
		float angle = float(i) * 6.2831853f / float(Sides);
		glm::vec2 edge = center + glm::vec2(18.0f * std::cos(angle), 12.5f * std::sin(angle));
		vertices[i + 1] = {edge, glm::vec4(0.035f, 0.035f, 0.035f, 0.0f)};
	}
	glBindBuffer(GL_ARRAY_BUFFER, buffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STREAM_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glUseProgram(color_program->program);
	glUniformMatrix4fv(color_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(world_to_clip));
	glBindVertexArray(vao);
	glDrawArrays(GL_TRIANGLE_FAN, 0, GLsizei(vertices.size()));
	glBindVertexArray(0);
	glUseProgram(0);
}
