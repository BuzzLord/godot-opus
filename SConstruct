#!/usr/bin/env python
import os
import sys

env = SConscript("thirdparty/godot-cpp/SConstruct")

# For reference:
# - CCFLAGS are compilation flags shared between C and C++
# - CFLAGS are for C-specific compilation flags
# - CXXFLAGS are for C++-specific compilation flags
# - CPPFLAGS are for pre-processor flags
# - CPPDEFINES are for pre-processor defines
# - LINKFLAGS are for linking flags

# tweak this if you want to use different folders, or more folders, to store your source code in.
env.Append(CPPPATH=["src/", "thirdparty/opus/include/"])
sources = Glob("src/*.cpp")

if env["platform"] == "windows":
    env.Append(LIBPATH=["thirdparty/opus/build/Release"])
    env.Append(LIBS="opus.lib")
else:
    env.Append(LIBPATH=["thirdparty/opus/build"])
    env.Append(LIBS="libopus")

if env["platform"] == "macos" or env["platform"] == "ios":
    library = env.SharedLibrary(
        "bin/addons/godot_opus/bin/libgodot_opus{}.framework/libgodot_opus{}".format(
            env["suffix"], env["suffix"]
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "bin/addons/godot_opus/bin/libgodot_opus{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
