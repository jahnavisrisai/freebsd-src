/* $FreeBSD: src/usr.sbin/sysinstall/dist.h,v 1.59 2003/10/24 20:55:15 jhb Exp $  */

#ifndef _DIST_H_INCLUDE
#define _DIST_H_INCLUDE

/* Bitfields for distributions - hope we never have more than 32! :-) */
#define DIST_BASE		0x00001
#define DIST_GAMES		0x00002
#define DIST_MANPAGES		0x00004
#define DIST_PROFLIBS		0x00008
#define DIST_DICT		0x00010
#define DIST_SRC		0x00020
#define DIST_DOC		0x00040
#define DIST_INFO		0x00080
#ifdef __i386__			/* only applicable on x86 */
#define DIST_COMPAT1X		0x00100
#define DIST_COMPAT20		0x00200
#define DIST_COMPAT21		0x00400
#define DIST_COMPAT22		0x00800
#define DIST_COMPAT3X		0x01000
#endif
#if defined(__i386__) || defined(__alpha__)
#define DIST_COMPAT4X		0x02000
#endif
#define DIST_XF86		0x04000
#define DIST_CRYPTO		0x08000
#define DIST_CATPAGES		0x10000
#define DIST_PORTS		0x20000
#define DIST_LOCAL		0x40000
#define DIST_PERL		0x80000
#define DIST_ALL		0xFFFFF

/* Subtypes for CRYPTO distribution */
#define DIST_CRYPTO_CRYPTO	0x0001
#define DIST_CRYPTO_SCRYPTO	0x0002
#define DIST_CRYPTO_SSECURE	0x0004
#define DIST_CRYPTO_SKERBEROS5	0x0008
#define DIST_CRYPTO_ALL		0x000F

/* Subtypes for SRC distribution */
#define DIST_SRC_BASE		0x00001
#define DIST_SRC_CONTRIB	0x00002
#define DIST_SRC_GNU		0x00004
#define DIST_SRC_ETC		0x00008
#define DIST_SRC_GAMES		0x00010
#define DIST_SRC_INCLUDE	0x00020
#define DIST_SRC_LIB		0x00040
#define DIST_SRC_LIBEXEC	0x00080
#define DIST_SRC_TOOLS		0x00100
#define DIST_SRC_RELEASE	0x00200
#define DIST_SRC_SBIN		0x00400
#define DIST_SRC_SHARE		0x00800
#define DIST_SRC_SYS		0x01000
#define DIST_SRC_UBIN		0x02000
#define DIST_SRC_USBIN		0x04000
#define DIST_SRC_BIN		0x08000
#define DIST_SRC_ALL		0x0FFFF

/* Subtypes for XFree86 packages */
#define	DIST_XF86_CLIENTS	0x0001
#define DIST_XF86_DOC		0x0002
#define	DIST_XF86_LIB		0x0004
#define DIST_XF86_MAN		0x0008
#define DIST_XF86_PROG		0x0010
#define	DIST_XF86_MISC_ALL	0x001F
#define DIST_XF86_SERVER	0x0020
#define	DIST_XF86_SERVER_FB	0x0001
#define	DIST_XF86_SERVER_NEST	0x0002
#define	DIST_XF86_SERVER_PRINT	0x0004
#define	DIST_XF86_SERVER_VFB	0x0008
#define	DIST_XF86_SERVER_ALL	0x000F
#define DIST_XF86_FONTS		0x0040
#define DIST_XF86_FONTS_75		0x0001
#define DIST_XF86_FONTS_100		0x0002
#define DIST_XF86_FONTS_CYR		0x0004
#define DIST_XF86_FONTS_SCALE		0x0008
#define	DIST_XF86_FONTS_BITMAPS		0x0010
#define DIST_XF86_FONTS_SERVER		0x0020
#define DIST_XF86_FONTS_ALL		0x003F
#define DIST_XF86_ALL		0x007F

/* Canned distribution sets */
#define _DIST_USER \
	( DIST_BASE | DIST_DOC | DIST_MANPAGES | DIST_DICT | DIST_CRYPTO | DIST_PERL )

#define _DIST_DEVELOPER \
	( _DIST_USER | DIST_PROFLIBS | DIST_INFO | DIST_SRC )

#endif	/* _DIST_H_INCLUDE */
