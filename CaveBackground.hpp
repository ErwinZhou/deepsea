#pragma once

#include "DrawSprites.hpp"

struct CaveBackground {
	CaveBackground();
	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip,
		glm::vec2 const &view_center, glm::vec2 const &view_size,
		glm::vec2 const &bounds_min, glm::vec2 const &bounds_max) const;
	void draw_fade(DrawSprites const &sprites, glm::mat4 const &world_to_clip,
		glm::vec2 const &view_center, glm::vec2 const &view_size, float amount) const;

private:
	SpriteTexture background_texture;
	SpriteTexture cavern_texture;
};
