#pragma once

#include <glm/glm.hpp>

//the big fish that chases the player. it's just a mouth facing right
struct BigFish {
	enum State {
		Waiting, //hidden off screen until its timer runs out
		Normal, //swims right
		Rocked, //hit by rocks, stops moving
		Stunned, //blinded, stops moving
	} state = Waiting;

	//middle of the mouth
	glm::vec2 position = glm::vec2(0.0f);
	//how wide each jaw is. the height follows the png's shape
	float size = 3.0f;
	float speed = 2.0f;

	//counts down while waiting, rocked or stunned
	float timer = 0.0f;
	//goes from 0 to 1 every time the mouth opens and closes
	float mouth_phase = 0.0f;

	static constexpr float MinDelay = 1.0f;
	static constexpr float MaxDelay = 5.0f;
	static constexpr float MouthPeriod = 2.0f;
	static constexpr float RockedTime = 5.0f;
	static constexpr float StunnedTime = 3.0f;

	//puts the fish at start. it starts swimming after a random wait between MinDelay and MaxDelay seconds
	void reset(glm::vec2 const &start);
	void update(float elapsed);
	void rock();
	void stun();

	//how far apart the jaws are right now
	float gap() const;
	//true if p is between the open jaws or a bit in front of them, and the fish isn't stopped
	bool can_suck(glm::vec2 const &p) const;

	void draw(glm::mat4 const &world_to_clip) const;
};
