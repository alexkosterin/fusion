/*
 *  FUSION
 *  Copyright (c) 2012-2013 Alex Kosterin
 */

#ifndef FUSION_BINTXT_H
#define	FUSION_BINTXT_H

int bin2txt(FILE* fd, size_t len, const char* data);
int bin2txt(size_t dlen, const char* data, size_t tlen, char* txt);
int txtlen(size_t blen, const char* bin);
int txt2bin(size_t tlen, const char* txt, size_t dlen, char* bin);

#endif  //FUSION_BINTXT_H

