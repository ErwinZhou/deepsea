#include "level1.hpp"

#include "data_path.hpp"

#include <algorithm>

// the artist's rock already hangs downward, with the canvas top attached to the ceiling
static glm::vec2 const SourceAnchor(0.52f, 0.0f);
static constexpr float RockBottom = 0.8726f;

Level1::Level1()
	: background_texture(data_path("background.png")),
	  pillar_texture(data_path("big_rock.png")),
	  stone_texture(data_path("small_rock.png")),
	  wall_texture(data_path("wall.png")) {
}

void Level1::setup(UnderwaterPhysics &physics, glm::vec2 const &view_size) {
	// fit two view heights
	background_size = glm::vec2(background_texture.pixel_size);
	background_size *= 2.0f * std::max(view_size.x / background_size.x, view_size.y / background_size.y);
	physics.bounds_min = -0.5f * background_size;
	physics.bounds_max = 0.5f * background_size;
	glm::vec2 start_center = physics.bounds_min + 0.5f * view_size;
	player_start = start_center + glm::vec2(4.0f, 0.0f);
	pillar.place(physics.bounds_min, physics.bounds_max, pillar_texture.pixel_size);
	physics.polygons.push_back(pillar.collider());
	physics.bodies.push_back({.position = start_center + glm::vec2(-1.5f, 1.8f), .radius = 0.35f});
	physics.bodies.push_back({.position = start_center + glm::vec2(5.0f, -1.5f), .inverse_mass = 0.625f, .radius = 0.43f});
	physics.bodies.push_back({.position = start_center + glm::vec2(10.0f, 3.4f), .inverse_mass = 1.25f, .radius = 0.29f});
	// leave a recoverable stone here, so that we could pick it up and shoot
	glm::vec2 crack = pillar.crack_region()[0];
	physics.bodies.push_back({.position = crack + glm::vec2(3.0f, -2.0f), .radius = 0.35f});

	float height = physics.bounds_max.y - physics.bounds_min.y;
	wall_size = glm::vec2(height * float(wall_texture.pixel_size.x) / float(wall_texture.pixel_size.y), height);
	glm::vec2 wall_origin(physics.bounds_max.x - 3.0f - 0.2783f * wall_size.x, physics.bounds_max.y);
	wall_center = wall_origin + glm::vec2(0.5f, -0.5f) * wall_size;
	// continue the wall above and below the level so edge contacts cannot trap bodies at the ceiling or floor
	right_wall.vertices.clear();
	for (auto point : {glm::vec2(0.278f, -1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 2.0f),
		glm::vec2(0.278f, 2.0f), glm::vec2(0.278f, 1.0f), glm::vec2(0.296f, 0.9f),
		glm::vec2(0.318f, 0.75f), glm::vec2(0.340f, 0.65f), glm::vec2(0.329f, 0.4f),
		glm::vec2(0.334f, 0.3f), glm::vec2(0.309f, 0.2f), glm::vec2(0.295f, 0.1f), glm::vec2(0.278f, 0.0f)}) {
		right_wall.vertices.push_back(wall_origin + glm::vec2(point.x, -point.y) * wall_size);
	}
	physics.polygons.push_back(right_wall);
}

void Level1::draw(DrawSprites const &sprites, UnderwaterPhysics const &physics, glm::mat4 const &world_to_clip) const {
	sprites.draw(background_texture, world_to_clip, glm::vec2(0.0f), background_size);
	pillar.draw(sprites, pillar_texture, world_to_clip);
	sprites.draw(wall_texture, world_to_clip, wall_center, wall_size);
	for (auto const &body : physics.bodies) {
		if (!body.active || body.kind != Body2D::Rock) continue;
		// matching the visible rock to its collision diameter
		float height = 2.0f * body.radius / 0.834f;
		glm::vec2 size(height * float(stone_texture.pixel_size.x) / float(stone_texture.pixel_size.y), height);
		sprites.draw(stone_texture, world_to_clip, body.position, size);
	}
}

void Level1::Pillar::place(glm::vec2 const &bounds_min, glm::vec2 const &bounds_max, glm::uvec2 const &pixel_size) {
	glm::vec2 world_size = bounds_max - bounds_min;
	anchor = glm::vec2(bounds_min.x + 0.68f * world_size.x, bounds_max.y);
	float canvas_height = 0.60f * world_size.y / RockBottom;
	size = glm::vec2(canvas_height * float(pixel_size.x) / float(pixel_size.y), canvas_height);
}

glm::vec2 Level1::Pillar::world_from_source(glm::vec2 const &point) const {
	// convert image y-down coordinates to world y-up coordinates without rotating the hanging artwork
	return anchor + glm::vec2(point.x - SourceAnchor.x, SourceAnchor.y - point.y) * size;
}

StaticPolygon2D Level1::Pillar::collider() const {
	StaticPolygon2D polygon;
	// follow the taper so the clear water beside the tip does not become an invisible rectangular wall
	for (auto point : {glm::vec2(0.075f, -1.0f), glm::vec2(0.947f, -1.0f), glm::vec2(0.947f, 0.0f),
		glm::vec2(0.960f, 0.40f), glm::vec2(0.720f, 0.70f), glm::vec2(0.620f, 0.870f),
		glm::vec2(0.470f, 0.870f), glm::vec2(0.280f, 0.70f), glm::vec2(0.070f, 0.10f), glm::vec2(0.075f, 0.0f)}) {
		polygon.vertices.push_back(world_from_source(point));
	}
	return polygon;
}

std::array<glm::vec2, 3> Level1::Pillar::crack_region() const {
	// the pale fracture reaches the sloping lower-right face of the new rock
	return {world_from_source({0.740f, 0.675f}), world_from_source({0.660f, 0.660f}),
		world_from_source({0.720f, 0.700f})};
}

glm::vec2 Level1::Pillar::crack_normal() const {
	return glm::normalize(glm::vec2(0.300f, -0.240f));
}

void Level1::Pillar::draw(DrawSprites const &sprites, SpriteTexture const &texture, glm::mat4 const &world_to_clip) const {
	// use the same source transform for the sprite center, outline and crack
	sprites.draw(texture, world_to_clip, world_from_source({0.5f, 0.5f}), size);
}
