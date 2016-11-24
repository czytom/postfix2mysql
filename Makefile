#!/usr/bin

VERS = $(shell ./debvers)
RELS=2

VERSION=$(VERS)"-"$(RELS)
CURDIR?=`pwd`

# BSD stuffs
BSD_VERSION=$(VERS).$(RELS)
PKG_BUILD=mysqmail-$(BSD_VERSION)
BSD_ARCH_NAME=$(PKG_BUILD).tar.gz
BSD_DEST_DIR?=..
BSD_SOURCE_DIR=src/bsd
BSD_BUILD_DIR?=$(BSD_SOURCE_DIR)/tmp
BSD_CATEGORIES=sysutils
MAIN_PORT_PATH=$(BSD_CATEGORIES)/mysqmail
PORT_BUILD=$(BSD_BUILD_DIR)/$(MAIN_PORT_PATH)
SRC_COPY_DIR=$(CURDIR)/$(BSD_BUILD_DIR)/$(PKG_BUILD)
PKG_PLIST_BUILD=$(CURDIR)/${BSD_BUILD_DIR}/PKG_PLIST_BUILD

INSTALL?=install -D
INSTALL_DIR?=install -d

CFLAGS=-O2 -Wall -I/usr/local/include
CC=gcc $(CFLAGS)
SBIN_DIR?=/usr/sbin
MAN_DIR?=/usr/share/man
DESTDIR?=""
INSTALL?=install
LIBS=-lmysqlclient -ldotconf -L/usr/lib/mysql -L/usr/lib64/mysql -L/usr/local/lib -L/usr/local/lib/mysql
EXEC=mysqmail-pure-ftpd-logger mysqmail-postfix-logger mysqmail-courier-logger mysqmail-dovecot-logger
MYOB=mydaemon.o myconfig.o
HFILES=mydaemon.h myconfig.h

all: $(EXEC)

mydaemon.o: mydaemon.c mydaemon.h
	$(CC) -c $< -o $@

myconfig.o: myconfig.c myconfig.h
	$(CC) -c $< -o $@

mysqmail-pure-ftpd-logger: mysqmail-pure-ftpd-logger.c $(MYOB) $(HFILES)
	$(CC) $< $(MYOB) $(LIBS) -o $@

mysqmail-postfix-logger: mysqmail-postfix-logger.c $(MYOB) $(HFILES)
	$(CC) $< $(MYOB) $(LIBS) -o $@

mysqmail-qmail-logger: mysqmail-qmail-logger.c $(MYOB) $(HFILES)
	$(CC) $< $(MYOB) $(LIBS) -o $@

mysqmail-courier-logger: mysqmail-courier-logger.c $(MYOB) $(HFILES)
	$(CC) $< $(MYOB) $(LIBS) -o $@

mysqmail-dovecot-logger: mysqmail-dovecot-logger.c $(MYOB) $(HFILES)
	$(CC) $< $(MYOB) $(LIBS) -o $@

strip: all
	for i in $(EXEC) ; do strip $${i} ; done

clean:
	rm -rf *.o $(EXEC)

install-pure: mysqmail-pure-ftpd-logger
	$(INSTALL) -m 0755 mysqmail-pure-ftpd-logger $(DESTDIR)$(SBIN_DIR)/mysqmail-pure-ftpd-logger
	$(INSTALL) -m 0644 doc/mysqmail-pure-ftpd-logger.8 $(DESTDIR)$(MAN_DIR)/man8/mysqmail-pure-ftpd-logger.8
	gzip -9 $(DESTDIR)$(MAN_DIR)/man8/mysqmail-pure-ftpd-logger.8

install-post: mysqmail-postfix-logger
	$(INSTALL) -m 0755 mysqmail-postfix-logger $(DESTDIR)$(SBIN_DIR)/mysqmail-postfix-logger
	$(INSTALL) -m 0644 doc/mysqmail-postfix-logger.8 $(DESTDIR)$(MAN_DIR)/man8/mysqmail-postfix-logger.8
	gzip -9 $(DESTDIR)$(MAN_DIR)/man8/mysqmail-postfix-logger.8

