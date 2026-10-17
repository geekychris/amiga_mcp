// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* title_image.h - main screen bitmap (generated, see tools/make_title.py) */
#ifndef TITLE_IMAGE_H
#define TITLE_IMAGE_H

#define TITLE_W 320
#define TITLE_H 256
#define TITLE_DEPTH 5
#define TITLE_PEN_TEXT 30      /* nearest to yellow */
#define TITLE_PEN_SHADOW 0    /* nearest to black */

extern const unsigned short title_palette[32];
extern const unsigned char title_planes[5][10240];

#endif
