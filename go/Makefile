DIRS:= pkg/arisble cmd/aristeus

TARGETS := all check clean install

.PHONY: $(TARGETS) $(DIRS)

$(TARGETS): %:
	@for dir in $(DIRS); do $(MAKE) -C $$dir $@; done
