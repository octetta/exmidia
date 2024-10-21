TARGETS = \
synth \
#

all: $(TARGETS)

synth: synth.c
	gcc -g $< -o $@ linenoise.c -lasound -lm
