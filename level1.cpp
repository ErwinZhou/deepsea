#include "level1.hpp"

#include "data_path.hpp"

#include <cmath>

// the upright png's stone base becomes the ceiling attachment after a half-turn
static glm::vec2 const SourceAnchor(0.44f, 0.90f);
static glm::vec2 const ShaftMin(0.265f, 0.055f);
static glm::vec2 const ShaftMax(0.625f, 0.90f);

Level1::Level1() : pillar_texture(data_path("pillar.png")) {
}

void Level1::setup(UnderwaterPhysics &physics) {
	pillar.place(physics.bounds_min, physics.bounds_max, pillar_texture.pixel_size);
	physics.obstacles.push_back(pillar.collider());
}

void Level1::draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const {
	pillar.draw(sprites, pillar_texture, world_to_clip);
}

void Level1::Pillar::place(glm::vec2 const &bounds_min, glm::vec2 const &bounds_max, glm::uvec2 const &pixel_size) {
	glm::vec2 world_size = bounds_max - bounds_min;
	anchor = glm::vec2(bounds_min.x + 0.68f * world_size.x, bounds_max.y);
	// the visible stone occupies about 85 percent of the png's height
	float canvas_height = 0.60f * world_size.y / (SourceAnchor.y - 0.05f);
	size = glm::vec2(canvas_height * float(pixel_size.x) / float(pixel_size.y), canvas_height);
}

glm::vec2 Level1::Pillar::world_from_source(glm::vec2 const &point) const {
	// a half-turn reverses x and sends the original image top downward from the ceiling
	return anchor + glm::vec2(SourceAnchor.x - point.x, point.y - SourceAnchor.y) * size;
}

StaticBox2D Level1::Pillar::collider() const {
	glm::vec2 a = world_from_source(ShaftMin);
	glm::vec2 b = world_from_source(ShaftMax);
	StaticBox2D box{glm::min(a, b), glm::max(a, b)};
	// extend into the ceiling so an overlapping body cannot be pushed above the world and trapped
	box.max.y = anchor.y + size.y;
	return box;
}

std::array<glm::vec2, 3> Level1::Pillar::crack_region() const {
	// this triangular fracture lies on the upright png's left edge, then rotates to the right
	return {world_from_source({0.265f, 0.37f}), world_from_source({0.325f, 0.415f}),
		world_from_source({0.265f, 0.475f})};
}

glm::vec2 Level1::Pillar::crack_normal() const {
	// the source's left-facing surface normal points right after the half-turn
	return glm::vec2(1.0f, 0.0f);
}

void Level1::Pillar::draw(DrawSprites const &sprites, SpriteTexture const &texture, glm::mat4 const &world_to_clip) const {
	// use the same source transform for the sprite center, shaft and crack
	sprites.draw(texture, world_to_clip, world_from_source({0.5f, 0.5f}), size, float(M_PI));
}
