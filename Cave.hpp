#pragma once

#include "DrawSprites.hpp"
#include "UnderwaterPhysics.hpp"
#include "BigFish.hpp"

#include <vector>

//the demo level: a cave corridor with a cracked wall to learn on, a cracked rock hanging from the ceiling,
//a second cracked wall in front of the way out, and the way out
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

	//what happened during the last update, for PlayMode to react to
	struct Events {
		bool hit = false;
		bool broke = false;
		bool first_wall_broke = false;
		bool crashed = false;
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
	//throws count chips out from the point at, flying roughly in the direction away
	void throw_chips(glm::vec2 const &at, glm::vec2 const &away, int count);
	//shakes dust down from the ceiling between left and right. amount 0 is none, 1 is a lot
	void drop_ceiling_dust(float left, float right, float amount, float elapsed);

	//places everything in the corridor and adds the loose rocks to physics
	void setup(UnderwaterPhysics &physics);
	//call once per frame before stepping physics
	void update(UnderwaterPhysics &physics, BigFish &big_fish, float elapsed);
	//walls, the hanging rock and the way out
	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;
	//the dropped rock, drawn over the big fish so it looks pinned under it
	void draw_over_big_fish(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;

	glm::vec2 bounds_min = glm::vec2(0.0f);
	glm::vec2 bounds_max = glm::vec2(0.0f);
	//seconds before each body can count as hitting something again, so one bounce counts once
	std::vector< float > hit_cooldown;

	SpriteTexture wall_texture;
	SpriteTexture hanging_texture;
	SpriteTexture split_texture;
	SpriteTexture exit_texture;
	SpriteTexture chip_texture;
};
