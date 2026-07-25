CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu11 -g
LDFLAGS = -lm
SRCDIR = src
OBJDIR = obj
TARGET = oyster

SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $^ -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@ -lm

clean:
	rm -rf $(OBJDIR) $(TARGET) *.oce modules/*.ocm

install: $(TARGET)
	sudo cp $(TARGET) /usr/local/bin/

# Тест модульной системы
test-modules: $(TARGET)
	@echo "=== Compiling math module ==="
	@echo 'const PI = 3.14159' > modules/math.osm
	@echo '' >> modules/math.osm
	@echo 'fun add($$a, $$b) {' >> modules/math.osm
	@echo '    return $$a + $$b' >> modules/math.osm
	@echo '}' >> modules/math.osm
	@echo '' >> modules/math.osm
	@echo 'fun mul($$a, $$b) {' >> modules/math.osm
	@echo '    return $$a * $$b' >> modules/math.osm
	@echo '}' >> modules/math.osm
	@echo '' >> modules/math.osm
	@echo 'export add' >> modules/math.osm
	@echo 'export mul' >> modules/math.osm
	@echo "=== Creating test.osf ==="
	@echo 'use "math" as M' > test.osf
	@echo '' >> test.osf
	@echo '$$x = 10' >> test.osf
	@echo '$$y = 20' >> test.osf
	@echo '$$z = M.add($$x, $$y)' >> test.osf
	@echo 'print($$z)' >> test.osf
	@echo '' >> test.osf
	@echo '$$w = M.mul($$x, $$y)' >> test.osf
	@echo 'print($$w)' >> test.osf
	@echo '' >> test.osf
	@echo '$$circle = 2 * &M.PI * $$x' >> test.osf
	@echo 'print($$circle)' >> test.osf
	@echo "=== Running ==="
	./$(TARGET) test.osf

# Быстрый тест
test: $(TARGET)
	@echo "=== Testing print(42) ==="
	@echo 'print(42)' > test.osf
	./$(TARGET) test.osf
	@echo ""
	@echo "=== Testing print('Hello') ==="
	@echo 'print("Hello")' > test.osf
	./$(TARGET) test.osf
	@echo ""
	@echo "=== Testing .oce execution ==="
	./$(TARGET) -c test.osf
	./$(TARGET) test.oce
	@rm -f test.osf test.oce

.PHONY: all clean install test test-modules
