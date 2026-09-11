DIRS:= pkg/arisble pkg/collector pkg/sse cmd/aristeus

TARGETS := all check clean install

.PHONY: $(TARGETS) $(DIRS) mocks

$(TARGETS): %:
	@for dir in $(DIRS); do $(MAKE) -C $$dir $@; done

mocks:
	mockery
