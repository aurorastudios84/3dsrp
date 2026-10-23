#ifndef TOUCH_TRAIL_H
#define TOUCH_TRAIL_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* UI-only history; no allocation, protocol changes or dependence on video FPS. */
#define TOUCH_TRAIL_CAPACITY 128u
#define TOUCH_TRAIL_LIFETIME 1000u
typedef struct { uint32_t time; uint16_t x,y; bool join; } TouchTrailPoint;
typedef struct {
    TouchTrailPoint points[TOUCH_TRAIL_CAPACITY];
    unsigned first,count;
    bool down;
} TouchTrail;

static bool touch_trail_update(TouchTrail *trail,bool visible,bool down,
                               unsigned x,unsigned y,uint32_t now)
{
    bool dirty=trail->count!=0;
    if(!visible) { memset(trail,0,sizeof(*trail)); return dirty; }
    while(trail->count && (uint32_t)(now-trail->points[trail->first].time)>=TOUCH_TRAIL_LIFETIME) {
        trail->first=(trail->first+1)%TOUCH_TRAIL_CAPACITY; trail->count--;
    }
    if(down) {
        if(trail->count==TOUCH_TRAIL_CAPACITY) {
            trail->first=(trail->first+1)%TOUCH_TRAIL_CAPACITY; trail->count--;
        }
        unsigned index=(trail->first+trail->count++)%TOUCH_TRAIL_CAPACITY;
        trail->points[index]=(TouchTrailPoint){now,x<320 ? x : 319,y<200 ? y : 199,trail->down};
    }
    trail->down=down;
    return dirty || down;
}

static void touch_trail_dot(uint16_t *fb,int x,int y,unsigned alpha)
{
    /* 3DSRP menu accent RGB(69,145,255), on the black touch surface. */
    uint16_t color=(uint16_t)(((8u*alpha/255u)<<11)|((36u*alpha/255u)<<5)|(31u*alpha/255u));
    for(int dy=-2;dy<=2;dy++) for(int dx=-2;dx<=2;dx++) {
        int px=x+dx,py=y+dy;
        if(dx*dx+dy*dy<=4 && px>0 && px<319 && py>0 && py<199)
            fb[px*240+239-py]=color;
    }
}
static void touch_trail_draw(const TouchTrail *trail,uint16_t *fb,uint32_t now)
{
    for(unsigned i=0;i<trail->count;i++) {
        const TouchTrailPoint *p=&trail->points[(trail->first+i)%TOUCH_TRAIL_CAPACITY];
        uint32_t age=now-p->time;
        if(age>=TOUCH_TRAIL_LIFETIME) continue;
        unsigned alpha=255u*(TOUCH_TRAIL_LIFETIME-age)/TOUCH_TRAIL_LIFETIME;
        if(i && p->join) {
            const TouchTrailPoint *q=&trail->points[(trail->first+i-1)%TOUCH_TRAIL_CAPACITY];
            int dx=(int)p->x-q->x,dy=(int)p->y-q->y;
            int steps=abs(dx)>abs(dy) ? abs(dx) : abs(dy);
            for(int step=0;step<steps;step++)
                touch_trail_dot(fb,q->x+dx*step/steps,q->y+dy*step/steps,alpha);
        }
        touch_trail_dot(fb,p->x,p->y,alpha);
    }
}
#endif
