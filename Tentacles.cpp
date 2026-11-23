#include "Tentacles.hpp"

#include "ColorProgram.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/constants.hpp>

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
constexpr float GripStart = 0.82f;
constexpr float TipStart = 0.88f;
constexpr float GripRadius = 0.85f;
constexpr float GripCloseTime = 0.15f;
constexpr float PullTime = 1.0f;
constexpr float PullDistance = 22.0f;
constexpr int Segments = 64;
constexpr int StripSize = 2 * (Segments + 1);

glm::vec2 bezier(glm::vec2 const &a, glm::vec2 const &b, glm::vec2 const &c, glm::vec2 const &d, float t) {
	float back = 1.0f - t;
	return back * back * back * a + 3.0f * back * back * t * b +
		3.0f * back * t * t * c + t * t * t * d;
}

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
	setup(center, center + glm::vec2(0.0f, 8.0f), glm::vec2(19.0f, 16.0f), 1.25f, 0.0f);
}

void Tentacles::setup(glm::vec2 center, glm::vec2 guard, glm::vec2 span, float base_width, float solid_fraction) {
	guard_center = guard;
	size = span;
	thickness = base_width;
	solid_base = solid_fraction;
	for (int i = 0; i < 8; ++i) {
		roots[i] = center + glm::vec2(-0.5f * size.x + float(i) * (size.x / 7.0f), 0.0f);
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
		float pull = glm::clamp((caught_time - GripCloseTime) / PullTime, 0.0f, 1.0f);
		glm::vec2 offset = roots[strike_arm] - caught_position;
		float distance = glm::length(offset);
		glm::vec2 direction = offset / std::max(distance, 0.001f);
		target = caught_position + direction * (std::clamp(distance - 2.0f, 0.0f, PullDistance) * pull * pull);
		if (size.y > 32.0f) {
			//clear the chest's ledge before dragging the fish into the shaft
			target.x = glm::mix(caught_position.x, guard_center.x + 0.32f * size.x,
				glm::smoothstep(0.0f, 0.55f, pull));
		}
		return false;
	}
	if (distracted) {
		distraction = std::min(1.0f, distraction + elapsed / 1.8f);
		return false;
	}
	if (strike_time < StrikeCycle) {
		strike_time += elapsed;
		//track the fish until the grab ends
		if (active && strike_time <= WarningTime + ExtendTime + HoldTime &&
			glm::distance(fish, guard_center) < StrikeRange) target = fish;
	} else if (active && glm::distance(fish, guard_center) < StrikeRange) {
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
	if (caught && arm == strike_arm) {
		//wrap the tip around the captured fish
		float side = arm < 4 ? 1.0f : -1.0f;
		glm::vec2 grip = target - glm::vec2(0.0f, GripRadius);
		if (along >= GripStart) {
			float turn = (along - GripStart) / (1.0f - GripStart);
			float close = glm::smoothstep(0.0f, GripCloseTime, caught_time);
			float angle = -glm::half_pi<float>() + side * glm::mix(0.5f, 1.7f * glm::pi<float>(), close) * turn;
			return target + GripRadius * glm::vec2(std::cos(angle), std::sin(angle));
		}
		float t = along / GripStart;
		glm::vec2 first = roots[arm] + 0.5f * (grip - roots[arm]);
		glm::vec2 second = grip - glm::vec2(side * 4.0f, 0.0f);
		return bezier(roots[arm], first, second, grip, t);
	}
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
		size.y * (0.9375f + 0.075f * float(arm % 3)) - 8.0f * warning);
	//spread the long arms around the chest
	float long_blend = glm::clamp((size.y - 16.0f) / 32.0f, 0.0f, 1.0f);
	float order = float(arm < 4 ? 3 - arm : arm - 4);
	float spread = (arm < 4 ? -1.0f : 1.0f) * (4.0f + 2.5f * order);
	rest.x = glm::mix(rest.x, guard_center.x - roots[arm].x + spread, long_blend);
	rest.y = glm::mix(rest.y, guard_center.y - roots[arm].y + 6.0f + 3.0f * order - 8.0f * warning, long_blend);
	glm::vec2 tip = glm::mix(rest, target - roots[arm], lunge);
	float sway = (1.0f - lunge) * std::sin(0.55f * clock - 4.0f * along + float(arm));
	float curl = (1.0f - lunge) * 1.5f * along * (1.0f - along);
	float x = along * along * (tip.x + 0.85f * sway) - curl;
	glm::vec2 point = roots[arm] + glm::vec2(x, along * tip.y);
	//the tips of the long arms curl back down toward the chest
	if (long_blend > 0.0f) {
		float side = arm < 4 ? -1.0f : 1.0f;
		glm::vec2 end = roots[arm] + tip;
		glm::vec2 first = roots[arm] + glm::vec2(side * (2.0f + 0.3f * order), 0.55f * tip.y);
		glm::vec2 second = end + glm::vec2(side * 3.5f, 6.0f);
		glm::vec2 curve = bezier(roots[arm], first, second, end, along);
		//a slow wave bends the exposed length while the buried base stays still
		float phase = 0.65f * clock + 1.35f * float(arm);
		curve.x += 2.2f * std::sin(phase - 8.0f * along) * glm::smoothstep(0.45f, 0.85f, along);
		curve.y += 0.7f * std::sin(0.8f * phase) * glm::smoothstep(0.7f, 1.0f, along);
		point = glm::mix(point, curve, long_blend * (1.0f - lunge));
	}
	if (!distracted) return point;
	//separate arcs keep the arms readable as they gather around the pearl
	float angle = float(arm) * 6.2831853f / 8.0f;
	glm::vec2 grip = pearl + 0.45f * glm::vec2(std::cos(angle), std::sin(angle));
	float side = pearl.x < roots[arm].x ? 1.0f : -1.0f;
	glm::vec2 first = roots[arm] + glm::vec2(0.0f, 0.3125f * size.y + 0.6f * float(arm));
	glm::vec2 second = grip + glm::vec2(side * (1.8f + 0.3f * float(arm)), 2.0f + 0.3f * float(arm));
	glm::vec2 curve = bezier(roots[arm], first, second, grip, along);
	return glm::mix(point, curve, reach);
}

