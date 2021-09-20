GIT_MOD = $(shell [ -z "$$(git status --short)" ] || echo -dirty)
GIT_TAG = $(shell git describe --tags)$(GIT_MOD)
GIT_REV = $(shell git rev-parse HEAD)

# Create config.h
CONFIG_H = src/config.h
CONFIG_1 = "\#define GIT_TAG \"$(GIT_TAG)\""
CONFIG_2 = "\#define GIT_REV \"$(GIT_REV)\""
$(shell [ -f $(CONFIG_H) ] || touch $(CONFIG_H))
$(shell [ -z "$$(echo "$(CONFIG)" | diff $(CONFIG_H) -)" ] || echo "$(CONFIG_1)" > $(CONFIG_H); echo "$(CONFIG_2)" >> $(CONFIG_H))

all:
	pio run

upload:
	pio run -t upload

menuconfig:
	pio run -t menuconfig
