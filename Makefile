# إعدادات المترجم وبيئة OpenOrbis
TOOLCHAIN := $(OO_PS4_TOOLCHAIN)
CC        := clang
LD        := ld.lld
PKG_TOOL  := $(TOOLCHAIN)/bin/linux/PkgTool.Core

TITLE_ID   := G29GTA501
TITLE      := G29 GTA5 Wheel Enabler
VERSION    := 01.00
CONTENT_ID := IV0000-$(TITLE_ID)_00-G29WHEELENABLER0

CFLAGS    := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables \
             -I$(TOOLCHAIN)/include -I$(TOOLCHAIN)/include/orbis \
             -D__PS4__ -O2 -Wall

LDFLAGS   := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x \
             --eh-frame-hdr -L$(TOOLCHAIN)/lib \
             -lkernel -lc -lc++ -lSceUserService -lScePad -lSceSysmodule

all: G29_GTA5.prx G29_GTA5_Installer.pkg

# 1. بناء ملف الإضافة البرمجية (PRX) الخاصة بـ GoldHEN بالبنية والتوقيع الصحيحين
G29_GTA5.prx: plugin.c
	$(CC) $(CFLAGS) -c plugin.c -o plugin.o
	$(LD) -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr \
		-L$(TOOLCHAIN)/lib $(TOOLCHAIN)/lib/crtlib.o plugin.o \
		-lkernel -lc -lScePad -o G29_GTA5.elf
	$(TOOLCHAIN)/bin/linux/create-fself -in=G29_GTA5.elf --out=G29_GTA5.prx --lib=G29_GTA5.prx --paid 0x3800000000000011
	test -s G29_GTA5.prx

# 2. بناء الملف التنفيذي لتطبيق التثبيت (eboot.bin)
eboot.bin: installer_main.c
	$(CC) $(CFLAGS) -c installer_main.c -o installer.o
	$(LD) $(LDFLAGS) $(TOOLCHAIN)/lib/crt1.o installer.o -o installer.elf
	$(TOOLCHAIN)/bin/linux/create-fself -in=installer.elf --eboot=eboot.bin --paid 0x3800000000000011
	test -s eboot.bin

# 3. تجهيز ملفات النظام الخاصة بالـ PKG (param.sfo)
param.sfo:
	mkdir -p sce_sys
	$(PKG_TOOL) sfo_new sce_sys/param.sfo
	$(PKG_TOOL) sfo_setentry sce_sys/param.sfo APP_TYPE --type Integer --maxsize 4 --value 1
	$(PKG_TOOL) sfo_setentry sce_sys/param.sfo APP_VER --type Utf8 --maxsize 8 --value $(VERSION)
	$(PKG_TOOL) sfo_setentry sce_sys/param.sfo ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(PKG_TOOL) sfo_setentry sce_sys/param.sfo CATEGORY --type Utf8 --maxsize 4 --value "gd"
	$(PKG_TOOL) sfo_setentry sce_sys/param.sfo CONTENT_ID --type Utf8 --maxsize 48 --value "$(CONTENT_ID)"
	$(PKG_TOOL) sfo_setentry sce_sys/param.sfo DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(PKG_TOOL) sfo_setentry sce_sys/param.sfo SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(PKG_TOOL) sfo_setentry sce_sys/param.sfo TITLE --type Utf8 --maxsize 128 --value "$(TITLE)"
	$(PKG_TOOL) sfo_setentry sce_sys/param.sfo TITLE_ID --type Utf8 --maxsize 12 --value "$(TITLE_ID)"
	$(PKG_TOOL) sfo_setentry sce_sys/param.sfo VERSION --type Utf8 --maxsize 8 --value $(VERSION)

# 4. بناء ملف PKG النهائي
G29_GTA5_Installer.pkg: G29_GTA5.prx eboot.bin param.sfo
	printf '<?xml version="1.0" encoding="utf-8" standalone="yes"?>\n' > project.gp4
	printf '<psproject fmt="gp4" version="1000">\n' >> project.gp4
	printf '  <volume>\n' >> project.gp4
	printf '    <volume_type>pkg_ps4_app</volume_type>\n' >> project.gp4
	printf '    <volume_id>PS4VOLUME</volume_id>\n' >> project.gp4
	printf '    <volume_ts>2026-01-01 00:00:00</volume_ts>\n' >> project.gp4
	printf '    <package content_id="$(CONTENT_ID)" passcode="00000000000000000000000000000000" storage_type="digital50" app_type="full"/>\n' >> project.gp4
	printf '    <chunk_info chunk_count="1" scenario_count="1">\n' >> project.gp4
	printf '      <chunks>\n' >> project.gp4
	printf '        <chunk id="0" layer_no="0" label="Chunk #0"/>\n' >> project.gp4
	printf '      </chunks>\n' >> project.gp4
	printf '      <scenarios default_id="0">\n' >> project.gp4
	printf '        <scenario id="0" type="sp" initial_chunk_count="1" label="Scenario #0">0</scenario>\n' >> project.gp4
	printf '      </scenarios>\n' >> project.gp4
	printf '    </chunk_info>\n' >> project.gp4
	printf '  </volume>\n' >> project.gp4
	printf '  <files img_no="0">\n' >> project.gp4
	printf '    <file targ_path="sce_sys/param.sfo" orig_path="sce_sys/param.sfo"/>\n' >> project.gp4
	printf '    <file targ_path="eboot.bin" orig_path="eboot.bin"/>\n' >> project.gp4
	printf '    <file targ_path="G29_GTA5.prx" orig_path="G29_GTA5.prx"/>\n' >> project.gp4
	printf '  </files>\n' >> project.gp4
	printf '  <rootdir>\n' >> project.gp4
	printf '    <dir targ_name="sce_sys"/>\n' >> project.gp4
	printf '  </rootdir>\n' >> project.gp4
	printf '</psproject>\n' >> project.gp4
	$(PKG_TOOL) pkg_build project.gp4 .
	mv $(CONTENT_ID).pkg G29_GTA5_Installer.pkg

clean:
	rm -rf *.o *.elf *.prx eboot.bin sce_sys project.gp4 *.pkg
