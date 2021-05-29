// 開發板選用 ESP32 Wrover Module

#include "wifiboy_lib.h"
#include <Preferences.h>
#define KEY_R    32
#define KEY_L    33
#define KEY_B    34
#define KEY_A    35
#define KEY_U    36
#define KEY_D    39
#define KEY_MENU 0
#define BUZZER   17
#define SPI_MISO 12
#define SPI_MOSI 13
#define SPI_CLK  14
#define SPI_CS   15
#define TFT_DC   4
#define TFT_BKLT 27
#define IO_5     5
#define IO_LED   16
#define LCD_H    128
#define LCD_W    160
#define BLOCK_H  7

uint8_t blk_t[28][4] = { // seven shapes, four rotation types
  {0,10,20,30},{0,1,2,3},{0,10,20,30},{0,1,2,3},
  {1,10,11,12},{0,10,11,20},{0,1,2,11},{1,10,11,21},
  {0,1,10,11},{0,1,10,11},{0,1,10,11},{0,1,10,11},
  {0,10,20, 21},{0,1,2,10},{0,1,11,21},{2,10,11,12},
  {1,11,20,21},{0,10,11,12},{0,1,10,20},{0,1,2,12},
  {0,1,11,12},{1,10,11,20},{0,1,11,12},{1,10,11,20},
  {1,2,10,11},{0,10,11,21},{1,2,10,11},{0,10,11,21}
};

uint16_t bcolor[8]={
  wbBLACK, wbBLUE, wbRED, wbGREEN, wbCYAN, wbMAGENTA, wbYELLOW, wbWHITE
};

// we use a lot of dirty globals for efficiency
Preferences preferences;
uint8_t board[20][10], offboard[20][10]; // keep an off board for dirty-rect-rendering
uint16_t cx, cy, kc, task_c, sfx_on, sfxn, sfxc, freq, fall_time, fall_limit, stage_limit,highscore; 
uint16_t ctype, nctype, level, pts, pn, i, j, k, pos, px, py, key, last_key, cline;
char tmpbuf[10];
int rot, ret;

void init_keys(){
  pinMode(KEY_L,INPUT); 
  pinMode(KEY_R,INPUT); 
  pinMode(KEY_U,INPUT); 
  pinMode(KEY_D,INPUT);
  pinMode(KEY_B,INPUT); 
  pinMode(KEY_A,INPUT);
  pinMode(KEY_MENU,INPUT); 
  pinMode(IO_5,INPUT);
  kc = 0;
}

uint16_t getkey(){
  return(key = digitalRead(KEY_L)*128+
               digitalRead(KEY_R)*64+
               digitalRead(KEY_U)*32+
               digitalRead(KEY_D)*16+
               digitalRead(KEY_B)*8+
               digitalRead(KEY_A)*4+
               digitalRead(KEY_MENU)*2+
               digitalRead(IO_5)*1);
}

void draw_board(){  
  for(i=0; i<20; i++){
    for(j=0; j<10; j++){
      if (offboard[i][j]!=board[i][j]){ // dirty rectangle check        
        wb_fillRect(j*(BLOCK_H+1)+1, i*(BLOCK_H+1), 
          BLOCK_H, BLOCK_H, bcolor[offboard[i][j]=board[i][j]]);
      }
    }
  }
  wb_drawFastHLine(0, 0, LCD_W/2, wbRED);  
}

void clear_board(){
  for(i=0; i<20; i++)     // board is 10x20
    for(j=0; j<10; j++) {
      offboard[i][j]=255; // initial state
      board[i][j]=0;      // clear all blocks
    }
  wb_fillRect(0, 0, LCD_W, LCD_H, wbBLACK);
}

void get_pos(uint16_t pos){
  py=pos/10; px = pos%10;
}

void draw_blocks(uint16_t x, uint16_t y, uint16_t v){
  for(int i=0; i<4; i++) {
    get_pos(y*10 + x + blk_t[ctype*4+rot][i]); 
    board[py][px]=v; 
  }
}

