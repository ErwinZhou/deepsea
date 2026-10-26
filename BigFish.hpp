#pragma once

#include "DrawSprites.hpp"

#include <glm/glm.hpp>

//the big fish that chases the player. only its open jaws show, coming in from the left
struct BigFish {
	BigFish();

	enum State {
		Waiting, //hidden off screen until enter() is called
		Normal, //swims right
		Stunned, //pinned by falling rock, stops moving
	} state = Waiting;

	//left edge of the jaw pngs, at the line where the two jaws meet. the jaws turn around this point
	glm::vec2 position = glm::vec2(0.0f);
	//how wide each jaw png is drawn. the height follows the png's shape
	float size = 75.0f;
	//a bit slower than the small fish, so staying ahead takes keeping moving
	float speed = 2.5f;

	//counts down while stunned
	float timer = 0.0f;
	//counts up from 0 while biting, below 0 when not biting
	float bite_time = -1.0f;
	//seconds since reset, drives the slow bob and the breathing of the mouth
	float time = 0.0f;
	//height the fish bobs around
	float base_y = 0.0f;
	//true for the one update where the jaws slam shut
	bool slammed = false;

	//puts the fish at start, hidden until enter() is called
	void reset(glm::vec2 const &start);
	//centers the open mouth on the world's height and hides the fish off its left edge
	void reset_for_world(glm::vec2 const &bounds_min, glm::vec2 const &bounds_max);
	//starts the fish swimming with the front of its art at from_x
	void enter(float from_x);
	void update(float elapsed);
	//stops the fish for this many seconds
	void stun(float seconds);
	//starts closing the jaws. does nothing if they are already closing
	void bite();
	bool biting() const;

	//0 when the mouth is open, 1 when it's shut
	float closed() const;
	//how far in front of position any part of the jaws reaches
	float art_front() const;
	//x of the front of the teeth. anything left of this is inside the mouth
	float teeth_x() const;
	//how far to shift the view to shake the screen after a slam
	glm::vec2 shake() const;

	void draw(DrawSprites const &sprites, glm::mat4 const &world_to_clip) const;

	SpriteTexture upper_jaw;
	SpriteTexture lower_jaw;
};