install-qmail: mysqmail-qmail-logger
	$(INSTALL) -m 0755 mysqmail-qmail-logger $(DESTDIR)$(SBIN_DIR)/mysqmail-qmail-logger
	$(INSTALL) -m 0644 doc/mysqmail-qmail-logger.8 $(DESTDIR)$(MAN_DIR)/man8/mysqmail-qmail-logger.8
	gzip -9 $(DESTDIR)$(MAN_DIR)/man8/mysqmail-qmail-logger.8

install-courier: mysqmail-courier-logger
	$(INSTALL) -m 0755 mysqmail-courier-logger $(DESTDIR)$(SBIN_DIR)/mysqmail-courier-logger
	$(INSTALL) -m 0644 doc/mysqmail-courier-logger.8 $(DESTDIR)$(MAN_DIR)/man8/mysqmail-courier-logger.8
	gzip -9 $(DESTDIR)$(MAN_DIR)/man8/mysqmail-courier-logger.8

install-dovecot: mysqmail-dovecot-logger
	$(INSTALL) -m 0755 mysqmail-dovecot-logger $(DESTDIR)$(SBIN_DIR)/mysqmail-dovecot-logger
	$(INSTALL) -m 0644 doc/mysqmail-dovecot-logger.8 $(DESTDIR)$(MAN_DIR)/man8/mysqmail-dovecot-logger.8
	gzip -9 $(DESTDIR)$(MAN_DIR)/man8/mysqmail-dovecot-logger.8

install-conf:
	$(INSTALL) -m 0640 doc/mysqmail.conf $(DESTDIR)$(ETCDIR)/mysqmail.conf

install-conf-sample:
	$(INSTALL) -m 0640 doc/mysqmail.conf $(DESTDIR)$(ETCDIR)/mysqmail.conf.sample

install: all
	$(MAKE) strip
	$(MAKE) DESTDIR=$(DESTDIR) SBIN_DIR=$(SBIN_DIR) MAN_DIR=$(MAN_DIR) INSTALL=$(INSTALL) install-pure
	$(MAKE) DESTDIR=$(DESTDIR) SBIN_DIR=$(SBIN_DIR) MAN_DIR=$(MAN_DIR) INSTALL=$(INSTALL) install-post
	$(MAKE) DESTDIR=$(DESTDIR) SBIN_DIR=$(SBIN_DIR) MAN_DIR=$(MAN_DIR) INSTALL=$(INSTALL) install-courier
	$(MAKE) DESTDIR=$(DESTDIR) SBIN_DIR=$(SBIN_DIR) MAN_DIR=$(MAN_DIR) INSTALL=$(INSTALL) install-dovecot

dist:
	./dist

rpm:
	$(MAKE) dist
	VERS=`head -n 1 debian/changelog | cut -d'(' -f2 | cut -d')' -f1 | cut -d'-' -f1` ; \
	PKGNAME=`head -n 1 debian/changelog | cut -d' ' -f1` ; \
	cd .. ; rpmbuild -ta $${PKGNAME}-$${VERS}.tar.gz

deb:
	if [ -z $(SIGN)"" ] ; then \
		./deb ; \
	else \
		./deb --sign ; \
	fi

source-copy:
	@if [ -z $(DESTFOLDER) ] ; then echo "Please set DESTFOLDER=" ; exit 1 ; fi
	@echo "-> Copying sources"
	@mkdir -p $(DESTFOLDER)
	@cp -rf doc Makefile etc my* $(DESTFOLDER)
	@mkdir -p $(DESTFOLDER)/src/bsd/tmp/$(PKG_BUILD)
	@mkdir -p $(DESTFOLDER)/src/bsd/mysqmail
	@for i in $(BSD_MAKE_PKG_SOURCES) ; do $(INSTALL) -m 0644 $$i $(DESTFOLDER)/$$i ; done

