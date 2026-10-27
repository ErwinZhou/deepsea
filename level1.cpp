#include "level1.hpp"

#include "data_path.hpp"

#include <algorithm>
#include <cassert>

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
	struct {
		float view_scale = 2.0f;
		glm::vec2 player_offset{4.0f, 0.0f};
		glm::vec2 aiming_rock_offset{3.0f, -2.0f};
		float aiming_rock_radius = 0.35f;
		float wall_visual_inset = 3.0f;
		float wall_collision_inset = 1.0f;
	} const layout;
	assert(view_size.x > 0.0f && view_size.y > 0.0f && "camera view dimensions must be positive");
	assert(physics.bodies.size() == 1 && physics.bodies[0].kind == Body2D::Fish && "create only the player before level setup");
	assert(physics.polygons.empty() && "level setup must not register geometry twice");

	// scale the background without changing its proportions
	background_size = glm::vec2(background_texture.pixel_size);
	background_size *= layout.view_scale * std::max(view_size.x / background_size.x, view_size.y / background_size.y);
	physics.bounds_min = -0.5f * background_size;
	physics.bounds_max = 0.5f * background_size;
	glm::vec2 const start_center = physics.bounds_min + 0.5f * view_size;
	player_start = start_center + layout.player_offset;
	pillar.place(physics.bounds_min, physics.bounds_max, pillar_texture.pixel_size);
	physics.polygons.push_back(pillar.collider());
	{ // keep rock placement relative to the first camera view
		struct RockSpawn {
			glm::vec2 offset;
			float radius;
			float mass;
		};
		RockSpawn const rocks[] = {
			{{-1.5f, 1.8f}, 0.35f, 1.0f},
			{{5.0f, -1.5f}, 0.43f, 1.6f},
			{{10.0f, 3.4f}, 0.29f, 0.8f}
		};
		for (RockSpawn const &rock : rocks) {
			assert(rock.radius > 0.0f && rock.mass > 0.0f && "rock spawn dimensions and mass must be positive");
			physics.bodies.push_back({.position = start_center + rock.offset, .inverse_mass = 1.0f / rock.mass, .radius = rock.radius});
		}
		// leave a recoverable stone outside the crack's exposed face
		glm::vec2 const crack = pillar.crack_region()[0];
		physics.bodies.push_back({.position = crack + layout.aiming_rock_offset, .radius = layout.aiming_rock_radius});
	}

	{ // place the visible wall using its measured transparent margin
		constexpr float source_left = 0.2783f;
		float const height = physics.bounds_max.y - physics.bounds_min.y;
		wall_size = glm::vec2(height * float(wall_texture.pixel_size.x) / float(wall_texture.pixel_size.y), height);
		glm::vec2 const wall_origin(physics.bounds_max.x - layout.wall_visual_inset - source_left * wall_size.x, physics.bounds_max.y);
		wall_center = wall_origin + glm::vec2(0.5f, -0.5f) * wall_size;
		// a rectangle is enough for this boundary, extending past the floor and ceiling to avoid trapping bodies
		float const left = physics.bounds_max.x - layout.wall_collision_inset;
		float const right = physics.bounds_max.x + wall_size.x;
		float const top = physics.bounds_max.y + height;
		float const bottom = physics.bounds_min.y - height;
		right_wall.vertices = {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
	}
	physics.polygons.push_back(right_wall);
}

void Level1::draw(DrawSprites const &sprites, UnderwaterPhysics const &physics, glm::mat4 const &world_to_clip) const {
	sprites.draw(background_texture, world_to_clip, glm::vec2(0.0f), background_size);
	pillar.draw(sprites, pillar_texture, world_to_clip);
	sprites.draw(wall_texture, world_to_clip, wall_center, wall_size);
	// the visible stone occupies this fraction of its png width
	constexpr float rock_visible_width = 0.834f;
	for (Body2D const &body : physics.bodies) {
		if (!body.active || body.kind != Body2D::Rock) {
			continue;
		}
		float height = 2.0f * body.radius / rock_visible_width;
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
