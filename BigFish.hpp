#pragma once

#include "DrawSprites.hpp"

#include <glm/glm.hpp>

//the big fish that chases the player. it's just a mouth facing right
struct BigFish {
	BigFish();

	enum State {
		Waiting, //hidden off screen until its timer runs out
		Normal, //swims right
		Stunned, //hit by a rock or blinded, stops moving
	} state = Waiting;

	//middle of the mouth
	glm::vec2 position = glm::vec2(0.0f);
	//how wide each jaw is. the height follows the png's shape
	float size = 6.0f;
	float speed = 2.0f;

	//counts down while waiting or stunned
	float timer = 0.0f;
	//goes from 0 to 1 every time the mouth opens and closes
	float mouth_phase = 0.0f;

	static constexpr float MinDelay = 1.0f;
	static constexpr float MaxDelay = 5.0f;
	static constexpr float MouthPeriod = 2.0f;

	//puts the fish at start. it starts swimming after a random wait between MinDelay and MaxDelay seconds
	void reset(glm::vec2 const &start);
	void update(float elapsed);
	//stops the fish for this many seconds
	void stun(float seconds);

	//how far apart the jaws are right now
	float gap() const;
	//height of one jaw, from the png's shape
	float jaw_height() const;
	//true if p is between the open jaws or a bit in front of them, and the fish isn't stopped
	bool can_suck(glm::vec2 const &p) const;
	//true if a circle at p touches the jaws or the space between them
	bool hits(glm::vec2 const &p, float radius) const;

	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;

	//this png is the top jaw. the bottom jaw is the same png flipped
	SpriteTexture jaw_texture;
};
