# C++ Project Makefile

CXX      := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic
INCLUDES := -I include
LDFLAGS  := -lssl -lcrypto

TARGET    := app
SRC_DIR   := src
BUILD_DIR := build
CERT_DIR  := certs
CERT_DIR  := data

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean debug release certs

all: certs $(TARGET)

debug: CXXFLAGS += -g -DDEBUG
debug: certs $(TARGET)

release: CXXFLAGS += -O2 -DNDEBUG
release: certs $(TARGET)

$(TARGET): $(OBJS) $(DATA_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# TODO create quota on this data directory
$(DATA_DIR):
	mkdir -p $(DATA_DIR)

# Generate a self-signed certificate if one does not already exist.
certs: $(CERT_DIR)/server.crt

$(CERT_DIR)/server.crt: | $(CERT_DIR)
	openssl req -x509 -newkey rsa:2048 -keyout $(CERT_DIR)/server.key \
	    -out $(CERT_DIR)/server.crt -days 365 -nodes \
	    -subj "/CN=localhost"

$(CERT_DIR):
	mkdir -p $(CERT_DIR)

-include $(DEPS)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
