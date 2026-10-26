#pragma once

#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"

#include <array>

struct Level1 {
	struct Pillar {
		// ceiling attachment in world space and full sprite dimensions, including transparent margins
		glm::vec2 anchor{0.0f};
		glm::vec2 size{0.0f};

		void place(glm::vec2 const &bounds_min, glm::vec2 const &bounds_max, glm::uvec2 const &pixel_size);
		// source coordinates start at the image's top-left and range from 0 to 1
		glm::vec2 world_from_source(glm::vec2 const &point) const;
		StaticBox2D collider() const;
		std::array<glm::vec2, 3> crack_region() const;
		glm::vec2 crack_normal() const;
		void draw(DrawSprites const &sprites, SpriteTexture const &texture, glm::mat4 const &world_to_clip) const;
	} pillar;

	Level1();
	void setup(UnderwaterPhysics &physics);
	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;

private:
	SpriteTexture pillar_texture;
};
