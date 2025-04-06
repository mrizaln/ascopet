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

# concurrentqueue
# ~~~~~~~~~~~~~~~
FetchContent_Declare(
  concurrentqueue
  GIT_REPOSITORY https://github.com/cameron314/concurrentqueue
  GIT_TAG v1.0.4)
FetchContent_MakeAvailable(concurrentqueue)

add_library(fetch::concurrentqueue ALIAS concurrentqueue)
# ~~~~~~~~~~~~~~~
