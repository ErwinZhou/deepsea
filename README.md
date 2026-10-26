# DeepSea

A 2D underwater horror game. You are a small fish in a cave, and something much bigger is coming.

Team: Haoran Xie, Yuchen Zhou, Sarah Garland.

## Build and run

You need Node.js, a C++20 compiler, and the course's `nest-libs` in a folder next to this one. [NEST.md](NEST.md) has the full setup.

```sh
node Maekfile.js
./dist/game
```

On Windows, run `dist\game.exe`. Escape quits.

Built on the course's base code. See [LICENSE](LICENSE).

Credits: the fish, jaw, rock and wall art in `assets/` was drawn by Sarah Garland for this game. `cave_rock.png` and `exit.png` are flat placeholders we generated ourselves and will be replaced. Code taken from or modeled on a specific page cites it with a `based on` comment where it is used. That includes cppreference, Wikipedia's rotation matrix and smoothstep pages, and a hit stop write-up from uhiyama-lab.com. Everything else builds on the course's NEST base code.
