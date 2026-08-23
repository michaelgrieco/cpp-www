# cpp-www

A C++ project.

## Project Structure

```
cpp-www/
├── include/        # Public header files (.h / .hpp)
├── src/
│   └── main.cpp    # Entry point
├── build/          # Compiled objects and dependency files (generated)
├── Makefile
└── README.md
```

## Build

```sh
# Default build
make

# Debug build
make debug

# Optimised release build
make release

# Clean build artefacts
make clean
```

The compiled binary is placed at `./app`.