void draw_preview(){ // next blocks
  wb_fillRect(110, 12, 60, 36, wbBLACK);
  for(i=0; i<4; i++) {
    get_pos(blk_t[nctype*4][i]);
    wb_fillRect(110+px*(BLOCK_H+1)+1, 
                   12+py*(BLOCK_H+1), 
                             BLOCK_H, 
                             BLOCK_H, 
                     bcolor[nctype+1]);
  }
}

void update_cline_level(){
  if (cline > 150) level = 10;
  else if (cline > 120) level=9;
  else if (cline > 100) level=8;
  else if (cline > 80) level=7;
  else if (cline > 60) level=6;
  else if (cline > 40) level=5;
  else if (cline > 25) level=4;
  else if (cline > 15) level=3;
  else if (cline > 10) level=2;
  else if (cline > 5) level=1;
}

void update_score(){
  int x=110, y1=58, y2=78, y3=98, y4=118;
  int w=50, h=10;
  
  wb_setTextColor(wbWHITE, wbWHITE);

  //SCORE
  wb_fillRect(x, y2, w, h, wbBLACK);
  wb_drawString(itoa(pts, tmpbuf, 10), x, y2, 2, 1);
  if (pts>highscore) {
    highscore=pts;
    preferences.begin("tetris",false);
    preferences.putUInt("highscore", highscore);
    preferences.end();    
    wb_fillRect(x, y1, w, h, wbBLACK);    
    wb_drawString(itoa(highscore, tmpbuf, 10), x, y1, 2, 1);
  }
  
  //LINE
  wb_fillRect(x, y3, w, h, wbBLACK);  
  wb_drawString(itoa(cline, tmpbuf, 10), x, y3, 2, 1);  

  //LEVEL
  update_cline_level();  
  fall_limit = stage_limit = (level<=12?13-level:1);  
  wb_fillRect(x, y4, w, h, wbBLACK);  
  wb_drawString(itoa(level+1, tmpbuf, 10), x, y4, 2, 1);  

}

int check_line(){
  int pt;
  for(i=19; i>0; i--) {
    for(j=0, pt=0; j<10; j++) if (board[i][j]!=0) pt++; else break;
    if (pt==10) {
      for(j=i; j>0; j--)
        for(k=0; k<10; k++) board[j][k]=board[j-1][k];
      return 1;
    }
  }
  return 0;
}

void go_next_blocks(){
  pn=0;
  while(check_line()) { // check all stacked line
    cline++;
    pts+=20*(2^pn);
    pn++;
  }
  sfxn=pn+1; sfxc=0;
  ctype=nctype;
  nctype=random(7);
  pts+=2;
  cx = 4; cy = 0;
  rot=random(4);
  draw_blocks(cx, cy, ctype+1);
  draw_preview();
  update_score();
}

void init_blocks(){
  clear_board();
  cline=0; level=0; pts=0;
  fall_limit = stage_limit = (level<=12?13-level:1);
  fall_time=0;
  cx=4; cy=0; 
  rot=random(4); ctype=random(7); nctype=random(7);
  draw_blocks(cx, cy, ctype+1);
  //draw_preview();
  update_score();
}

void blocks_fall(){
  if (++fall_time > fall_limit) {
    for(i=0; i<4; i++) {
      get_pos(pos=(cy+1)*10 + cx + blk_t[ctype*4+rot][i]);
      if (pos < 160) board[py-1][px]=0;
      else { // at bottom
        draw_blocks(cx, cy, ctype+1);
        go_next_blocks();
        return;
      }
    }
    cy++;
    for(i=0; i<4; i++) {
      get_pos(cy*10 + cx + blk_t[ctype*4+rot][i]);
      if (board[py][px]!=0) {
        draw_blocks(cx, cy-1, ctype+1);
        if (cy==1) {
          // game over, and restart
          sfxn=6; sfxc=0;
          wb_fillRect(3, 40, 75, 30, wbRED);  
          wb_fillRect(4, 41, 73, 28, wbBLACK);
          wb_drawString("Game Over!", 15, 51, 2, 1);        
          delay(2000); // wait 2 secs to prevent user's extra keys
          //wb_fillRect(16, 240, 128, 16, wbBLACK);
          //wb_drawString("Press a key to restart!", 21, 244, 2, 1);
          while(getkey()!=0xff); while(getkey()==0xff);
          while(getkey()!=0xff);
          sfxn=7; sfxc=0;
          //wb_fillRect(1, 1, LCD_W/2-2, LCD_H-2, wbBLACK);
          //init_blocks();
          init_game();
          return;
        }
        go_next_blocks();
        return;
      }
    }
    draw_blocks(cx, cy, ctype+1);
    fall_time=0;
  }
}

