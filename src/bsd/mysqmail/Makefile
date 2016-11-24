# Ports collection makefile for:	mysqmail
# Date created:				2 April 2010
# Whom:					Fernando Kiernan
#
# $FreeBSD$
#

PORTNAME=	mysqmail
PORTVERSION=	__VERSION__
CATEGORIES=	__CATEGORIES__
MASTER_SITES=	ftp://ftp.gplhost.sg/pub/mysqmail/bsd/ \
		ftp://qala-sg.apt-proxy.gplhost.sg/pub/mysqmail/bsd/ \
		ftp://601.apt-proxy.gplhost.com/pub/mysqmail/bsd/ \
		ftp://seattle.apt-proxy.gplhost.com/pub/mysqmail/bsd/ \
		ftp://ftp.gplhost.com/pub/mysqmail/bsd/ \
		ftp://ftp.gplhost.co.uk/pub/mysqmail/bsd/ \
		ftp://ftp.gplhost.fr/pub/mysqmail/bsd/ \
		ftp://972.apt-proxy.gplhost.com/pub/mysqmail/bsd/ \
		http://ftp.gplhost.fr/pub/mysqmail/bsd/

MAINTAINER=	fkiernan@id.net.ar
COMMENT=	A mail traffic logger from syslog to Mysql

BUILD_DEPENDS=	${LOCALBASE}/include/dotconf.h:${PORTSDIR}/devel/dotconf

MAN8=		mysqmail-pure-ftpd-logger.8 mysqmail-postfix-logger.8 mysqmail-courier-logger.8 mysqmail-dovecot-logger.8
MANCOMPRESSED=	yes

USE_GMAKE=	yes

USE_MYSQL=	server

USE_RC_SUBR=	mysqmail-postfix-logger mysqmail-dovecot-logger mysqmail-courier-logger mysqmail-pure-ftp-logger

USE_SUBMAKE=	yes

post-patch:
	@${REINPLACE_CMD} -e 's|/etc/mysqmail.conf|${PREFIX}/etc/mysqmail.conf|' ${WRKSRC}/myconfig.c

do-install:
	${MAKE} -C ${WRKDIR}/mysqmail-__VERSION__ SBIN_DIR=${PREFIX}/sbin MAN_DIR=${PREFIX}/man INSTALL=${INSTALL} install-pure
	${MAKE} -C ${WRKDIR}/mysqmail-__VERSION__ SBIN_DIR=${PREFIX}/sbin MAN_DIR=${PREFIX}/man INSTALL=${INSTALL} install-post
	${MAKE} -C ${WRKDIR}/mysqmail-__VERSION__ SBIN_DIR=${PREFIX}/sbin MAN_DIR=${PREFIX}/man INSTALL=${INSTALL} install-courier
	${MAKE} -C ${WRKDIR}/mysqmail-__VERSION__ SBIN_DIR=${PREFIX}/sbin MAN_DIR=${PREFIX}/man INSTALL=${INSTALL} install-dovecot
	${MAKE} -C ${WRKDIR}/mysqmail-__VERSION__ install-conf-sample ETCDIR=${PREFIX}/etc
	@if [ ! -f ${PREFIX}/etc/mysqmail.conf ]; then \
		${CP} -p ${PREFIX}/etc/mysqmail.conf.sample ${PREFIX}/etc/mysqmail.conf ; \
	fi

.include <bsd.port.mk>
