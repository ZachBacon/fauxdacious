SHARED_LIB = ${LIB_PREFIX}fauxdqt${LIB_SUFFIX}
LIB_MAJOR = 2
LIB_MINOR = 2

SRCS = about-qt.cc \
       art-qt.cc \
       audqt.cc \
       colorbutton.cc \
       eq-preset-qt.cc \
       equalizer-qt.cc \
       file-entry.cc \
       fileopener.cc \
       font-entry.cc \
       images.cc \
       infopopup-qt.cc \
       infowin-qt.cc \
       info-widget.cc \
       log-inspector.cc \
       menu-qt.cc \
       playlist-management.cc \
       plugin-menu-qt.cc \
       prefs-builder.cc \
       prefs-plugin.cc \
       prefs-widget-qt.cc \
       prefs-window-qt.cc \
       prefs-pluginlist-model.cc \
       queue-manager-qt.cc \
       url-opener-qt.cc \
       util-qt.cc \
       treeview.cc \
       volumebutton.cc

INCLUDES = colorbutton.h \
           export.h \
           iface.h \
           info-widget.h \
           libfauxdqt.h \
           menu.h \
           treeview.h

include ../../buildsys.mk
include ../../extra.mk

includesubdir = libfauxdqt

LD = ${CXX}

CPPFLAGS := -I.. -I../.. \
            ${CPPFLAGS} \
            ${QT_CFLAGS} \
            -DLIBAUDQT_BUILD

CFLAGS += ${LIB_CFLAGS}

LIBS := -L../libfauxdcore -lfauxdcore \
        ${LIBS} -lm \
        ${QT_LIBS}

images.cc: images.qrc
	${QT_BINPATH}/rcc images.qrc -o images.cc
