CC := cc
AR := ar

BUILD_DIR := build

COMMON_CFLAGS := -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables
COMMON_CPPFLAGS := -Isubprojects/rt/include -Isubprojects/platform/include -Isubprojects/util/include -Isubprojects/arena/include -Isubprojects/unicode/include -Isubprojects/crc32/include -Isubprojects/deflate/include -Isubprojects/zip/include -Isubprojects/xml/include -Isubprojects/doc_model/include -Iprojects/convert_core/include -Iprojects/fmt_markdown/include -Iprojects/fmt_odt/include -Iprojects/odt_core/include -Iprojects/odt_cli/include

RT_OBJS := \
	$(BUILD_DIR)/rt_sys.o \
	$(BUILD_DIR)/rt_string.o

PLATFORM_OBJS := \
	$(BUILD_DIR)/platform_fs.o

UTIL_OBJS := \
	$(BUILD_DIR)/util.o

ARENA_OBJS := \
	$(BUILD_DIR)/arena.o

UNICODE_OBJS := \
	$(BUILD_DIR)/unicode.o

CRC32_OBJS := \
	$(BUILD_DIR)/crc32.o

DEFLATE_OBJS := \
	$(BUILD_DIR)/deflate.o

ZIP_OBJS := \
	$(BUILD_DIR)/zip.o

XML_OBJS := \
	$(BUILD_DIR)/xml.o

DOC_MODEL_OBJS := \
	$(BUILD_DIR)/doc_model.o

ODT_CORE_OBJS := \
	$(BUILD_DIR)/odt_core.o

CONVERT_CORE_OBJS := \
	$(BUILD_DIR)/convert_core.o

FMT_MARKDOWN_OBJS := \
	$(BUILD_DIR)/fmt_markdown.o

FMT_ODT_OBJS := \
	$(BUILD_DIR)/fmt_odt.o

ODT_CLI_OBJS := \
	$(BUILD_DIR)/odt_cli_cli.o

ODT_CLI_MAIN_OBJ := $(BUILD_DIR)/odt_cli_main.o
TEST_MAIN_OBJ := $(BUILD_DIR)/test_main.o
TEST_PHASE0_OBJ := $(BUILD_DIR)/test_phase0.o
TEST_PHASE1_OBJ := $(BUILD_DIR)/test_phase1.o
TEST_PHASE2_OBJ := $(BUILD_DIR)/test_phase2.o
TEST_PHASE3_OBJ := $(BUILD_DIR)/test_phase3.o
TEST_PHASE4_OBJ := $(BUILD_DIR)/test_phase4.o
TEST_PHASE5_OBJ := $(BUILD_DIR)/test_phase5.o
TEST_PHASE6_OBJ := $(BUILD_DIR)/test_phase6.o
TEST_PHASE7_OBJ := $(BUILD_DIR)/test_phase7.o
TEST_PHASE8_OBJ := $(BUILD_DIR)/test_phase8.o
TEST_PHASE9_OBJ := $(BUILD_DIR)/test_phase9.o
TEST_PHASE10_OBJ := $(BUILD_DIR)/test_phase10.o
START_OBJ := $(BUILD_DIR)/start.o

RT_LIB := $(BUILD_DIR)/librt.a
PLATFORM_LIB := $(BUILD_DIR)/libplatform.a
UTIL_LIB := $(BUILD_DIR)/libutil.a
ARENA_LIB := $(BUILD_DIR)/libarena.a
UNICODE_LIB := $(BUILD_DIR)/libunicode.a
CRC32_LIB := $(BUILD_DIR)/libcrc32.a
DEFLATE_LIB := $(BUILD_DIR)/libdeflate.a
ZIP_LIB := $(BUILD_DIR)/libzip.a
XML_LIB := $(BUILD_DIR)/libxml.a
DOC_MODEL_LIB := $(BUILD_DIR)/libdoc_model.a
ODT_CORE_LIB := $(BUILD_DIR)/libodt_core.a
CONVERT_CORE_LIB := $(BUILD_DIR)/libconvert_core.a
FMT_MARKDOWN_LIB := $(BUILD_DIR)/libfmt_markdown.a
FMT_ODT_LIB := $(BUILD_DIR)/libfmt_odt.a
ODT_CLI_LIB := $(BUILD_DIR)/libodt_cli.a
TEST_BIN := $(BUILD_DIR)/test
ODT_CLI_BIN := $(BUILD_DIR)/odt_cli

