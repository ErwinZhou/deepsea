#include "level3.hpp"

#include "data_path.hpp"
#include "DrawLines.hpp"

#include <algorithm>

Level3::Level3()
	: exit_texture(data_path("level1/exit.png")),
	  fade_texture(glm::u8vec4(255)) {
}

void Level3::setup(UnderwaterPhysics &physics, Camera2D &camera, size_t player_body) {
	physics.bounds_min = glm::vec2(-0.5f * level_length, -0.5f * corridor_height);
	physics.bounds_max = glm::vec2(0.5f * level_length, 0.5f * corridor_height);
	physics.obstacles.clear();
	physics.bodies[player_body].position = glm::vec2(physics.bounds_min.x + entrance_distance, 0.0f);

	exit_box = StaticBox2D{glm::vec2(physics.bounds_max.x - exit_width + exit_shift, physics.bounds_min.y),
		glm::vec2(physics.bounds_max.x + exit_shift, physics.bounds_max.y)};

	camera.view_size = glm::vec2(corridor_height * (16.0f / 9.0f), corridor_height);
	camera.dead_zone = glm::vec2(5.0f, 2.5f);
	camera.follow_speed = 4.0f;
	camera.center = camera.constrain(physics.bodies[player_body].position, physics.bounds_min, physics.bounds_max);
}

void Level3::update(float elapsed, UnderwaterPhysics const &physics, size_t player_body) {
	clock += elapsed;
	if (escaped) {
		end_time += elapsed;
		return;
	}

	//touching the exit starts the ending
	Body2D const &player = physics.bodies[player_body];
	const glm::vec2 offset = player.position - glm::clamp(player.position, exit_box.min, exit_box.max);
	if (player.active && glm::dot(offset, offset) <= player.radius * player.radius) {
		escaped = true;
	}
}

void Level3::prepare_water(Water::Look &look) const {
	look.glows.clear();
	look.glows.emplace_back(0.5f * (exit_box.min.x + exit_box.max.x), 0.5f * (exit_box.min.y + exit_box.max.y), exit_glow);
	look.beams.clear();
	look.blockers.clear();
	look.bubble_columns.clear();
	look.glow_radius = glm::vec2(10.0f, 8.0f);
	look.brightness = scene_brightness;
	look.time = clock;
}

void Level3::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	sprites.draw(exit_texture, world_to_clip, 0.5f * (exit_box.min + exit_box.max), exit_box.max - exit_box.min);
}

void Level3::draw_overlay(DrawSprites const &sprites, glm::mat4 const &world_to_clip,
	glm::vec2 const &view_center, glm::vec2 const &view_size) const {
	//fade in from the white mouth transition
	float entrance = 1.0f - std::min(1.0f, clock / fade_in_time);
	if (entrance > 0.0f) {
		sprites.draw(fade_texture, world_to_clip, view_center, view_size, 0.0f, false,
			glm::u8vec4(255, 255, 255, uint8_t(255.0f * entrance)), 0.0f, DrawSprites::Emissive);
	}
	if (escaped) {
		float black = std::min(1.0f, end_time / fade_out_time);
		sprites.draw(fade_texture, world_to_clip, view_center, view_size, 0.0f, false,
			glm::u8vec4(0, 0, 0, uint8_t(255.0f * black)), 0.0f, DrawSprites::Emissive);
	}

	if (!escaped) return;
	float text = std::clamp((end_time - fade_out_time) / text_fade_time, 0.0f, 1.0f);
	if (text <= 0.0f) return;

	//center the ending text
	DrawLines lines(world_to_clip);
	glm::vec3 text_end;
	lines.draw_text("YOU ESCAPED", glm::vec3(0.0f), glm::vec3(text_height, 0.0f, 0.0f), glm::vec3(0.0f, text_height, 0.0f),
		glm::u8vec4(0xc8, 0xd2, 0xd6, uint8_t(255.0f * text)), &text_end);
	const glm::vec3 shift(view_center - 0.5f * glm::vec2(text_end.x, text_height), 0.0f);
	for (DrawLines::Vertex &vertex : lines.attribs) vertex.Position += shift;
}
