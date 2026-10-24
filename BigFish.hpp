#pragma once

#include "DrawSprites.hpp"

#include <glm/glm.hpp>

//the big fish that chases the player. its jaws come in from the left, gaping, and snap shut on you
struct BigFish {
	BigFish();

	enum State {
		Waiting, //hidden off screen until its timer runs out
		Normal, //swims right
		Stunned, //hit by a rock or blinded, stops moving
	} state = Waiting;

	//left edge of the jaw pngs, at the line where the two jaws meet. the jaws turn around this point
	glm::vec2 position = glm::vec2(0.0f);
	//how wide each jaw png is drawn. the height follows the png's shape
	float size = 40.0f;
	float speed = 2.0f;

	//counts down while waiting or stunned
	float timer = 0.0f;
	//counts down while the mouth is snapping shut and opening again
	float bite_timer = 0.0f;

	static constexpr float MinDelay = 1.0f;
	static constexpr float MaxDelay = 5.0f;
	static constexpr float BiteTime = 0.8f;

	//puts the fish at start. it starts swimming after a random wait between MinDelay and MaxDelay seconds
	void reset(glm::vec2 const &start);
	void update(float elapsed);
	//stops the fish for this many seconds
	void stun(float seconds);
	//snaps the mouth shut, then opens it again
	void bite();

	//0 when the mouth is open, 1 when it's shut
	float closed() const;
	//how far in front of position the teeth reach
	float mouth_front() const;
	//true if p is between the jaws and the fish isn't stopped
	bool in_mouth(glm::vec2 const &p) const;
	//true if a circle at p has reached the front of the head
	bool hits(glm::vec2 const &p, float radius) const;

	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;

	SpriteTexture upper_jaw;
	SpriteTexture lower_jaw;
};
