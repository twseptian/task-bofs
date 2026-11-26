/*
 * beacon.h - beacon API subset provided by the Sliver COFF loader (reflektor).
 * Loader strips the __imp_ prefix and resolves Win32 imports against loaded
 * modules. Arg buffer: uint32 LE total length, then per manifest argument
 * string = u32(len+1)+data+NUL, int = u32, short = u16. BeaconDataParse
 * skips the envelope itself; strings extract NUL-terminated; optional args
 * are always packed so parsing is sequential in manifest order.
 */
#ifndef BEACON_H
#define BEACON_H

#define CALLBACK_OUTPUT 0x0
#define CALLBACK_ERROR  0x1

void BeaconOutput(int type, char * data, int len);
void BeaconPrintf(int type, char * fmt, ...);

typedef struct {
    char * original;
    char * buffer;
    int    length;
    int    size;
    int    current; /* untouched by the loader */
} datap;

void   BeaconDataParse(datap * parser, char * buffer, int size);
int    BeaconDataInt(datap * parser);
short  BeaconDataShort(datap * parser);
int    BeaconDataLength(datap * parser);
char * BeaconDataExtract(datap * parser, int * size);

#endif /* BEACON_H */
