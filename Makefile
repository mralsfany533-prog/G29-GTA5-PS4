# إعدادات المترجم وبيئة OpenOrbis
TOOLCHAIN := $(OO_PS4_TOOLCHAIN)
CC        := clang
LD        := ld.lld
PKG_TOOL  := $(TOOLCHAIN)/bin/linux/PkgTool.Core

# معرف التطبيق الخاص بالمثبّت (Title ID)
TITLE_ID  := G29GTA501
TITLE     := G29 GTA5 Wheel Enabler
VERSION   := 01.00

# إعدادات الترجمة المشتركة
CFLAGS    := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables \
             -I$(TOOLCHAIN)/include -I$(TOOLCHAIN)/include/orbis \
             -D__PS4__ -O2 -Wall

LDFLAGS   := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x \
             --eh-frame-hdr -L$(TOOLCHAIN)/lib \
             -lkernel -lc -lc++ -lSceUserService -lScePad -lSceSysmodule

all: G29_GTA5_Installer.pkg

# 1. بناء ملف الإضافة البرمجية (PRX) الخاصة بـ GoldHEN بدون تكرار crtlib.o
G29_GTA5.prx: plugin.c
	$(CC) $(CFLAGS) -c plugin.c -o plugin.o
	$(LD) -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr \
		-L$(TOOLCHAIN)/lib plugin.o \
		-lkernel -lc -lScePad -o G29_GTA5.elf
	$(TOOLCHAIN)/bin/linux/create-fself -in=G29_GTA5.elf --out=G29_GTA5.prx --lib=G29_GTA5.prx

# 2. بناء الملف التنفيذي لتطبيق التثبيت (eboot.bin)
eboot.bin: installer_main.c
	$(CC) $(CFLAGS) -c installer_main.c -o installer.o
	$(LD) $(LDFLAGS) $(TOOLCHAIN)/lib/crt1.o installer.o -o installer.elf
	$(TOOLCHAIN)/bin/linux/create-fself -in=installer.elf --out=eboot.bin --paid 0x3800000000000011

# 3. تجهيز ملفات النظام الخاصة بالـ PKG (param.sfo و gp4)
param.sfo:
	$(PKG_TOOL) sfo_new param.sfo
	$(PKG_TOOL) sfo_setentry param.sfo APP_TYPE --type Integer --maxsize 4 --value 1
	$(PKG_TOOL) sfo_setentry param.sfo APP_VER --type Utf8 --maxsize 8 --value $(VERSION)
	$(PKG_TOOL) sfo_setentry param.sfo ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(PKG_TOOL) sfo_setentry param.sfo CATEGORY --type Utf8 --maxsize 4 --value "gd"
	$(PKG_TOOL) sfo_setentry param.sfo CONTENT_ID --type Utf8 --maxsize 48 --value "IV0000-$(TITLE_ID)_00-G29WHEELENABLER0"
	$(PKG_TOOL) sfo_setentry param.sfo DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(PKG_TOOL) sfo_setentry param.sfo SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(PKG_TOOL) sfo_setentry param.sfo TITLE --type Utf8 --maxsize 128 --value "$(TITLE)"
	$(PKG_TOOL) sfo_setentry param.sfo TITLE_ID --type Utf8 --maxsize 12 --value "$(TITLE_ID)"
	$(PKG_TOOL) sfo_setentry param.sfo VERSION --type Utf8 --maxsize 8 --value $(VERSION)

# 4. تغليف كل شيء داخل ملف PKG النهائي
G29_GTA5_Installer.pkg: G29_GTA5.prx eboot.bin param.sfo
	mkdir -p pkg_build/sce_sys
	cp eboot.bin pkg_build/
	cp G29_GTA5.prx pkg_build/
	cp param.sfo pkg_build/sce_sys/
	$(PKG_TOOL) gp4_proj_create --volume_type pkg_ps4_app --content_id "IV0000-$(TITLE_ID)_00-G29WHEELENABLER0" --passcode 00000000000000000000000000000000 pkg_build/project.gp4
	$(PKG_TOOL) gp4_file_add pkg_build/project.gp4 eboot.bin eboot.bin
	$(PKG_TOOL) gp4_file_add pkg_build/project.gp4 G29_GTA5.prx G29_GTA5.prx
	$(PKG_TOOL) gp4_file_add pkg_build/project.gp4 sce_sys/param.sfo sce_sys/param.sfo
	$(PKG_TOOL) pkg_build pkg_build/project.gp4 .
	mv IV0000-$(TITLE_ID)_00-G29WHEELENABLER0.pkg G29_GTA5_Installer.pkg

clean:
	rm -rf *.o *.elf *.prx eboot.bin param.sfo pkg_build *.pkg
