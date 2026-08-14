all: arisble check


arisble:
	make -C pkg/arisble

check: arisble
	make -C pkg/arisble check

clean:
	echo "Nothing to clean yet"

.PHONE: clean check arisble
