# C++ Project Makefile

TOTP_SECRET_FILE := $(HOME)/.totp.secret.h
ifeq (,$(wildcard $(TOTP_SECRET_FILE)))
$(warning Warning: TOTP file not found, run `make totp` to generate the file.)
TOTP_INCLUDE :=
else
TOTP_INCLUDE := --include $(TOTP_SECRET_FILE)
endif

ifeq (,$(DOMAIN))
$(error Error: Cannot find a domain name for which to generate the SSL certificates.)
endif

CXX      := g++
CXXFLAGS := -std=c++11 -Wall -Wextra -Wpedantic
INCLUDES := -I include $(TOTP_INCLUDE)
LDFLAGS  := -lssl -lcrypto

TARGET    := app
SRC_DIR   := src
BUILD_DIR := build
CERT_DIR  := certs
DATA_DIR  := data

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp, $(BUILD_DIR)/%.o, $(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all clean debug release certs

all: certs $(TARGET)

debug: CXXFLAGS += -g -DDEBUG
debug: certs $(TARGET)

release: CXXFLAGS += -O2 -DNDEBUG
release: certs $(TARGET)


totp: $(TOTP_SECRET_FILE)
	@echo "$$(tr -dc 'A-Z2-7' < /dev/urandom | head -c 32)" > $(TOTP_SECRET_FILE)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# TODO create quota on this data directory
$(DATA_DIR):
	mkdir -p $(DATA_DIR)
	mkdir -p $(DATA_DIR)/form
	mkdir -p $(DATA_DIR)/secret
	mkdir -p $(DATA_DIR)/files

# Generate a self-signed certificate if one does not already exist.
certs: $(CERT_DIR)/server.crt

$(CERT_DIR)/server.crt:
	mkdir -p $(CERT_DIR)
	openssl req -x509 -newkey rsa:2048 -keyout $(CERT_DIR)/server.key \
	    -out $(CERT_DIR)/server.crt -days 365 -nodes \
	    -subj "/CN=$(DOMAIN)"

#_acme-challenge.grieco.tplinkdns.com
cert-test:
	./acme.sh --issue --dns -d $(DOMAIN)--yes-I-know-dns-manual-mode-enough-go-ahead-please
	./acme.sh --renew -d $(DOMAIN) --yes-I-know-dns-manual-mode-enough-go-ahead-please

$(CERT_DIR):
	mkdir -p $(CERT_DIR)

-include $(DEPS)

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(CERT_DIR)
