#pragma once

#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "BigFish.hpp"

#include <vector>

//cave obstacles and the way out
struct Cave {
	Cave();

	//a rock wall across the whole corridor. spitting enough rocks into it breaks it
	struct Wall {
		float x = 0.0f;
		int hits_left = 0;
		StaticBox2D box;
		//seconds left of the shudder after a hit
		float shudder = 0.0f;
	};
	//teaches spitting before the big fish shows up
	Wall first_wall;
	//stands between the small fish and the way out
	Wall last_wall;

	//the cracked rock hanging from the ceiling. enough hits and it drops
	struct HangingRock {
		enum State { Hanging, Falling, Fallen } state = Hanging;
		int hits_left = 0;
		glm::vec2 center = glm::vec2(0.0f);
		glm::vec2 velocity = glm::vec2(0.0f);
		float angle = 0.0f;
		//true once the big fish has been held up by it, so it only works once
		bool used = false;
		float shudder = 0.0f;
	} hanging_rock;

	//swimming past this x is escaping
	float exit_x = 0.0f;

	//exit and coral light sources
	std::vector< glm::vec3 > light_spots;
	glm::vec2 coral_position = glm::vec2(0.0f);
	//keep the coral and its light attached to the pillar
	void update_coral();
	//use fewer boxes for shadows than for collisions
	std::vector< StaticBox2D > light_blockers;

	struct Target {
		std::vector< glm::vec4 > parts;
		std::vector< glm::vec2 > cracks;
		glm::vec2 texel_size = glm::vec2(0.0f);
		glm::vec2 entry = glm::vec2(1.0f, 0.5f);
	};
	Target wall_targets[3];
	Target hanging_targets[3];

	//what happened during the last update, for PlayMode to react to
	struct Events {
		bool hit = false;
		//a spat rock knocked against something but not on its crack
		bool missed = false;
		bool broke = false;
		bool first_wall_broke = false;
		bool crashed = false;
		//play impact sounds at the last collision
		glm::vec2 at = glm::vec2(0.0f);
	} events;

	//chips knocked off by hits, and dust shaken down from the ceiling
	struct Particle {
		glm::vec2 position = glm::vec2(0.0f);
		glm::vec2 velocity = glm::vec2(0.0f);
		float size = 0.0f;
		float angle = 0.0f;
		float spin = 0.0f;
		float life = 0.0f;
		float max_life = 0.0f;
		//how fast it falls. chips drop fast, dust drifts down slowly
		float gravity = 0.0f;
	};
	std::vector< Particle > particles;
	//throw chips away from an impact
	void throw_chips(glm::vec2 const &at, glm::vec2 const &away, int count);
	//drop ceiling dust in the visible part of the cave
	void drop_ceiling_dust(float left, float right, float amount, float elapsed);

	//places everything in the corridor and adds the loose rocks to physics
	void setup(UnderwaterPhysics &physics);
	//call once per frame before stepping physics
	void update(UnderwaterPhysics &physics, BigFish &big_fish, float elapsed);
	//walls, the hanging rock and the way out
	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;
	//the pillar draws over the big fish
	void draw_over_big_fish(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;
	glm::vec2 shake() const;
	int wall_stage(Wall const &wall) const;
	glm::vec2 wall_crack_position(Wall const &wall) const;

	glm::vec2 bounds_min = glm::vec2(0.0f);
	glm::vec2 bounds_max = glm::vec2(0.0f);
	//seconds before each body can count as hitting something again, so one bounce counts once
	std::vector< float > hit_cooldown;

	SpriteTexture wall_textures[3];
	SpriteTexture hanging_textures[3];
	SpriteTexture exit_texture;
	SpriteTexture coral_texture;
	SpriteTexture chip_texture;
};
