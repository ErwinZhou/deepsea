#include "CaveBackground.hpp"

#include "data_path.hpp"

#include <algorithm>
#include <cmath>

CaveBackground::CaveBackground()
	: background_texture(data_path("cavern_background.png")),
	  cavern_texture(data_path("cavern_rock.png")) {
}

void CaveBackground::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip,
	glm::vec2 const &view_center, glm::vec2 const &view_size,
	glm::vec2 const &bounds_min, glm::vec2 const &bounds_max) const {
	const glm::vec2 half_view = 0.5f * view_size;
	const float middle_y = 0.5f * (bounds_min.y + bounds_max.y);
	auto draw_layer = [&](SpriteTexture const &texture, float parallax, float height, float fade, uint8_t alpha) {
		height = std::max(height, (bounds_max.y - bounds_min.y) + 4.0f);
		const float width = height * float(texture.pixel_size.x) / float(texture.pixel_size.y);
		const glm::vec2 origin(view_center.x * (1.0f - parallax), middle_y);
		const int first = int(std::floor((view_center.x - half_view.x - origin.x) / width));
		const int last = int(std::floor((view_center.x + half_view.x - origin.x) / width));
		for (int i = first; i <= last; ++i) {
			sprites.draw(texture, world_to_clip, origin + glm::vec2((float(i) + 0.5f) * width, 0.0f),
				glm::vec2(width + 0.03f, height), 0.0f, i % 2 != 0,
				glm::u8vec4(255, 255, 255, alpha), fade, DrawSprites::Background);
		}
	};
	draw_layer(background_texture, 0.12f, 28.0f, 0.72f, 255);
	draw_layer(cavern_texture, 0.28f, 34.0f, 0.45f, 55);
}
