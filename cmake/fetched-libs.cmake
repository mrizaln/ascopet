set(FETCHCONTENT_QUIET FALSE)

include(FetchContent)

# circbuf
# ~~~~~~~
FetchContent_Declare(
  circbuf
  GIT_REPOSITORY https://github.com/mrizaln/circbuf
  GIT_TAG v0.1.0)
FetchContent_MakeAvailable(circbuf)

add_library(fetch::circbuf ALIAS circbuf)
# ~~~~~~~