.PHONY: all clean test odt_cli

all: test odt_cli

test: $(TEST_BIN)
	./$(TEST_BIN)

odt_cli: $(ODT_CLI_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/rt_sys.o: subprojects/rt/src/rt_sys.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/rt_string.o: subprojects/rt/src/rt_string.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/platform_fs.o: subprojects/platform/src/platform_fs.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/util.o: subprojects/util/src/util.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/arena.o: subprojects/arena/src/arena.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/unicode.o: subprojects/unicode/src/unicode.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/crc32.o: subprojects/crc32/src/crc32.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/deflate.o: subprojects/deflate/src/deflate.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/zip.o: subprojects/zip/src/zip.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/xml.o: subprojects/xml/src/xml.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/doc_model.o: subprojects/doc_model/src/doc_model.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/odt_core.o: projects/odt_core/src/odt_core.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/convert_core.o: projects/convert_core/src/convert_core.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/fmt_markdown.o: projects/fmt_markdown/src/fmt_markdown.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/fmt_odt.o: projects/fmt_odt/src/fmt_odt.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/odt_cli_cli.o: projects/odt_cli/src/cli.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/odt_cli_main.o: projects/odt_cli/src/main.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_main.o: projects/test/src/main.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase0.o: projects/test/src/phase0.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase1.o: projects/test/src/phase1.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase2.o: projects/test/src/phase2.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase3.o: projects/test/src/phase3.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase4.o: projects/test/src/phase4.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase5.o: projects/test/src/phase5.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase6.o: projects/test/src/phase6.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase7.o: projects/test/src/phase7.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase8.o: projects/test/src/phase8.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase9.o: projects/test/src/phase9.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/test_phase10.o: projects/test/src/phase10.c | $(BUILD_DIR)
	$(CC) $(COMMON_CFLAGS) $(COMMON_CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/start.o: subprojects/rt/src/start.S | $(BUILD_DIR)
	$(CC) -c $< -o $@

$(RT_LIB): $(RT_OBJS)
	$(AR) rcs $@ $(RT_OBJS)

$(PLATFORM_LIB): $(PLATFORM_OBJS)
	$(AR) rcs $@ $(PLATFORM_OBJS)

$(UTIL_LIB): $(UTIL_OBJS)
	$(AR) rcs $@ $(UTIL_OBJS)

$(ARENA_LIB): $(ARENA_OBJS)
	$(AR) rcs $@ $(ARENA_OBJS)

$(UNICODE_LIB): $(UNICODE_OBJS)
	$(AR) rcs $@ $(UNICODE_OBJS)

$(CRC32_LIB): $(CRC32_OBJS)
	$(AR) rcs $@ $(CRC32_OBJS)

$(DEFLATE_LIB): $(DEFLATE_OBJS)
	$(AR) rcs $@ $(DEFLATE_OBJS)

$(ZIP_LIB): $(ZIP_OBJS)
	$(AR) rcs $@ $(ZIP_OBJS)

$(XML_LIB): $(XML_OBJS)
	$(AR) rcs $@ $(XML_OBJS)

$(DOC_MODEL_LIB): $(DOC_MODEL_OBJS)
	$(AR) rcs $@ $(DOC_MODEL_OBJS)

$(ODT_CORE_LIB): $(ODT_CORE_OBJS)
	$(AR) rcs $@ $(ODT_CORE_OBJS)

$(CONVERT_CORE_LIB): $(CONVERT_CORE_OBJS)
	$(AR) rcs $@ $(CONVERT_CORE_OBJS)

$(FMT_MARKDOWN_LIB): $(FMT_MARKDOWN_OBJS)
	$(AR) rcs $@ $(FMT_MARKDOWN_OBJS)

$(FMT_ODT_LIB): $(FMT_ODT_OBJS)
	$(AR) rcs $@ $(FMT_ODT_OBJS)

$(ODT_CLI_LIB): $(ODT_CLI_OBJS)
	$(AR) rcs $@ $(ODT_CLI_OBJS)

$(TEST_BIN): $(START_OBJ) $(TEST_MAIN_OBJ) $(TEST_PHASE0_OBJ) $(TEST_PHASE1_OBJ) $(TEST_PHASE2_OBJ) $(TEST_PHASE3_OBJ) $(TEST_PHASE4_OBJ) $(TEST_PHASE5_OBJ) $(TEST_PHASE6_OBJ) $(TEST_PHASE7_OBJ) $(TEST_PHASE8_OBJ) $(TEST_PHASE9_OBJ) $(TEST_PHASE10_OBJ) $(ODT_CLI_LIB) $(FMT_ODT_LIB) $(FMT_MARKDOWN_LIB) $(CONVERT_CORE_LIB) $(ODT_CORE_LIB) $(DOC_MODEL_LIB) $(XML_LIB) $(ZIP_LIB) $(DEFLATE_LIB) $(CRC32_LIB) $(UNICODE_LIB) $(ARENA_LIB) $(UTIL_LIB) $(PLATFORM_LIB) $(RT_LIB)
	$(CC) -nostdlib -static -o $@ $(START_OBJ) $(TEST_MAIN_OBJ) $(TEST_PHASE0_OBJ) $(TEST_PHASE1_OBJ) $(TEST_PHASE2_OBJ) $(TEST_PHASE3_OBJ) $(TEST_PHASE4_OBJ) $(TEST_PHASE5_OBJ) $(TEST_PHASE6_OBJ) $(TEST_PHASE7_OBJ) $(TEST_PHASE8_OBJ) $(TEST_PHASE9_OBJ) $(TEST_PHASE10_OBJ) $(ODT_CLI_LIB) $(FMT_ODT_LIB) $(FMT_MARKDOWN_LIB) $(CONVERT_CORE_LIB) $(ODT_CORE_LIB) $(DOC_MODEL_LIB) $(XML_LIB) $(ZIP_LIB) $(DEFLATE_LIB) $(CRC32_LIB) $(UNICODE_LIB) $(ARENA_LIB) $(UTIL_LIB) $(PLATFORM_LIB) $(RT_LIB)

$(ODT_CLI_BIN): $(START_OBJ) $(ODT_CLI_MAIN_OBJ) $(ODT_CLI_LIB) $(FMT_ODT_LIB) $(FMT_MARKDOWN_LIB) $(CONVERT_CORE_LIB) $(ODT_CORE_LIB) $(DOC_MODEL_LIB) $(XML_LIB) $(ZIP_LIB) $(CRC32_LIB) $(DEFLATE_LIB) $(PLATFORM_LIB) $(RT_LIB)
	$(CC) -nostdlib -static -o $@ $(START_OBJ) $(ODT_CLI_MAIN_OBJ) $(ODT_CLI_LIB) $(FMT_ODT_LIB) $(FMT_MARKDOWN_LIB) $(CONVERT_CORE_LIB) $(ODT_CORE_LIB) $(DOC_MODEL_LIB) $(XML_LIB) $(ZIP_LIB) $(CRC32_LIB) $(DEFLATE_LIB) $(PLATFORM_LIB) $(RT_LIB)

clean:
	rm -rf $(BUILD_DIR)
