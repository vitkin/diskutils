.PHONY: all help clean install bindiff binpatch sparsefile-rescue sparsefile-restore

BINARY_DIR := build
INSTALL_DIR := /usr/local/bin
$(shell mkdir -p $(BINARY_DIR))

help:
	@echo make all bindiff binpatch sparsefile-rescue sparsefile-restore
	@echo make install clean
	@echo

sparsefile-rescue bindiff:
	$(MAKE) --jobs=4 --directory=$(BINARY_DIR) --makefile=../Makefile.build $(filter-out clean,$(MAKECMDGOALS))

binpatch sparsefile-restore:
	$(MAKE) --jobs=4 --directory=$(BINARY_DIR) --makefile=../Makefile.build sparsefile-restore 
	$(shell ln -fs sparsefile-restore build/binpatch)

all:
	$(MAKE) --jobs=4 --directory=$(BINARY_DIR) --makefile=../Makefile.build sparsefile-rescue sparsefile-restore bindiff
	$(shell ln -fs sparsefile-restore build/binpatch)

clean:
	rm -f $(BINARY_DIR)/*

install:
	@echo installing files into $(INSTALL_DIR)
	$(foreach t,sparsefile-rescue sparsefile-restore bindiff,$(shell test -e build/$t && install build/$t $(INSTALL_DIR)))
	$(shell ln -fs sparsefile-restore $(INSTALL_DIR)/binpatch)