void check_left(){
  for(i=0; i<4; i++) {
    get_pos(cy*10 + cx-1 + blk_t[ctype*4+rot][i]);
    if (px>cx+4) return; //left bound
  }
  draw_blocks(cx, cy, 0);
  for(i=0; i<4; i++) {
    get_pos(cy*10 + cx-1 + blk_t[ctype*4+rot][i]);
    if (board[py][px]!=0) {
      draw_blocks(cx, cy, ctype+1);
      return;
    }
  }
  cx--;
  draw_blocks(cx, cy, ctype+1);
}

void check_right(){
  for(i=0; i<4; i++) {
    get_pos(cy*10 + cx+1 + blk_t[ctype*4+rot][i]);
    if (px>cx-1) {
      board[py][px-1]=0;
    } else {
      draw_blocks(cx, cy, ctype+1);
      return;
    }
  }
  for(i=0; i<4; i++) {
    get_pos(cy*10 + cx+1 + blk_t[ctype*4+rot][i]);
    if (board[py][px]!=0) {
      draw_blocks(cx, cy, ctype+1);
      return;
    }
  }
  cx++;
  draw_blocks(cx, cy, ctype+1);
}

int check_rotate(){
  for(i=0; i<4; i++) {
    get_pos(cy*10 + cx + blk_t[ctype*4+rot][i]);
    if (pos<200) {
      if (board[py][px]!=0) return -2;
      if (px < cx) {
        cx--; return -1;
      }
      if (px > cx+4) {
        cx++; return -1;
      }
    }
  }
  return 0;
}

uint16_t sfx[8][8]={
  {0},
  {262, 0, 0, 0, 0, 0, 0, 0},
  {262, 330, 0, 0, 0, 0, 0, 0},
  {262, 330, 392, 0, 0, 0, 0, 0},
  {262, 330, 392, 523, 660, 0, 0, 0},
  {262, 330, 392, 523, 660, 784, 1047, 0},
  {262, 392, 330, 262, 16, 165, 131, 0},
  {1047,0,0,0,0,0,0,0}
};

void sfx_engine(){ // multi-tasking sound effect engine
  if (sfx_on) {
    if ((++task_c%3)==0) {
      if (sfxn!=0) {
        freq=sfx[sfxn][sfxc];
        if (freq) {
          ledcSetup(1, freq, 8);
          ledcWrite(1, 30);
        } else ledcWrite(1, 0);
        sfxc++;
        if (sfxc>7) sfxn=0;
      } 
    }
  }
}

hw_timer_t * _timer = NULL;  // the esp32 timer
void ticker_setup(){ // multitasking ticker engine
  wb_tickerInit(30000, sfx_engine);
  //ledcAttachPin(25, 1);
  ledcAttachPin(IO_LED, 1);
  ledcSetup(1, 200, 8);
  sfx_on=1;
  sfxn=0;
  task_c=0;
}

void init_preferences(){
  preferences.begin("tetris",false);
  highscore=preferences.getUInt("highscore",1);
  if (highscore==1) { // first run
    preferences.putUInt("highscore", 0);
    highscore=0;
  }
  if ((getkey()&0x90)==0) preferences.putUInt("highscore", 0);
  preferences.end();
}