bsd-ports-packages:
	@echo "--- Making source snapshot $(BSD_ARCH_NAME) ---"
	@mkdir -p $(BSD_BUILD_DIR)
	@echo "-> Copying source package files with make source-copy DESTFOLDER=$(SRC_COPY_DIR)"
	@make source-copy DESTFOLDER=$(SRC_COPY_DIR)
	@cd $(BSD_BUILD_DIR) && tar -czf $(BSD_ARCH_NAME) $(PKG_BUILD) && cd $(CURDIR)
	@if ! [ $(BSD_DEST_DIR) = . -o $(BSD_DEST_DIR) = ./ -o $(BSD_DEST_DIR) = $(CURDIR) ] ; then mv $(BSD_BUILD_DIR)/$(BSD_ARCH_NAME) $(BSD_DEST_DIR)/ ; fi
	@echo " --- Succesfully made BSD source snapshot ${BSD_DEST_DIR}/${BSD_ARCH_NAME} ---"

	@echo " --- Making BSD port tree for version "${BSD_VERSION}" ---"
	@echo "===> Creating port files in $(PORT_BUILD)"
	@mkdir -p $(PORT_BUILD)
	@mkdir -p $(PORT_BUILD)/files
	@sed "s/__VERSION__/$(BSD_VERSION)/" $(BSD_SOURCE_DIR)/mysqmail/Makefile | sed "s/__CATEGORIES__/$(BSD_CATEGORIES)/" >$(PORT_BUILD)/Makefile	# Create Makefile with correct port version and categories
	@cp $(BSD_SOURCE_DIR)/mysqmail/pkg-message $(PORT_BUILD)
	@cp $(BSD_SOURCE_DIR)/mysqmail/pkg-descr $(PORT_BUILD)
	@cp $(BSD_SOURCE_DIR)/mysqmail/pkg-plist $(PORT_BUILD)
	@cp $(BSD_SOURCE_DIR)/mysqmail/mysqmail-postfix-logger $(PORT_BUILD)/files/mysqmail-postfix-logger.in
	@cp $(BSD_SOURCE_DIR)/mysqmail/mysqmail-dovecot-logger $(PORT_BUILD)/files/mysqmail-dovecot-logger.in
	@cp $(BSD_SOURCE_DIR)/mysqmail/mysqmail-courier-logger $(PORT_BUILD)/files/mysqmail-courier-logger.in
	@cp $(BSD_SOURCE_DIR)/mysqmail/mysqmail-pure-ftp-logger $(PORT_BUILD)/files/mysqmail-pure-ftp-logger.in
	@echo "SHA256 ($(BSD_ARCH_NAME)) = "`if [ -e /sbin/sha256 ] ; then sha256 -r $(BSD_DEST_DIR)/$(BSD_ARCH_NAME) | cut -f1 -d" " ; fi` >>$(PORT_BUILD)/distinfo
	@echo "SIZE ($(BSD_ARCH_NAME)) = "`ls -ALln $(BSD_DEST_DIR)/$(BSD_ARCH_NAME) | awk '{print $$5}'` >>$(PORT_BUILD)/distinfo

	@echo "===> Creating archive file"
	@cd $(BSD_BUILD_DIR) && tar -czf mysqmailBSDport-$(BSD_VERSION).tar.gz $(BSD_CATEGORIES) && cd $(CURDIR)
	@mv $(BSD_BUILD_DIR)/mysqmailBSDport-"$(BSD_VERSION)".tar.gz $(BSD_DEST_DIR)
	@echo "--- Successfully made BSD port tree $(BSD_DEST_DIR)/mysqmailBSDport-$(BSD_VERSION).tar.gz ---"
	@echo "===> Deleting temp files"
	@rm -r $(BSD_BUILD_DIR)

BSD_MAKE_PKG_SOURCES=$(BSD_SOURCE_DIR)/mysqmail/Makefile $(BSD_SOURCE_DIR)/mysqmail/pkg-descr  \
$(BSD_SOURCE_DIR)/mysqmail/pkg-message $(BSD_SOURCE_DIR)/mysqmail/pkg-plist \
$(BSD_SOURCE_DIR)/mysqmail/mysqmail-postfix-logger \
$(BSD_SOURCE_DIR)/mysqmail/mysqmail-dovecot-logger \
$(BSD_SOURCE_DIR)/mysqmail/mysqmail-courier-logger \
$(BSD_SOURCE_DIR)/mysqmail/mysqmail-pure-ftp-logger


.PHONY: clean install all strip dist rpm install-conf install-conf-sample install-courier install-qmail install-post install-pure deb install-dovecot bsd-ports-packages
