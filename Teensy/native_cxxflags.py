# Applies C++-only compiler flags for the native test env (build_flags in
# platformio.ini reach the C compiler too, and CMSIS-DSP is plain C).
Import("env")

env.Append(CXXFLAGS=["-std=gnu++17"])
