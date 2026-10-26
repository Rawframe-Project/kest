# Copies objects into DEST under the name of their source with `.o`, the way
# tools/check-dead.sh reads them: build/release/vm.o, build/debug/main.o.
#
#   cmake -DDEST=<dir> -DOBJECTS=<a|b|...> -P cmake/CopyObjects.cmake
file(MAKE_DIRECTORY "${DEST}")
string(REPLACE "|" ";" objects "${OBJECTS}")
foreach(object IN LISTS objects)
  get_filename_component(name "${object}" NAME)
  string(REGEX REPLACE "\\.c\\.(o|obj)$" ".o" name "${name}")
  file(COPY_FILE "${object}" "${DEST}/${name}" ONLY_IF_DIFFERENT)
endforeach()