float Tentacles::width(int arm, float along) const {
	if (caught && arm == strike_arm && along >= GripStart) {
		return glm::mix(0.35f, 0.04f, (along - GripStart) / (1.0f - GripStart));
	}
	float long_blend = glm::clamp((size.y - 16.0f) / 32.0f, 0.0f, 1.0f);
	float taper = 1.0f - 0.7f * long_blend * glm::smoothstep(0.45f, 0.75f, along);
	return glm::mix(thickness, 0.52f * thickness, distraction) * (1.0f - along) *
		taper * (1.0f + 0.11f * std::sin(17.0f * along + float(arm)));
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
	if (!fish.active || (distracted && solid_base == 0.0f)) return;
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
			//the lower arms stay solid while the tips hold the pearl
			if (distracted && at > solid_base) { start = end; continue; }
			float radius = fish.radius + width(i, at);
			if (distance < radius) {
				if (!distracted && i == strike_arm && at > TipStart && strike_time >= WarningTime &&
					strike_time < WarningTime + ExtendTime + HoldTime) {
					fish.active = false;
					fish.velocity = glm::vec2(0.0f);
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

void Tentacles::draw(glm::mat4 const &world_to_clip, float clock, bool grip_only) const {
	if (grip_only && !caught) return;
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
	if (grip_only) {
		const int first = 2 * int(TipStart * float(Segments));
		glDrawArrays(GL_TRIANGLE_STRIP, strike_arm * StripSize + first, StripSize - first);
	} else {
		for (int strip = 0; strip < 8; ++strip) {
			glDrawArrays(GL_TRIANGLE_STRIP, strip * StripSize, StripSize);
		}
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
