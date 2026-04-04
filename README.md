# Counter-Strike: Source Enhanced

Discord server: [Permanent Link](https://discord.gg/WGeNnBksK5)

It has been started from a fork of from [nillerusr](https://github.com/nillerusr/source-engine), a huge thanks to him for having port most of the valve project generator stuff to waf.

## How to build:
First install dependencies: https://github.com/nillerusr/source-engine/wiki/Source-Engine-(EN) \
You also need:
- dxvk native library
- zstd library

To compile for CS:S Enhanced a command line can look like so:
```./waf configure clangdb install -p -o build -T fastnative --prefix ./gamedata/css_enhanced/game```

- `clangdb` generates compilation database for LSP
- `install` installs into the prefix `../css_enhanced` the binaries
- `-T fastnative`, compiler build the source code for a performance oriented release for your specific machine.
- `-d` for dedicated (server) build.
- `-P <0-4>` is to enable profiling with different levels, 1 is usually enough if you want to know what takes more framerate.

## How to play:

- Compile the game as above.
- Run the game with css_enhanced(.exe).
- I have my own server at cssserv.xutaxkamay.com, when it isn't down.

## What was enhanced:

A lot! Here is a comprehensive list of the core changes and enhancements based on our codebase rework:

### Networking & Synchronization
- **Precise Float Encoding:** Network compression (quantization) on floats was mostly removed, so the client receives full 32-bit exact values from the server, avoiding coordinate and rotation precision loss.
- **ZSTD Compression:** Implemented ZSTD compression with pre-trained dictionary data for entity updates, compressing/decompressing very fast for an excellent ratio (around 1/10).
- **TCP Snapshots:** Added option (`sv_send_snapshot_on_tcp`) to send reliable entity snapshots via TCP instead of UDP. Previously, if UDP packets containing entity updates were lost, the client would lack the data needed to interpolate entity positions. Because the client missed this history while the server maintained the true state, lag compensation would completely break down during packet loss. Sending snapshots asynchronously via TCP ensures the client always receives the entity updates, guaranteeing perfectly reliable lag compensation.
- **Tick Synchronization (TODO):** The server now sends back the client's perceived tick in `NET_Tick` to precisely manage clock drift. Snapshot tick IDs and command queue info are also tracked. *(Note: This feature is still a work-in-progress)*
- **Increased Network Limits:** Network rate limit raised drastically (up to 64 MB/s), frame history increased for better lag compensation window, and max user message size increased to support larger game events.

### Prediction, Hit Registration & Input
- **Trigger & Output Event Prediction:** Client-side trigger and solid movement prediction (trigger_push, trigger_multiple, trigger_teleport ...). This eliminates the classic "mismatching between server and client" console warnings and interaction delay. *(Note: Output event prediction still misses `logic_*` entities and some others like doors, buttons).*
- **Local Player Interpolation:** Completely rewritten to be time-based (`cl_interpolation_amount`) rather than ratio-based. Additionally, interpolated network variables for the local player are now explicitly calculated during prediction and user command processing using `interpolation_amount_frac`, ensuring perfectly accurate interpolation across all framerates without relying on sub-ticks.
- **Lag Compensation Overhaul:** The entire server-side lag compensation system was rewritten from scratch. Instead of the old system that just stored basic position data, it now uses a proper interpolation framework that smoothly tracks everything: player positions, angles, animations, and movement states. This means the server can accurately reconstruct exactly what the client saw at any point in time, resulting in much more reliable hit registration. The system is also designed to eventually support lag compensation for non-player entities like moving platforms and triggers.
- **View Angle Synchronization:** The mouse view angle update (`CL_ExtraMovementUpdate`) was moved to occur *before* frame processing instead of after. Previously, the rendering view was always one frame ahead of the view angles sent to the server in `CreateMove`, causing a constant mismatch. This change ensures the server's view perfectly synchronizes with what you see on screen.
- **No Autobhop Lag:** Bunnyhopping movement operates perfectly smoothly.

### Performance & Engine Limits
- **Unlimited FPS:** `fps_max 0` is now possible without triggering speedhacking false positives, allowing framerates well over 1k FPS.
- **Input System Performance:** Removed the legacy Windows raw input system processing when in fullscreen mode, reclaiming around ~300 FPS.
- **DXVK Integration:** Added native SDL windowing flags to support DXVK rendering paths.

### Modding, Game Events & Features
- **Dynamic Game Events:** The network serialization for game events (`IGameEvent`) was completely rewritten. Instead of relying on strict schema definitions (`.res` files) where the client and server must agree on the event structure beforehand, events are now serialized dynamically over the network (sending key names and types on the fly). This allows attaching arbitrary data to events without engine restrictions, powering new events like `bullet_impact`, `bullet_hit_player`, and `player_lag_hitboxes`.
- **New Weapon:** Added the Barrett M82A1 sniper rifle.
- **Separate RCON Port:** RCON traffic has been moved to a separate dedicated port (27024) instead of sharing the main game traffic port.
- **Build & CI/CD:** Added automated GitHub Actions prerelease workflows for Windows and Linux AMD64, along with comprehensive `.clang-format` rules for modern C++ styling.

## Future Plans (TODO)
- **New Weapons:** Expanding the arsenal (e.g., Gluon Gun).
- **Shareable Custom Skins:** A system allowing players to use their own skins and have them visible to other players on the server.
- **Advanced Replay & SourceTV:** Upgraded demo recording and SourceTV that perfectly recreates the player's perspective by re-using lag compensation data.
- **Enhanced Hit Indicators:** Continued improvements to hit feedback (e.g., `cl_enable_hitmarks`).
- **Hitbox Upgrades:** Moving towards sphere/cylinder-based hitboxes for better precision.
- **Engine Bug Fixes:** Patching long-standing engine quirks like edge bugs and surf ramp bugs.
- **Social Features:** Implementing Avatars and a Clan system.
- **In-Game Timer:** A built-in timer for speedrunning/movement modes.
