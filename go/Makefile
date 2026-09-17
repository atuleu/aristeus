DIRS:= pkg/arisble pkg/collector pkg/sse pkg/emc2101 cmd/aristeus cmd/aristeus-node-monitor

TARGETS := all check clean install

.PHONY: $(TARGETS) $(DIRS) mocks

$(TARGETS): %:
	@for dir in $(DIRS); do $(MAKE) -C $$dir $@; done

mocks:
	mockery
