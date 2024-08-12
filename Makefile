
ifneq ($(shell [ -d ${SDK_BUILD_IMAGES_DIR}/sdcard/ ] && echo 1 || echo 0),1)
$(shell mkdir -p ${SDK_BUILD_IMAGES_DIR}/sdcard/)
endif

.PHONY: all clean distclean

.PHONY: copy_freetype_fonts
copy_freetype_fonts:
	@echo "Copy freetype resources"
	@if [ ! -d ${SDK_BUILD_IMAGES_DIR}/sdcard/res/font ]; then \
		mkdir -p ${SDK_BUILD_IMAGES_DIR}/sdcard/res/font/; \
	fi; \
	rsync -aq --delete $(SDK_CANMV_SRC_DIR)/resources/font/ ${SDK_BUILD_IMAGES_DIR}/sdcard/res/font/

.PHONY: copy_libs
copy_libs:
	@echo "Copy libs"
	@if [ ! -d ${SDK_BUILD_IMAGES_DIR}/sdcard/libs ]; then \
		mkdir -p ${SDK_BUILD_IMAGES_DIR}/sdcard/libs/; \
	fi; \
	rsync -aq --delete $(SDK_CANMV_SRC_DIR)/resources/libs/ ${SDK_BUILD_IMAGES_DIR}/sdcard/libs/

.PHONY: copy_examples
copy_examples:
	@echo "Copy examples"
	@if [ ! -d ${SDK_BUILD_IMAGES_DIR}/sdcard/examples ]; then \
		mkdir -p ${SDK_BUILD_IMAGES_DIR}/sdcard/examples/; \
	fi; \
	rsync -aq --delete --exclude='.git' $(SDK_CANMV_SRC_DIR)/resources/examples/ ${SDK_BUILD_IMAGES_DIR}/sdcard/examples/

.PHONY: copy_micropython
copy_micropython:
	@echo "Copy micropython"
	@if [ ! -e $(SDK_CANMV_BUILD_DIR)/micropython ]; then \
		echo "micropython not exists." && exit 1; \
	fi; \
	cp -rf $(SDK_CANMV_BUILD_DIR)/micropython ${SDK_BUILD_IMAGES_DIR}/sdcard/

.PHONY: build
build:
	@$(MAKE) -j$(NCPUS) -C port || exit $?;

.PHONY: gen_image
gen_image: build copy_freetype_fonts copy_libs copy_examples copy_micropython

all: gen_image
	@echo "Make canmv done."

clean:
	@rm -rf ${SDK_BUILD_IMAGES_DIR}/sdcard/
	@make -C port clean

distclean: clean
