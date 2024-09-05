# Counter-Strike: Source Enhanced

Started from a fork of https://github.com/nillerusr/source-engine \ 
A huge thanks to him for having port most of the valve project generator stuff to waf.

## How to build:
First install dependencies: https://github.com/nillerusr/source-engine/wiki/Source-Engine-(EN) \
You also need zstd library.

To compile for CS:S Enhanced a command line can look like so:
```./waf configure clangdb install -p -o build --use-ccache -T fastnative --prefix ../css_enhanced```

- `clangdb` generates compilation database for LSP
- `install` installs into the prefix `../css_enhanced` the binaries
- `-T fastnative`, compiler build the source code for a performance oriented release for your specific machine.
- `-d` for dedicated (server) build.
- `-P <0-4>` is to enable profiling with different levels, 1 is usually enough if you want to know what takes more framerate.

## How to play:

You can join my Discord server at to get a release: https://discord.gg/e8nbakt8

OR:

- First, you need original CS:S & HL2 files.
- Copy the original CS:S folder somewhere
- Overwrite the hl2 and platform folder from the Half-Life 2 files so that shaders can work.\
This is because stdshaders needs some rewrite to make it work basically with sdk 2013, can't do it now yet.\
- Run the game with hl2_launcher(.exe) -game cstrike.
- I have my own server at cssserv.xutaxkamay.com, when it isn't down.

## What was enhanced:

A lot! If you're curious you can look on the commit log, but here's the most interesting things:

- Trigger prediction, output event prediction. (this caused the player's mistmatching between server and client)
- Local player interpolation fixed without doing sub-ticks. (this is known as your screen not matching the server/client's tick view)
- Lag compensation fixed. (Animations are server controlled now)
- SetupBones being different from client and server due to the fact that IKs weren't enabled.
- No autobhop lag.
- fps_max 0 is possible now without speedhacking. (you can get more than 1k fps)
- More performance in general, for example in Windows, I removed the legacy input system that took around 300 fps in fullscreen.
- You can use ARM servers to run a dedicated server (this is huge, since servers have a lot of power consumption).
- Network compression mostly removed so the client get the exact values from the server
- zstd compression with trained data so that it compress (and decompress) very fast for a very good ratio. (around 1/10)

And more to come (TODO):
- New weapons coming, thinking gluon gun & m82a1
- Shareable skins where you can use your own skins and other people will see them.
- And much more to come ...
- Replay
- Source TV and recordings that can display the exact things that the player has seen on his screen by re-using lag compensation.
- Hit indicator
- A timer
- m_rawinput 2 that aligns the angles to a tick.
- General bug fixes like edge bugs, surf ramp bugs ...
- Sphere/cylinder hitboxes
