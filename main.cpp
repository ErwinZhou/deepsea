//Mode.hpp declares the "Mode::current" static member variable, which is used to decide where event-handling, updating, and drawing events go:
#include "Mode.hpp"

//The 'PlayMode' mode plays the game:
#include "PlayMode.hpp"

//For asset loading:
#include "Load.hpp"

//For sound init:
#include "Sound.hpp"

//GL.hpp will include a non-namespace-polluting set of opengl prototypes:
#include "GL.hpp"

//for screenshots:
#include "load_save_png.hpp"

//Includes for libSDL:
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

//...and for c++ standard library functions:
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>

#ifdef _WIN32
extern "C" { uint32_t GetACP(); }
#endif
int main(int argc, char **argv) {
	PlayMode::LevelId start_level = PlayMode::LevelId::One;
	if (argc != 1) {
		if (argc != 3 || std::string(argv[1]) != "--level" || (std::string(argv[2]) != "1" && std::string(argv[2]) != "2")) {
			std::cerr << "Usage: game [--level 1|2]\n";
			return 1;
		}
		start_level = std::string(argv[2]) == "1" ? PlayMode::LevelId::One : PlayMode::LevelId::Two;
	}
#ifdef _WIN32
	{ //when compiled on windows, check that code page is forced to utf-8 (makes file loading/saving work right):
		//see: https://docs.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page
		uint32_t code_page = GetACP();
		if (code_page == 65001) {
			std::cout << "Code page is properly set to UTF-8." << std::endl;
		} else {
			std::cout << "WARNING: code page is set to " << code_page << " instead of 65001 (UTF-8). Some file handling functions may fail." << std::endl;
		}
	}

	//when compiled on windows, unhandled exceptions don't have their message printed, which can make debugging simple issues difficult.
	try {
#endif

	//------------  initialization ------------

	//Initialize SDL library:
	SDL_Init(SDL_INIT_VIDEO);

	//based on https://wiki.libsdl.org/SDL3/SDL_GLContextFlag
	SDL_GL_ResetAttributes();
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif

	//create window:
	Mode::window = SDL_CreateWindow(
		"DeepSea",
		1280, 720, //TODO: modify window size if you'd like
		SDL_WINDOW_OPENGL
		| SDL_WINDOW_RESIZABLE //uncomment to allow resizing
		| SDL_WINDOW_HIGH_PIXEL_DENSITY //uncomment for full resolution on high-DPI screens
	);

	//prevent exceedingly tiny windows when resizing:
	SDL_SetWindowMinimumSize(Mode::window,100,100);

	if (!Mode::window) {
		std::cerr << "Error creating SDL window: " << SDL_GetError() << std::endl;
		return 1;
	}

	//Create OpenGL context:
	SDL_GLContext context = SDL_GL_CreateContext(Mode::window);

	if (!context) {
		SDL_DestroyWindow(Mode::window);
		std::cerr << "Error creating OpenGL context: " << SDL_GetError() << std::endl;
		return 1;
	}

	//On windows, load OpenGL entrypoints: (does nothing on other platforms)
	init_GL();

	//based on https://wiki.libsdl.org/SDL3/SDL_GL_SetSwapInterval
	if (!SDL_GL_SetSwapInterval(1)) {
		std::cerr << "NOTE: couldn't set vsync (" << SDL_GetError() << ")." << std::endl;
	}

	//Set automatic SRGB encoding if framebuffer needs it:
	glEnable(GL_FRAMEBUFFER_SRGB);

	//Hide mouse cursor (note: showing can be useful for debugging):
	//SDL_ShowCursor(SDL_DISABLE);

	//------------ init sound --------------
	Sound::init();

	//------------ load assets --------------
	call_load_functions();

	//------------ create game mode + make current --------------
	Mode::set_current(std::make_shared<PlayMode>(start_level));

	//------------ main loop ------------

	//this inline function will be called whenever the window is resized,
	// and will update the window_size and drawable_size variables:
	glm::uvec2 window_size; //size of window (layout pixels)
	glm::uvec2 drawable_size; //size of drawable (physical pixels)
	//On non-highDPI displays, window_size will always equal drawable_size.
	auto on_resize = [&](){
		int w,h;
		SDL_GetWindowSize(Mode::window, &w, &h);
		window_size = glm::uvec2(w, h);
		SDL_GetWindowSizeInPixels(Mode::window, &w, &h);
		drawable_size = glm::uvec2(w, h);
		glViewport(0, 0, drawable_size.x, drawable_size.y);
	};
	on_resize();
	auto previous_time = std::chrono::steady_clock::now();
	auto next_frame = previous_time;
	constexpr auto FramePeriod = std::chrono::nanoseconds(1000000000 / 60);

	//This will loop until the current mode is set to null:
	while (Mode::current) {
		//every pass through the game loop creates one frame of output
		//  by performing three steps:

		{ //(1) process any events that are pending
			static SDL_Event evt;
			while (SDL_PollEvent(&evt)) {
				//ignore key repeat:
				if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.repeat) {
					continue;
				}
				//handle resizing:
				if (evt.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
					on_resize();
				}
				//handle input:
				if (Mode::current && Mode::current->handle_event(evt, window_size)) {
					// mode handled it; great
				} else if (evt.type == SDL_EVENT_QUIT) {
					Mode::set_current(nullptr);
					break;
				} else if (evt.type == SDL_EVENT_KEY_DOWN && evt.key.key == SDLK_PRINTSCREEN) {
					// --- screenshot key ---
					std::string filename = "screenshot.png";
					std::cout << "Saving screenshot to '" << filename << "'." << std::endl;
					glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
					glReadBuffer(GL_FRONT);
					int w,h;
					SDL_GetWindowSizeInPixels(Mode::window, &w, &h);
					std::vector< glm::u8vec4 > data(w*h);
					glReadPixels(0,0,w,h, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
					for (auto &px : data) {
						px.a = 0xff;
					}
					save_png(filename, glm::uvec2(w,h), data.data(), LowerLeftOrigin);
				}
			}
			if (!Mode::current) break;
		}

		//based on https://wiki.libsdl.org/SDL3/SDL_GetWindowFlags
		SDL_WindowFlags flags = SDL_GetWindowFlags(Mode::window);
		if ((flags & SDL_WINDOW_MINIMIZED) || !(flags & SDL_WINDOW_INPUT_FOCUS)) {
			SDL_Delay(100);
			previous_time = std::chrono::steady_clock::now();
			next_frame = previous_time;
			continue;
		}

		{ //(2) call the current mode's "update" function to deal with elapsed time:
			auto current_time = std::chrono::steady_clock::now();
			float elapsed = std::chrono::duration< float >(current_time - previous_time).count();
			previous_time = current_time;

			//if frames are taking a very long time to process,
			//lag to avoid spiral of death:
			elapsed = std::min(0.1f, elapsed);

			Mode::current->update(elapsed);
			if (!Mode::current) break;
		}

		{ //(3) call the current mode's "draw" function to produce output:
		
			Mode::current->draw(drawable_size);
		}

		//Wait until the recently-drawn frame is shown before doing it all again:
		SDL_GL_SwapWindow(Mode::window);
		next_frame += FramePeriod;
		auto now = std::chrono::steady_clock::now();
		if (next_frame > now) {
			//based on https://wiki.libsdl.org/SDL3/SDL_DelayNS
			SDL_DelayNS(Uint64(std::chrono::duration_cast<std::chrono::nanoseconds>(next_frame - now).count()));
		} else {
			next_frame = now;
		}
	}


	//------------  teardown ------------
	Mode::set_current(nullptr);
	Sound::shutdown();

	SDL_GL_DestroyContext(context);
	context = 0;

	SDL_DestroyWindow(Mode::window);
	Mode::window = NULL;

	return 0;

#ifdef _WIN32
	} catch (std::exception const &e) {
		std::cerr << "Unhandled exception:\n" << e.what() << std::endl;
		return 1;
	} catch (...) {
		std::cerr << "Unhandled exception (unknown type)." << std::endl;
		throw;
	}
#endif
}
