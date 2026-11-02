#ifndef BUILD_ID_H
#define BUILD_ID_H
/* GHDAX (joystick 10 ms controller timeout + automatic recovery, HUD exact +400 on ENEMY / BULLETS): BUILD identity (ASSET BUILD reply, HUD2 tag). Default = single ELF (asset upload + game); VARIANT=full = the former
 * UART diagnostic build, size reference only (does not meet the 3072 B reserve rule). */
#if defined(BUILD_VARIANT_GAME) && defined(ASSET_UPLOAD_SERIAL)
#define BUILD_ID "GHDB1_20261101"
#define BUILD_HUD_TAG "GHDB1"
#else
#define BUILD_ID "GHDBD_20261101"
#define BUILD_HUD_TAG "GHDBD"
#endif
#endif
