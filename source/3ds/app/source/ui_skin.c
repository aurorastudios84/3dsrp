#include "ui_skin.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t offset; uint8_t width,height; int8_t x,y; uint8_t advance; } UiGlyph;
typedef struct { unsigned size; const unsigned char *pixels; const UiGlyph *glyphs; } UiFont;
#include "ui_fonts.h"
typedef struct { uint16_t *pixels; unsigned width,height; bool column_major; } Canvas;
static struct {
    bool active,items[30],selected[30];
    unsigned row,column;
    UiSkinKind kind;
    char rows[30][41],primary[32];
} screen;
static uint16_t rgb(unsigned r,unsigned g,unsigned b) { return (uint16_t)((r>>3)<<11|(g>>2)<<5|(b>>3)); }
static uint16_t *pixel(Canvas *c,int x,int y)
{
    if(x<0 || y<0 || (unsigned)x>=c->width || (unsigned)y>=c->height) return NULL;
    return c->pixels+(c->column_major ? (unsigned)x*c->height+c->height-1-(unsigned)y : (unsigned)y*c->width+(unsigned)x);
}
static void blend(Canvas *c,int x,int y,uint16_t color,unsigned alpha)
{
    uint16_t *p=pixel(c,x,y); if(!p || !alpha) return;
    if(alpha==255) { *p=color; return; }
    unsigned r=((*p>>11)*(255-alpha)+(color>>11)*alpha+127)/255;
    unsigned g=(((*p>>5)&63)*(255-alpha)+((color>>5)&63)*alpha+127)/255;
    unsigned b=((*p&31)*(255-alpha)+(color&31)*alpha+127)/255;
    *p=(uint16_t)(r<<11|g<<5|b);
}
static void rect(Canvas *c,int x,int y,int w,int h,uint16_t color)
{
    for(int iy=0;iy<h;iy++) for(int ix=0;ix<w;ix++) blend(c,x+ix,y+iy,color,255);
}
static void pill(Canvas *c,int x,int y,int w,int h,uint16_t color)
{
    rect(c,x+3,y,w-6,h,color);rect(c,x,y+3,w,h-6,color);
    rect(c,x+1,y+1,w-2,h-2,color);
}
static const UiFont *font(unsigned size)
{
    for(unsigned i=0;i<sizeof(fonts)/sizeof(fonts[0]);i++) if(fonts[i].size==size) return &fonts[i];
    return &fonts[1];
}
static unsigned width(const char *text,unsigned size)
{
    unsigned result=0;const UiFont *f=font(size);
    for(const unsigned char *p=(const unsigned char *)text;*p;p++) result+=f->glyphs[*p>=32 && *p<127 ? *p-32 : '?'-32].advance;
    return result;
}
static void text(Canvas *c,int x,int y,const char *s,unsigned size,uint16_t color,int limit)
{
    const UiFont *f=font(size);int cursor=x;
    for(const unsigned char *p=(const unsigned char *)s;*p;p++) {
        const UiGlyph *g=&f->glyphs[*p>=32 && *p<127 ? *p-32 : '?'-32];
        if(cursor+g->advance>limit) break;
        for(unsigned gy=0;gy<g->height;gy++) for(unsigned gx=0;gx<g->width;gx++)
            blend(c,cursor+g->x+(int)gx,y+g->y+(int)gy,color,f->pixels[g->offset+gy*g->width+gx]);
        cursor+=g->advance;
    }
}
static void background(Canvas *c)
{
    for(unsigned y=0;y<c->height;y++) for(unsigned x=0;x<c->width;x++) {
        unsigned glow=(c->width-x)*8/c->width;
        *pixel(c,x,y)=rgb(7+glow/3,12+glow/2,24+glow+y/28);
    }
}
void ui_skin_begin(UiSkinKind kind,const char *primary)
{
    memset(&screen,0,sizeof(screen));screen.active=true;screen.kind=kind;
    for(unsigned r=0;r<30;r++) memset(screen.rows[r],' ',40);
    if(primary) snprintf(screen.primary,sizeof(screen.primary),"%s",primary);
}
void ui_skin_at(unsigned row,unsigned column)
{
    if(screen.active) { screen.row=row;screen.column=column; }
}
void ui_skin_write(const char *s)
{
    if(!screen.active || !s || (unsigned char)*s==27) return;
    for(;*s;s++) {
        if(*s=='\n') { screen.row++;screen.column=0;continue; }
        if(*s=='\r') { screen.column=0;continue; }
        if(screen.column>=40) { screen.column=0;screen.row++; }
        if(screen.row<30) screen.rows[screen.row][screen.column]=*s;
        screen.column++;
    }
}
void ui_skin_item(unsigned row,bool selected)
{
    if(screen.active && row<30) { screen.items[row]=true;screen.selected[row]=selected; }
}
static void trim(char *line)
{
    size_t n=strlen(line);while(n && line[n-1]==' ') line[--n]=0;
}
static void ready(Canvas *c)
{
    uint16_t white=rgb(239,245,255),muted=rgb(142,160,188),blue=rgb(61,139,255);
    rect(c,0,0,320,76,rgb(15,29,53));rect(c,14,12,3,48,blue);
    text(c,25,8,"PRIMARY CONSOLE",14,white,307);
    const char *name=screen.primary[0] ? screen.primary : "No console selected";
    const unsigned sizes[]={22,18,14,11,10,8};unsigned size=8;
    for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++)
        if(width(name,sizes[i])<=282) { size=sizes[i];break; }
    text(c,24,29,name,size,white,307);
    text(c,25,60,"Choose your primary in Consoles",8,muted,307);
    for(unsigned i=0;i<5;i++) {
        unsigned row=3+2*i,y=82+20*i;
        const char *label=screen.rows[row]+2;
        if(screen.selected[row]) {
            pill(c,12,y,296,19,rgb(25,73,143));rect(c,12,y+3,3,13,blue);
        }
        text(c,25,y+1,label,11,white,301);
    }
    for(unsigned r=16;r<=19;r++) text(c,16,184+(int)(r-16)*9,screen.rows[r],8,muted,305);
    text(c,16,220,screen.rows[20],8,rgb(255,190,132),305);
    text(c,16,231,"A  Open     Y  Settings     START  Exit",8,muted,305);
}
void ui_skin_end(uint16_t *bottom)
{
    if(!screen.active) return;
    screen.active=false;
    Canvas c={bottom,320,240,true};background(&c);
    for(unsigned r=0;r<30;r++) trim(screen.rows[r]);
    if(screen.kind==UI_SKIN_READY) { ready(&c);return; }
    uint16_t white=rgb(235,242,255),muted=rgb(151,167,191),blue=rgb(69,145,255);
    rect(&c,0,0,4,19,blue);
    text(&c,14,0,screen.rows[0],14,white,309);
    for(unsigned r=1;r<30;r++) {
        if(!screen.rows[r][0]) continue;
        const char *line=screen.rows[r];int y=(int)r*8-1;
        unsigned size=screen.kind==UI_SKIN_PRIVACY && r>=3 && r<=17 ? 10 : 8;
        if(screen.kind==UI_SKIN_HELP) {
            if(r==1) { text(&c,263,4,line,8,muted,316);continue; }
            if(r>=3 && r<=23) { y=27+((int)r-3)*17/2;size=10; }
            else if(r>=27) y=219+((int)r-27)*10;
        }
        if(screen.items[r]) {
            bool dense=(screen.kind==UI_SKIN_DENSE && r>=3 && r<=15) ||
                (screen.kind==UI_SKIN_CONSOLES && r>=3 && r<=10);
            size=dense ? 8 : 11;
            if(screen.selected[r]) {
                if(dense) rect(&c,11,y+1,298,8,rgb(25,73,143));
                else pill(&c,11,y,298,15,rgb(25,73,143));
                rect(&c,11,y+2,2,dense ? 6 : 11,blue);
            }
            line+=2;
        }
        uint16_t color=r>=25 ? muted : white;
        if(r==29) color=rgb(255,190,132);
        text(&c,17,y,line,size,color,308);
    }
}
const uint16_t *ui_skin_brand(void)
{
    static uint16_t image[400u*240u];static bool initialized;
    if(initialized) return image;
    initialized=true;Canvas c={image,400,240,false};background(&c);
    /* Nested arched doorways form a portal into the console. */
    const int tops[]={29,34,48,52,67,70};
    const int halves[]={53,48,35,31,18,15};
    const int bottoms[]={169,164,157,153,145,142};
    const uint16_t colors[]={rgb(52,121,240),rgb(9,15,32),
        rgb(65,142,255),rgb(12,24,48),rgb(216,237,255),rgb(9,15,32)};
    for(unsigned frame=0;frame<6;frame++) {
        int h=halves[frame],top=tops[frame],mid=top+h;
        for(int y=top;y<=bottoms[frame];y++) for(int dx=-h;dx<=h;dx++)
            if(y>=mid || dx*dx+(y-mid)*(y-mid)<=h*h)
                blend(&c,326+dx,y,colors[frame],255);
    }
    text(&c,32,77,"3DSRP",32,rgb(241,247,255),350);
    text(&c,35,126,"PS4 / PS5 REMOTE PLAY",10,rgb(161,196,247),360);
    rect(&c,35,160,43,3,rgb(65,142,255));
    text(&c,35,181,"Your portal to PlayStation.",11,rgb(175,192,215),360);
    text(&c,35,200,"Powered by Chiaki-ng",10,rgb(175,192,215),360);
    text(&c,35,220,"Chiaki / chiaki-ng contributors",8,rgb(118,143,176),360);
    text(&c,35,231,"3DS port: AuroraStudios84",8,rgb(118,143,176),360);
    return image;
}
