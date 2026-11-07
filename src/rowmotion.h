/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_ROWMOTION_H
#define ZYPEROS_ROWMOTION_H
#include <stdbool.h>
#include <stdint.h>
#define ROW_WINDOW 7
#define ROW_SLOTS 16
typedef struct { int item; float offset,alpha; } row_slot;
typedef struct { int item; float from,to,alpha,to_alpha; bool wrap; } row_track;
typedef struct { row_track tracks[ROW_SLOTS]; int n,count,cursor; uint32_t moved; bool initialized; } row_motion;
void row_reset(row_motion *r,int cursor,int count,uint32_t now);
void row_select(row_motion *r,int cursor,int count,uint32_t now);
int row_sample(const row_motion *r,uint32_t now,row_slot out[ROW_SLOTS]);
bool row_moving(const row_motion *r,uint32_t now);
#endif