void init_game(){
  init_keys();  
  init_blocks();
  
  //紅色外框
  wb_drawFastHLine(0, 0, LCD_W/2, wbRED);  
  wb_drawFastHLine(0, LCD_H-1, LCD_W/2, wbRED);
  wb_drawFastVLine(0, 0, LCD_H, wbRED);
  wb_drawFastVLine(LCD_W/2, 0, LCD_H, wbRED);
  
  //黃色字
  wb_setTextColor(wbYELLOW, wbYELLOW);  
  wb_drawString("<< OK:ESP32 >>", 85, 2, 2, 1);
  wb_drawString("Best"  , 90, 48, 2, 1);
  wb_drawString("Score" , 90, 68, 2, 1);
  wb_drawString("Lines" , 90, 88, 2, 1);
  wb_drawString("Level" , 90, 108, 2, 1);

  //綠色字
  wb_setTextColor(wbGREEN, wbGREEN);
  wb_drawString(itoa(highscore, tmpbuf, 10), 110, 58, 2, 1);
}

void check_downkey(){
  if ((((key&0x10)==0)&&last_key==0)) { // down
    fall_limit=1; 
    last_key=5;
  } else if (last_key==5) { // down release
    fall_limit=stage_limit; 
    last_key=0;
  }
}

void check_leftkey(){
  if (((key&0x80)==0)) { // repeatable key
    last_key=1;
    if (kc++%6==0) check_left(); // move delay = 6
    fall_limit=stage_limit;
  } else if (((key&0x80)==0x80)&&(last_key==1)) { // left release
    last_key=0; 
    kc=0;
  }
}

void check_rightkey(){
  if (((key&0x40)==0)) { // repeatable key
    last_key=2;
    if (kc++%6==0) check_right(); // move delay = 6
    fall_limit=stage_limit; 
  } else if (((key&0x40)==0x40)&&(last_key==2)) { // left release
    last_key=0; 
    kc=0;
  }
}

void check_selectkey(){
  if (((key&0x2)==0)&&(last_key==0)) { // repeatable key
    last_key=6;
    if (sfx_on==0) { 
      wb_fillRect(230, 290, 8, 8, wbBLACK); 
      sfxn=0; 
    } else {
      wb_setTextColor(wbRED, wbRED);
      wb_drawString("X", 230, 290, 1, 1);
    }
    sfx_on = 1 - sfx_on;
  } else if (((key&0x2)==0x2)&&(last_key==6)) { // left release
    last_key=0; 
  }
}

void check_rotrkey(){
  if (((key&0x04)==0)){ 
    last_key=3;
    if (kc++ % 15 == 0) { // rotate delay = 15
      draw_blocks(cx, cy, 0); // clean current blocks
      if (++rot>3) rot=0;
      while(1) { // dangerous!
        ret = check_rotate();
        if (ret==-2) {
          if (++rot>3) rot=0;
        } else if (ret==0) break; // pick next legal rotation
      }
      draw_blocks(cx, cy, ctype+1); // draw rotated blocks
    }
  } else if (((key&0x04)==0x04)&&(last_key==3)) {
    last_key=0;
    kc=0;
  }
}

void check_rotlkey(){ 
  if (((key&0x08)==0)) { 
    last_key=4;
    if (kc++ % 15 == 0) { // rotate delay = 15
      draw_blocks(cx, cy, 0); 
      if (--rot<0) rot=3;
      while(1) { // dangerous!
        ret = check_rotate();
        if (ret==-2) {
          if (--rot<0) rot=3;
        } else if (ret==0) break; 
      }
      draw_blocks(cx, cy, ctype+1);
    }
  } else if (((key&0x08)==0x08)&&(last_key==4)) {
    last_key=0;
    kc=0;
  }
}

void setup() {
  wb_init(3);
  init_preferences();
  ticker_setup();
  init_game();
}

void loop(){ 
  getkey();
  check_downkey();
  check_leftkey();
  check_rightkey();
  check_rotrkey();
  check_rotlkey();
  check_selectkey();
  draw_board();
  blocks_fall();
  delay(30);
}
