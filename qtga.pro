TARGET  = qtga
TEMPLATE = lib
CONFIG += qt plug-in
VERSION = 1.0.0

HEADERS = tga_p.h
#    include/xTGA/api.h \
#    include/xTGA/error.h \
#    include/xTGA/flags.h \
#    include/xTGA/marray.h \
#    include/xTGA/pixelformats.h \
#    include/xTGA/structures.h \
#    include/xTGA/tga_file.h \
#    include/xTGA/types.h \
#    include/xTGA/xTGA.h \
#    include/xTGA/xTGA_C.h \
#    src/codecs.h \
#    src/error_macro.h
SOURCES = tga.cpp
#    src/codecs.cpp \
#    src/marray.cpp \
#    src/pixelformats.cpp \
#    src/tga_file.cpp \
#    src/xTGA_C.cpp
OTHER_FILES += tga.json
target.path += plugin/imageformats
INSTALLS += target

#PLUGIN_TYPE = imageformats
#PLUGIN_CLASS_NAME = TGAPlugin

DISTFILES += \
    include/xTGA/.gitignore
