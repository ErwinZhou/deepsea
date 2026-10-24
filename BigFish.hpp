#pragma once

#include "DrawSprites.hpp"

#include <glm/glm.hpp>

//the big fish that chases the player. its jaws come in from the left, gaping, and slowly close on you
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
	float size = 60.0f;
	float speed = 1.5f;

	//counts down while waiting or stunned
	float timer = 0.0f;
	//counts up from 0 while biting, below 0 when not biting
	float bite_time = -1.0f;
	//seconds since reset, drives the slow bob and the breathing of the mouth
	float time = 0.0f;
	//height the fish bobs around
	float base_y = 0.0f;
	//true for the one update where the jaws slam shut
	bool slammed = false;

	static constexpr float MinDelay = 1.0f;
	static constexpr float MaxDelay = 5.0f;

	//puts the fish at start. it starts swimming after a random wait between MinDelay and MaxDelay seconds
	void reset(glm::vec2 const &start);
	void update(float elapsed);
	//stops the fish for this many seconds
	void stun(float seconds);
	//starts closing the jaws. does nothing if they are already closing
	void bite();
	bool biting() const;

	//0 when the mouth is open, 1 when it's shut
	float closed() const;
	//how far in front of position the teeth reach
	float mouth_front() const;
	//true if p is between the jaws and the fish isn't stopped
	bool in_mouth(glm::vec2 const &p) const;
	//true if a circle at p has reached the front of the head
	bool hits(glm::vec2 const &p, float radius) const;
	//how far to shift the view to shake the screen after a slam
	glm::vec2 shake() const;

	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;

	SpriteTexture upper_jaw;
	SpriteTexture lower_jaw;
};
