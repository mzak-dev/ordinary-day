#!/usr/bin/env python
# Builds the Ordinary Day GDExtension into game/bin/.
#   scons platform=macos arch=arm64 target=template_debug

env = SConscript("godot-cpp/SConstruct")

env.Append(CPPPATH=["src/"])
sources = Glob("src/core/*.cpp") + Glob("src/godot/*.cpp")

library = env.SharedLibrary(
    "game/bin/libordinaryday{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
    source=sources,
)

Default(library)
