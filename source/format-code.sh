find . -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.c" -o -name "*.hpp" \) -print0 | xargs -0 clang-format -i -style=file

